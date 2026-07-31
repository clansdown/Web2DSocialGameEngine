<script lang="ts">
  import { onDestroy } from 'svelte';
  import { currentCharacter } from '../lib/stores';
  import { getFiefdomRequest, buildRequest, getTextsRequest, getBuildingConfigsRequest } from '../lib/api';
  import type { FiefdomResponse, BuildingTypeConfig } from '../lib/api';
  import { getSessionToken, getInMemoryCredentials } from '../lib/auth';
  import { getConfigBoolean, setConfig as setConfigKV, getConfigNumber } from '../lib/storage';

  import {
    initEngine, setBoardSize, getMousePosition,
    setBackground, setBackgroundMode,
    setCameraFollowsPlayer, setBoardPanEnabled, destroyEngine,
    everyTick, whenLoaded
  } from '../../SimpleGame/ui/src/lib/simplegame';
  import {
    GameObject, GameObjectClass, createText
  } from '../../SimpleGame/ui/src/lib/gameclasses';
  import type { Text } from '../../SimpleGame/ui/src/lib/gameclasses';
  import { ButtonClass } from '../../SimpleGame/ui/src/lib/button';
  import { Column, LayoutJustify } from '../../SimpleGame/ui/src/lib/layout';

  interface Props {
    onBack: () => void;
  }

  let { onBack }: Props = $props();

  const BW = 20000;
  const BH = 20000;
  const CELL = 64;
  const CX = BW / 2;
  const CY = BH / 2;

  let canvasEl: HTMLCanvasElement;
  let debugDiv: HTMLDivElement;
  let loading = $state(true);
  let errorMsg = $state<string | null>(null);
  let fiefdomData: FiefdomResponse | null = $state(null);
  let buildingConfigs: Record<string, BuildingTypeConfig> = $state({});

  let buildingGameObjMap = new Map<number, GameObject>();
  let buildingClasses = new Map<string, GameObjectClass>();
  let underConstructionSet = new Set<number>();

  let placementMode = $state(false);
  let placementType = $state<string | null>(null);
  let ghostBuilding: GameObject | null = null;
  let ghostOverlayValid: GameObject | null = null;
  let ghostOverlayInvalid: GameObject | null = null;
  let ghostTooltip: Text | null = null;
  let ghostPos = { gx: 0, gy: 0 };
  let ghostValid = $state(false);
  let validOverlayClass: GameObjectClass | null = null;
  let invalidOverlayClass: GameObjectClass | null = null;

  let buildCol: Column | null = null;
  let cancelBtn: GameObject | null = null;

  let resText: Text | null = null;
  let mlText: Text | null = null;

  let showIntro = $state(false);
  let introHtml = $state('');

  function createOverlayClass(color: string, id: string): GameObjectClass {
    const c = document.createElement('canvas');
    c.width = 1;
    c.height = 1;
    const ctx = c.getContext('2d')!;
    ctx.fillStyle = color;
    ctx.fillRect(0, 0, 1, 1);
    return new GameObjectClass(id, c.toDataURL(), null);
  }

  function g2b(gx: number, gy: number, w: number, h: number): { x: number; y: number } {
    return { x: CX + gx * CELL + (w * CELL) / 2, y: CY + gy * CELL + (h * CELL) / 2 };
  }

  function b2g(bx: number, by: number): { gx: number; gy: number } {
    return { gx: Math.round((bx - CX) / CELL), gy: Math.round((by - CY) / CELL) };
  }

  function getRect(gx: number, gy: number, w: number, h: number):
    { l: number; t: number; r: number; b: number } {
    return { l: CX + gx * CELL, t: CY + gy * CELL, r: CX + (gx + w) * CELL, b: CY + (gy + h) * CELL };
  }

  function overlap(a: { l: number; t: number; r: number; b: number },
                    b: { l: number; t: number; r: number; b: number }): boolean {
    return a.l < b.r && a.r > b.l && a.t < b.b && a.b > b.t;
  }

  function getCfg(id: string): BuildingTypeConfig | undefined {
    return buildingConfigs[id];
  }

  function checkVal(gx: number, gy: number, typeId: string): { valid: boolean; reason: string } {
    if (!fiefdomData) return { valid: false, reason: 'No fiefdom data' };
    const cfg = getCfg(typeId);
    if (!cfg) return { valid: false, reason: 'Unknown building type' };
    if (fiefdomData.manor_level < cfg.min_manor_level) {
      return { valid: false, reason: `Need manor level ${cfg.min_manor_level}` };
    }
    const ghostRect = getRect(gx, gy, cfg.width, cfg.height);
    for (const b of fiefdomData.buildings || []) {
      const existing = getCfg(b.name);
      if (!existing) continue;
      if (overlap(ghostRect, getRect(b.x, b.y, existing.width, existing.height))) {
        return { valid: false, reason: `Overlaps ${existing.display_name}` };
      }
    }
    for (const [res, amt] of Object.entries(cfg.costs)) {
      const current = (fiefdomData as unknown as Record<string, number>)[res] || 0;
      if (current < amt) {
        return { valid: false, reason: `Not enough ${res} (need ${amt})` };
      }
    }
    return { valid: true, reason: '' };
  }

  function clearBuildings() {
    for (const obj of buildingGameObjMap.values()) obj.destroy();
    buildingGameObjMap.clear();
    buildingClasses.clear();
    underConstructionSet.clear();
  }

  function renderBuildings() {
    if (!fiefdomData) return;
    const homeBasePlaced = fiefdomData.buildings?.some(b => b.name === 'home_base');
    const now = Date.now() / 1000;

    for (const b of fiefdomData.buildings || []) {
      if (b.name === 'home_base') continue;
      const cfg = getCfg(b.name);
      if (!cfg) continue;

      const underConstruction = b.construction_start_ts > 0;
      const imgUrl = underConstruction ? cfg.construction_image : cfg.image;
      const classKey = b.name + (underConstruction ? '_con' : '');

      let cls = buildingClasses.get(classKey);
      if (!cls) {
        cls = new GameObjectClass(classKey, imgUrl, null);
        buildingClasses.set(classKey, cls);
      }

      const pos = g2b(b.x, b.y, cfg.width, cfg.height);
      const obj = new GameObject(cls, pos.x, pos.y);
      obj.width = cfg.width * CELL;
      obj.height = cfg.height * CELL;

      if (underConstruction) {
        underConstructionSet.add(b.id);
        const totalSec = cfg.construction_times[b.level] || cfg.construction_times[0] || 60;
        obj.var.construction_start = b.construction_start_ts;
        obj.var.construction_duration = totalSec;
        obj.var.progress = Math.min((now - b.construction_start_ts) / totalSec, 1);
        obj.setProgressBar(() => obj.var.progress, '#4caf50', '#333333', 0.9);
      }

      buildingGameObjMap.set(b.id, obj);
    }

    // Manor house (always at center)
    const manorCfg = getCfg('home_base');
    if (manorCfg) {
      const manorUnderConstruction = homeBasePlaced
        ? ((fiefdomData.buildings || []).find(b => b.name === 'home_base')?.construction_start_ts ?? 0) > 0
        : false;
      const manorImg = manorUnderConstruction ? manorCfg.construction_image : manorCfg.image;
      const manorKey = 'home_base' + (manorUnderConstruction ? '_con' : '');
      let manorCls = buildingClasses.get(manorKey);
      if (!manorCls) {
        manorCls = new GameObjectClass(manorKey, manorImg, null);
        buildingClasses.set(manorKey, manorCls);
      }
      const mPos = g2b(0, 0, 5, 5);
      const manorObj = new GameObject(manorCls, mPos.x, mPos.y);
      manorObj.width = 5 * CELL;
      manorObj.height = 5 * CELL;
      manorObj.opacity = homeBasePlaced ? 1.0 : 0.4;
      buildingGameObjMap.set(-1, manorObj);

      if (manorUnderConstruction) {
        const b = (fiefdomData.buildings || []).find(b => b.name === 'home_base')!;
        underConstructionSet.add(b.id);
        const totalSec = manorCfg.construction_times[b.level] || manorCfg.construction_times[0] || 60;
        manorObj.var.construction_start = b.construction_start_ts;
        manorObj.var.construction_duration = totalSec;
        manorObj.var.progress = Math.min((now - b.construction_start_ts) / totalSec, 1);
        manorObj.setProgressBar(() => manorObj.var.progress, '#4caf50', '#333333', 0.9);
      }
    }
  }

  function enterPlacement(typeId: string) {
    if (!fiefdomData) return;
    placementMode = true;
    placementType = typeId;

    const cfg = getCfg(typeId);
    if (!cfg) return;

    const ghostCls = new GameObjectClass('ghost_' + typeId, cfg.image, null);
    const pos = g2b(0, 0, cfg.width, cfg.height);
    ghostBuilding = new GameObject(ghostCls, pos.x, pos.y);
    ghostBuilding.width = cfg.width * CELL;
    ghostBuilding.height = cfg.height * CELL;
    ghostBuilding.opacity = 0.5;
    ghostBuilding.draggable = true;
    ghostBuilding.onDragMap.set(0, () => {
      if (ghostValid) {
        placeBuilding(ghostPos.gx, ghostPos.gy, typeId);
      }
    });

    if (!validOverlayClass) {
      validOverlayClass = createOverlayClass('rgba(0, 200, 0, 0.3)', 'v_overlay');
      invalidOverlayClass = createOverlayClass('rgba(200, 0, 0, 0.3)', 'i_overlay');
    }

    const vCls = validOverlayClass!;
    const iCls = invalidOverlayClass!;

    ghostOverlayValid = new GameObject(vCls, pos.x, pos.y);
    ghostOverlayValid.width = cfg.width * CELL;
    ghostOverlayValid.height = cfg.height * CELL;
    ghostOverlayValid.opacity = 0.6;
    ghostOverlayValid.visible = true;

    ghostOverlayInvalid = new GameObject(iCls, pos.x, pos.y);
    ghostOverlayInvalid.width = cfg.width * CELL;
    ghostOverlayInvalid.height = cfg.height * CELL;
    ghostOverlayInvalid.opacity = 0.6;
    ghostOverlayInvalid.visible = false;

    ghostTooltip = createText('', { x: pos.x, y: pos.y - cfg.height * CELL / 2 - 30 });

    ghostPos = { gx: 0, gy: 0 };
    ghostValid = false;

    if (buildCol) buildCol.visible = false;
    if (cancelBtn) cancelBtn.visible = true;
  }

  function exitPlacement() {
    placementMode = false;
    placementType = null;
    if (ghostBuilding) { ghostBuilding.destroy(); ghostBuilding = null; }
    if (ghostOverlayValid) { ghostOverlayValid.destroy(); ghostOverlayValid = null; }
    if (ghostOverlayInvalid) { ghostOverlayInvalid.destroy(); ghostOverlayInvalid = null; }
    if (ghostTooltip) { ghostTooltip.destroy(); ghostTooltip = null; }
    if (buildCol) buildCol.visible = true;
    if (cancelBtn) cancelBtn.visible = false;
  }

  async function placeBuilding(gx: number, gy: number, typeId: string) {
    if (!fiefdomData) return;
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds) return;
    try {
      await buildRequest({
        fiefdom_id: fiefdomData.id,
        building_type: typeId,
        x: gx, y: gy
      }, { username: creds.username, token });
      await loadFiefdomData();
      clearBuildings();
      renderBuildings();
      exitPlacement();
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : 'Failed to place building';
    }
  }

  async function loadFiefdomData() {
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !$currentCharacter) return;
    try {
      let fid = await getConfigNumber('fiefdom_id', 0);
      const data = await getFiefdomRequest(
        fid ? { fiefdom_id: fid } : { character_id: $currentCharacter.id },
        { username: creds.username, token }
      );
      if (data.id && !fid) await setConfigKV('fiefdom_id', data.id);
      fiefdomData = data;
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : 'Failed to load fiefdom';
    }
  }

  function setupGame() {
    renderBuildings();

    resText = createText('', { x: 20, y: 20 });
    mlText = createText('', { x: 20, y: 50 });

    buildCol = new Column(BW - 120, 80);

    const homeBasePlaced = fiefdomData?.buildings?.some(b => b.name === 'home_base');
    const available = Object.entries(buildingConfigs).filter(([id, cfg]) => {
      if (id === 'home_base' && homeBasePlaced) return false;
      return fiefdomData && fiefdomData.manor_level >= cfg.min_manor_level;
    });

    for (const [typeId, cfg] of available) {
      const bc = new ButtonClass('b_' + typeId);
      const btn = bc.spawn(0, 0, cfg.display_name, cfg.image, {
        width: 200, height: 100, backgroundOpacity: 0.85
      });
      btn.visible = true;
      btn.zIndex = 100;
      btn.onClick(0, () => enterPlacement(typeId));
      buildCol.addChild(btn);
    }

    const cc = new ButtonClass('cancel_manor');
    cancelBtn = cc.spawn(0, 0, 'Cancel', null, {
      width: 200, height: 50, color: '#666666', backgroundOpacity: 0.85
    });
    cancelBtn.visible = false;
    cancelBtn.zIndex = 100;
    cancelBtn.onClick(0, () => exitPlacement());

    buildCol.setGutter(10);
    buildCol.setPadding(0);
    buildCol.setJustify(LayoutJustify.START);
    buildCol.layout();

    loading = false;
  }

  async function initialize() {
    const introSeen = await getConfigBoolean('manor_intro_seen', false);
    if (!introSeen) {
      const texts = await getTextsRequest('en', ['manor_intro'], 'male');
      introHtml = texts['manor_intro'] || '';
      if (introHtml) {
        showIntro = true;
        loading = false;
        return;
      }
    }

    loading = true;

    // Fetch building configs from server
    try {
      buildingConfigs = await getBuildingConfigsRequest();
    } catch (e) {
      errorMsg = 'Failed to load building configs';
      loading = false;
      return;
    }

    await loadFiefdomData();

    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !$currentCharacter) {
      if (!errorMsg) errorMsg = 'Not authenticated';
      loading = false;
      return;
    }
    if (errorMsg) {
      loading = false;
      return;
    }

    // Auto-place manor house if not already placed
    if (fiefdomData && !fiefdomData.buildings?.some(b => b.name === 'home_base')) {
      try {
        await buildRequest({
          fiefdom_id: fiefdomData.id,
          building_type: 'home_base',
          x: 0, y: 0
        }, { username: creds.username, token });
        await loadFiefdomData();
      } catch (e) {
        // Silently continue — manor may already exist or be in progress
      }
    }

    debugDiv = document.createElement('div');
    initEngine(canvasEl, debugDiv, false, () => {});
    setBoardSize(BW, BH);
    setBackground(['/images/manor/ground/grass.jpg']);
    setBackgroundMode('tile');
    setCameraFollowsPlayer(false);
    setBoardPanEnabled(true);

    whenLoaded(setupGame);

    everyTick(() => {
      if (resText && fiefdomData) {
        resText.text = `Gold: ${fiefdomData.gold}  Wood: ${fiefdomData.wood}  Stone: ${fiefdomData.stone}  Grain: ${fiefdomData.grain}`;
      }
      if (mlText && fiefdomData) {
        mlText.text = `Manor Level ${fiefdomData.manor_level}`;
      }

      if (ghostBuilding && placementType) {
        const cfg = getCfg(placementType);
        if (!cfg) return;

        const mouse = getMousePosition();
        const snapped = b2g(mouse.x, mouse.y);
        const pos = g2b(snapped.gx, snapped.gy, cfg.width, cfg.height);

        ghostBuilding.x = pos.x;
        ghostBuilding.y = pos.y;
        ghostPos = snapped;

        if (ghostOverlayValid) { ghostOverlayValid.x = pos.x; ghostOverlayValid.y = pos.y; }
        if (ghostOverlayInvalid) { ghostOverlayInvalid.x = pos.x; ghostOverlayInvalid.y = pos.y; }

        const result = checkVal(snapped.gx, snapped.gy, placementType);
        ghostValid = result.valid;

        if (!result.valid) {
          if (ghostOverlayValid) ghostOverlayValid.visible = false;
          if (ghostOverlayInvalid) ghostOverlayInvalid.visible = true;
          if (ghostTooltip) {
            ghostTooltip.x = pos.x;
            ghostTooltip.y = pos.y - cfg.height * CELL / 2 - 30;
            ghostTooltip.text = result.reason;
            ghostTooltip.opacity = 1;
          }
        } else {
          if (ghostOverlayValid) ghostOverlayValid.visible = true;
          if (ghostOverlayInvalid) ghostOverlayInvalid.visible = false;
          if (ghostTooltip) ghostTooltip.opacity = 0;
        }
      }

      // Update progress bars for construction buildings
      const now = Date.now() / 1000;
      for (const id of underConstructionSet) {
        const obj = buildingGameObjMap.get(id);
        if (obj && obj.var.construction_duration) {
          obj.var.progress = Math.min(
            (now - obj.var.construction_start) / obj.var.construction_duration,
            1
          );
          if (obj.var.progress > 0.999) {
            underConstructionSet.delete(id);
            obj.setProgressBar(null, '#4caf50', '#333333', 0.9);
          }
        }
      }
    });
  }

  function dismissIntro() {
    showIntro = false;
    setConfigKV('manor_intro_seen', true);
    initialize();
  }

  $effect(() => {
    initialize();
  });

  onDestroy(() => {
    destroyEngine();
  });
</script>

<div class="manor-container position-relative">
  {#if loading && !showIntro && !errorMsg}
    <div class="d-flex justify-content-center align-items-center" style="height: 80vh;">
      <div class="text-center">
        <div class="spinner-border mb-3" role="status">
          <span class="visually-hidden">Loading...</span>
        </div>
        <p class="text-muted">Loading Manor...</p>
      </div>
    </div>
  {:else if showIntro}
    <div class="container py-5">
      <div class="card">
        <div class="card-body text-center p-5">
          <h3 class="card-title">Your Manor</h3>
          <p class="text-muted mt-3">{introHtml}</p>
          <button class="btn btn-primary mt-4" onclick={dismissIntro}>Begin</button>
        </div>
      </div>
    </div>
  {:else if errorMsg}
    <div class="container py-5">
      <button class="btn btn-outline-secondary mb-4" onclick={onBack}>&larr; Back</button>
      <div class="alert alert-danger">{errorMsg}</div>
    </div>
  {/if}

  <canvas bind:this={canvasEl} class="w-100" style="display: {loading || showIntro || errorMsg ? 'none' : 'block'}; height: calc(100vh - 60px);"></canvas>
</div>
