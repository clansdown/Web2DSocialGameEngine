<script lang="ts">
  import { onDestroy } from 'svelte';
  import { currentCharacter } from '../lib/stores';
  import { getFiefdomRequest, buildRequest, getBuildingConfigsRequest, setFiefdomImportRequest, setFiefdomReserveRequest, setBuildingOutputRateRequest } from '../lib/api';
  import type { FiefdomResponse, BuildingTypeConfig, EconomyReport } from '../lib/api';
  import { loadTexts } from '../lib/text';
  import { getSessionToken, getInMemoryCredentials } from '../lib/auth';
  import { getConfigBoolean, setConfig as setConfigKV, getConfigNumber } from '../lib/storage';

  import {
    initEngine, setBoardSize, getMousePosition,
    setBackground, setBackgroundMode,
    setCameraFollowsPlayer, setBoardPanEnabled, destroyEngine,
    setViewportSize, setCameraPosition, setBackgroundTileSize,
    onKeyDown, everyTick
  } from '../../SimpleGame/ui/src/lib/simplegame';
  import {
    ItemClass, createText
  } from '../../SimpleGame/ui/src/lib/gameclasses';
  import type { GameObject, Text } from '../../SimpleGame/ui/src/lib/gameclasses';
  import { ButtonClass, type Button } from '../../SimpleGame/ui/src/lib/button';
  import { Column, LayoutJustify } from '../../SimpleGame/ui/src/lib/layout';

  interface Props {
    onBack: () => void;
  }

  let { onBack }: Props = $props();

  const BW = 20000;
  const BH = 20000;
  // Grid cells are NOT 1:1 with board units. One building cell = 64 board units.
  const CELL_TO_BOARD_UNITS = 64;
  const BOARD_TO_CELL = 1 / CELL_TO_BOARD_UNITS;
  const GRASS_TILE = 1800;
  const CX = BW / 2;
  const CY = BH / 2;

  // Shared HUD button look: like Bootstrap btn-outline-light but with a
  // semi-opaque dark fill (the engine only fades the background layer, so
  // icons and label text stay fully opaque).
  const MANOR_BTN_COLOR = '#212529';
  const MANOR_BTN_FG = '#f8f9fa';
  const MANOR_BTN_OPACITY = 0.55;
  const MANOR_BTN_RADIUS = 6;
  const MANOR_BTN_ACTIVE_COLOR = '#0d6efd';

  let canvasEl: HTMLCanvasElement;
  let debugDiv: HTMLDivElement;
  let loading = $state(true);
  let errorMsg = $state<string | null>(null);
  let fiefdomData: FiefdomResponse | null = $state(null);
  let buildingConfigs: Record<string, BuildingTypeConfig> = $state({});
  let buildingOrder: string[] = [];

  let buildingGameObjMap = new Map<number, GameObject>();
  let buildingClasses = new Map<string, ItemClass>();
  let underConstructionSet = new Set<number>();

  let placementMode = $state(false);
  let placementType = $state<string | null>(null);
  let ghostBuilding: GameObject | null = null;
  let ghostOverlayValid: GameObject | null = null;
  let ghostOverlayInvalid: GameObject | null = null;
  let ghostTooltip: Text | null = null;
  let ghostPos = { gx: 0, gy: 0 };
  let ghostValid = $state(false);
  let validOverlayClass: ItemClass | null = null;
  let invalidOverlayClass: ItemClass | null = null;

  let buildCol: Column | null = null;
  let actionCol: Column | null = null;
  let mainBtn: Button | null = null;
  let buildBtn: Button | null = null;
  let economyBtn: Button | null = null;
  let productionBtn: Button | null = null;
  let panelTop = $state(220);
  let manorTexts = $state<Record<string, string>>({});
  let buildableIds: string[] = [];
  let buildButtons = new Map<string, Button>();

  let constructionRefreshInFlight = false;
  let completionPollRequested = new Set<number>();

  let showIntro = $state(false);
  let introHtml = $state('');

  let showEconomy = $state(false);
  let showProduction = $state(false);
  let economyReport = $state<EconomyReport | null>(null);
  const IMPORT_RESOURCES = ['grain', 'wood', 'steel', 'bronze', 'stone', 'leather', 'mana', 'charcoal', 'iron', 'ironwork', 'fancy_ironwork'];
  const RESOURCE_DISPLAY: Record<string, string> = {
    grain: 'Grain', wood: 'Wood', steel: 'Steel', bronze: 'Bronze',
    stone: 'Stone', leather: 'Leather', mana: 'Mana', charcoal: 'Charcoal',
    iron: 'Iron', ironwork: 'Ironwork', fancy_ironwork: 'Fancy Ironwork'
  };
  let reserveInputs = $state<Record<string, number>>({});

  function createOverlayClass(color: string, id: string): ItemClass {
    const c = document.createElement('canvas');
    c.width = 1;
    c.height = 1;
    const ctx = c.getContext('2d')!;
    ctx.fillStyle = color;
    ctx.fillRect(0, 0, 1, 1);
    return new ItemClass(id, c.toDataURL());
  }

  // ── Road tiles ────────────────────────────────────────────────────────────
  // Roads auto-tile: each 1x1 tile picks a base image (straight/corner/
  // three_way/four_way) from the config's `road_tiles_canonical` and is rotated
  // via the engine's setOrientation to match its orthogonal road neighbors.
  // The tile art is generated procedurally (canvas data URLs) so the network
  // always renders with aligned seams; real art can later replace the PNGs
  // referenced by `road_tiles` (see tools/generate_road_tiles.py).

  const ROAD_DIRS = ['n', 'e', 's', 'w'] as const;

  /** Rotates a set of road directions clockwise by k quarter-turns (n→e→s→w). */
  function rotateRoadDirs(dirs: string[], k: number): string[] {
    return dirs.map(d => ROAD_DIRS[(ROAD_DIRS.indexOf(d as (typeof ROAD_DIRS)[number]) + k) % 4]);
  }

  /** True if two direction arrays contain the same set. */
  function sameRoadDirs(a: string[], b: string[]): boolean {
    if (a.length !== b.length) return false;
    const setB = new Set(b);
    return a.every(d => setB.has(d));
  }

  /** Draws a dirt road tile with a path across the given screen directions. */
  function makeRoadTileDataUrl(dirs: string[], size: number = 128): string {
    const c = document.createElement('canvas');
    c.width = size;
    c.height = size;
    const ctx = c.getContext('2d')!;
    ctx.fillStyle = '#5d5340';
    ctx.fillRect(0, 0, size, size);
    // Speckled dirt
    for (let i = 0; i < 400; i++) {
      const shade = 60 + Math.floor(Math.random() * 40);
      ctx.fillStyle = `rgb(${shade}, ${shade - 10}, ${shade - 28})`;
      ctx.fillRect(Math.floor(Math.random() * size), Math.floor(Math.random() * size), 2, 2);
    }
    // Path band (gravel) across connected sides
    const band = Math.round(size * 0.42);
    const half = Math.round(band / 2);
    const cx = Math.round(size / 2);
    const cy = Math.round(size / 2);
    ctx.fillStyle = '#8a7f6e';
    if (dirs.includes('n')) ctx.fillRect(cx - half, 0, band, cy + half);
    if (dirs.includes('s')) ctx.fillRect(cx - half, cy - half, band, size - cy + half);
    if (dirs.includes('e')) ctx.fillRect(cx - half, cy - half, size - cx + half, band);
    if (dirs.includes('w')) ctx.fillRect(0, cy - half, cx + half, band);
    // Stone edge lines
    ctx.strokeStyle = '#5b5044';
    ctx.lineWidth = 2;
    ctx.strokeRect(cx - half, 0, band, cy + half);
    ctx.strokeRect(cx - half, cy - half, band, size - cy + half);
    ctx.strokeRect(cx - half, cy - half, size - cx + half, band);
    ctx.strokeRect(0, cy - half, cx + half, band);
    // A few lighter stones
    ctx.fillStyle = '#a89c8a';
    for (let i = 0; i < 30; i++) {
      const sx = Math.floor(Math.random() * size);
      const sy = Math.floor(Math.random() * size);
      ctx.fillRect(sx, sy, 3, 3);
    }
    return c.toDataURL();
  }

  let roadTileClasses = new Map<string, ItemClass>();

  /**
   * Resolves which road base tile + rotation (degrees) to use for a road at the
   * given grid cell, based on its orthogonal road neighbors.
   *
   * @param gx - Road cell x
   * @param gy - Road cell y
   * @param cfg - The road building config (road_tiles / road_tiles_canonical)
   * @param buildings - All fiefdom buildings (for neighbor lookup)
   * @returns The tile key and orientation; falls back to the straight tile
   */
  function resolveRoadTile(gx: number, gy: number, cfg: BuildingTypeConfig,
                           buildings: Array<{ name: string; x: number; y: number }>):
    { key: string; orientation: number } {
    const actual: string[] = [];
    if (buildings.some(b => b.name === 'road' && b.x === gx && b.y === gy - 1)) actual.push('n');
    if (buildings.some(b => b.name === 'road' && b.x === gx + 1 && b.y === gy)) actual.push('e');
    if (buildings.some(b => b.name === 'road' && b.x === gx && b.y === gy + 1)) actual.push('s');
    if (buildings.some(b => b.name === 'road' && b.x === gx - 1 && b.y === gy)) actual.push('w');

    const canonical = (cfg.road_tiles_canonical as Record<string, string[]> | undefined) ?? {};
    for (const [key, dirs] of Object.entries(canonical)) {
      if (!Array.isArray(dirs)) continue;
      for (let k = 0; k < 4; k++) {
        if (sameRoadDirs(rotateRoadDirs(dirs, k), actual)) {
          return { key, orientation: k * 90 };
        }
      }
    }
    // Four-way (all sides) never appears in canonical — it needs no rotation.
    if (actual.length === 4) return { key: 'four_way', orientation: 0 };
    // Isolated / dead-ends reuse the straight tile (pass-through look).
    let k = 0;
    if (actual.length === 1) {
      k = (actual[0] === 'n' || actual[0] === 's') ? 1 : 0;
    }
    return { key: 'straight', orientation: k * 90 };
  }

  /** Builds (and caches) ItemClasses for the four road tile base images. */
  function ensureRoadTileClasses(cfg: BuildingTypeConfig): void {
    if (roadTileClasses.size > 0) return;
    const canonical = (cfg.road_tiles_canonical as Record<string, string[]> | undefined) ?? {};
    const tileDirs: Record<string, string[]> = {
      straight: canonical.straight ?? ['e', 'w'],
      corner: canonical.corner ?? ['n', 'e'],
      three_way: canonical.three_way ?? ['n', 'e', 'w'],
      four_way: ['n', 'e', 's', 'w']
    };
    for (const [key, dirs] of Object.entries(tileDirs)) {
      roadTileClasses.set(key, new ItemClass('road_' + key, makeRoadTileDataUrl(dirs)));
    }
  }

  /**
   * Sets a button's icon to a fixed height while preserving the source image's
   * aspect ratio (reads naturalWidth/naturalHeight once loaded), so non-square
   * building art is never squashed into a square.
   *
   * @param btn - The button whose icon to size
   * @param height - Desired icon height in screen pixels (width scales to match)
   */
  function setAspectIconSize(btn: Button, height: number): void {
    const img = btn.icon;
    if (!img) return;
    const apply = () => {
      if (img.naturalWidth > 0 && img.naturalHeight > 0) {
        btn.setIconHeight(height);
        btn.setIconWidth(Math.round(height * (img.naturalWidth / img.naturalHeight)));
      }
    };
    if (img.complete && img.naturalWidth > 0) apply();
    else img.onload = apply;
  }

  function g2b(gx: number, gy: number, w: number, h: number): { x: number; y: number } {
    return { x: CX + gx * CELL_TO_BOARD_UNITS, y: CY + gy * CELL_TO_BOARD_UNITS };
  }

  function b2g(bx: number, by: number): { gx: number; gy: number } {
    return { gx: Math.round((bx - CX) * BOARD_TO_CELL), gy: Math.round((by - CY) * BOARD_TO_CELL) };
  }

  function getRect(gx: number, gy: number, w: number, h: number):
    { l: number; t: number; r: number; b: number } {
    const c = g2b(gx, gy, w, h);
    return {
      l: c.x - (w * CELL_TO_BOARD_UNITS) / 2,
      t: c.y - (h * CELL_TO_BOARD_UNITS) / 2,
      r: c.x + (w * CELL_TO_BOARD_UNITS) / 2,
      b: c.y + (h * CELL_TO_BOARD_UNITS) / 2
    };
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
    const maxCount = cfg.max_per_fiefdom as number | undefined;
    if (maxCount != null) {
      const count = (fiefdomData.buildings ?? []).filter(b => b.name === typeId).length;
      if (count >= maxCount) {
        return { valid: false, reason: 'Already built (max reached)' };
      }
    }
    const prereq = ((cfg.prerequisites as Array<Record<string, number>> | undefined)?.[0]) ?? {};
    for (const [key, reqLevel] of Object.entries(prereq)) {
      if (key === 'manor_level') continue;
      const have = (fiefdomData.buildings ?? []).find(b => b.name === key)?.level ?? 0;
      if (have < reqLevel) {
        return { valid: false, reason: `Need ${key} level ${reqLevel}` };
      }
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

  /**
   * Formats a gold amount as shillings and (if non-zero) pence, so monetary
   * build costs are never shown as fractional gold. 1 gold = 20 shillings =
   * 240 pence; 1 shilling = 12 pence.
   *
   * @param gold - Gold amount (fractional allowed; rounded to the nearest penny)
   * @returns String like "12s" or "10s 6d"; pence-only ("6d") if under 1 shilling
   */
  function formatShillingsPence(gold: number): string {
    const totalPence = Math.round(gold * 240);
    const s = Math.floor(totalPence / 12);
    const d = totalPence % 12;
    const sPart = s > 0 ? `${s}s` : '';
    const dPart = d > 0 ? `${d}d` : '';
    return sPart + (sPart && dPart ? ' ' : '') + dPart;
  }

  /**
   * Formats a building's level-1 costs as a compact price string.
   *
   * @param costs - Map of resource name -> level-1 cost amount
   * @returns String like "10s 10w" (gold in shillings/pence, wood= w, stone= st);
   *          empty if no costs
   */
  function formatCost(costs: Record<string, number>): string {
    const units: Record<string, string> = { wood: 'w', stone: 'st' };
    const parts: string[] = [];
    for (const [res, amt] of Object.entries(costs)) {
      if (amt <= 0) continue;
      if (res === 'gold') {
        parts.push(formatShillingsPence(amt));
      } else if (res === 'silver_pence') {
        parts.push(`${Number.isInteger(amt) ? amt : amt.toFixed(1)}d`);
      } else {
        const unit = units[res];
        if (!unit) continue;
        parts.push(`${Number.isInteger(amt) ? amt : amt.toFixed(1)}${unit}`);
      }
    }
    return parts.join(' ');
  }

  /**
   * Whether the player can currently build a new instance of the given type,
   * mirroring the server's build checks: manor level, max_per_fiefdom,
   * level-1 prerequisites (non-manor_level keys = required buildings), and
   * level-1 cost affordability.
   *
   * @param typeId - Building type id from the config
   * @returns True if buildable right now
   */
  function canBuild(typeId: string): boolean {
    if (!fiefdomData) return false;
    const cfg = getCfg(typeId);
    if (!cfg) return false;
    if (fiefdomData.manor_level < (cfg.min_manor_level ?? 1)) return false;
    const maxCount = cfg.max_per_fiefdom as number | undefined;
    if (maxCount != null) {
      const count = (fiefdomData.buildings ?? []).filter(b => b.name === typeId).length;
      if (count >= maxCount) return false;
    }
    const prereq = ((cfg.prerequisites as Array<Record<string, number>> | undefined)?.[0]) ?? {};
    for (const [key, reqLevel] of Object.entries(prereq)) {
      if (key === 'manor_level') continue;
      const have = (fiefdomData.buildings ?? []).find(b => b.name === key)?.level ?? 0;
      if (have < reqLevel) return false;
    }
    for (const [res, amt] of Object.entries(cfg.costs)) {
      const have = (fiefdomData as unknown as Record<string, number>)[res] ?? 0;
      if (have < amt) return false;
    }
    return true;
  }

  /**
   * Re-evaluates every build-palette button's enabled state and applies
   * SimpleGame's disabled (grey overlay + click suppression) when the building
   * cannot currently be built.
   */
  function updateBuildButtonStates(): void {
    for (const [typeId, btn] of buildButtons) {
      btn.setDisabled(!canBuild(typeId));
    }
  }

  function clearBuildings() {
    for (const obj of buildingGameObjMap.values()) obj.destroy();
    buildingGameObjMap.clear();
    buildingClasses.clear();
    roadTileClasses.clear();
    underConstructionSet.clear();
  }

  function renderBuildings() {
    if (!fiefdomData) return;
    const homeBasePlaced = fiefdomData.buildings?.some(b => b.name === 'home_base');

    for (const b of fiefdomData.buildings || []) {
      if (b.name === 'home_base') continue;
      const cfg = getCfg(b.name);
      if (!cfg) continue;

      const underConstruction = b.construction_start_ts > 0;

      // Roads auto-tile: pick the base image + rotation from orthogonal
      // neighbors. Roads are always level >= 1 (instant build), so no
      // construction variant exists.
      if (b.name === 'road' && !underConstruction) {
        ensureRoadTileClasses(cfg);
        const resolved = resolveRoadTile(b.x, b.y, cfg, fiefdomData.buildings ?? []);
        let cls = roadTileClasses.get(resolved.key);
        if (!cls) {
          cls = new ItemClass('road_' + resolved.key, makeRoadTileDataUrl(['e', 'w']));
          roadTileClasses.set(resolved.key, cls);
        }
        const pos = g2b(b.x, b.y, 1, 1);
        const obj = cls.spawn(pos.x, pos.y);
        obj.width = 1 * CELL_TO_BOARD_UNITS;
        obj.height = 1 * CELL_TO_BOARD_UNITS;
        obj.setOrientation(resolved.orientation);
        buildingGameObjMap.set(b.id, obj);
        continue;
      }

      const imgUrl = underConstruction ? cfg.construction_image : cfg.image;
      const classKey = b.name + (underConstruction ? '_con' : '');

      let cls = buildingClasses.get(classKey);
      if (!cls) {
        cls = new ItemClass(classKey, imgUrl);
        buildingClasses.set(classKey, cls);
      }

      const pos = g2b(b.x, b.y, cfg.width, cfg.height);
      const obj = cls.spawn(pos.x, pos.y);
      obj.width = cfg.width * CELL_TO_BOARD_UNITS;
      obj.height = cfg.height * CELL_TO_BOARD_UNITS;

      if (underConstruction) {
        underConstructionSet.add(b.id);
        const totalSec = cfg.construction_times[b.level] || cfg.construction_times[0] || 60;
        obj.var.construction_start = b.construction_start_ts;
        obj.var.construction_duration = totalSec;
        obj.setProgressBar(
          () => (Date.now() / 1000 - obj.var.construction_start) / obj.var.construction_duration,
          '#4caf50', '#333333', 0.9
        );
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
        manorCls = new ItemClass(manorKey, manorImg);
        buildingClasses.set(manorKey, manorCls);
      }
      const mPos = g2b(0, 0, manorCfg.width, manorCfg.height);
      const manorObj = manorCls.spawn(mPos.x, mPos.y);
      manorObj.width = manorCfg.width * CELL_TO_BOARD_UNITS;
      manorObj.height = manorCfg.height * CELL_TO_BOARD_UNITS;
      manorObj.opacity = homeBasePlaced ? 1.0 : 0.4;

      if (manorUnderConstruction) {
        const b = (fiefdomData.buildings || []).find(b => b.name === 'home_base')!;
        // Key by the home_base row id so the everyTick completion poll finds it.
        buildingGameObjMap.set(b.id, manorObj);
        underConstructionSet.add(b.id);
        const totalSec = manorCfg.construction_times[b.level] || manorCfg.construction_times[0] || 60;
        manorObj.var.construction_start = b.construction_start_ts;
        manorObj.var.construction_duration = totalSec;
        manorObj.setProgressBar(
          () => (Date.now() / 1000 - manorObj.var.construction_start) / manorObj.var.construction_duration,
          '#4caf50', '#333333', 0.9
        );
      } else {
        buildingGameObjMap.set(-1, manorObj);
      }
    }
  }

  function enterPlacement(typeId: string) {
    if (!fiefdomData) return;
    if (placementMode && placementType === typeId) {
      exitPlacement();
      return;
    }
    exitPlacement();

    placementMode = true;
    placementType = typeId;

    const cfg = getCfg(typeId);
    if (!cfg) return;
    const ghostCls = new ItemClass('ghost_' + typeId, cfg.image);
    const pos = g2b(0, 0, cfg.width, cfg.height);

    ghostBuilding = ghostCls.spawn(pos.x, pos.y);
    ghostBuilding.width = cfg.width * CELL_TO_BOARD_UNITS;
    ghostBuilding.height = cfg.height * CELL_TO_BOARD_UNITS;
    // Hitbox is copied from the class at spawn time, before its image loads, so
    // it is 0 — set it to the building footprint or the engine never hits the
    // ghost (mousedown falls through to board panning, suppressing clicks).
    ghostBuilding.hitboxWidth = cfg.width * CELL_TO_BOARD_UNITS;
    ghostBuilding.hitboxHeight = cfg.height * CELL_TO_BOARD_UNITS;
    ghostBuilding.hitboxXOffset = 0;
    ghostBuilding.hitboxYOffset = 0;
    ghostBuilding.opacity = 0.5;
    ghostBuilding.draggable = true;
    ghostBuilding.onClick(0, () => {
      if (ghostValid) {
        placeBuilding(ghostPos.gx, ghostPos.gy, typeId);
      }
    });
    ghostBuilding.onDragEnd(0, () => {
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

    ghostOverlayValid = vCls.spawn(pos.x, pos.y);
    ghostOverlayValid.width = cfg.width * CELL_TO_BOARD_UNITS;
    ghostOverlayValid.height = cfg.height * CELL_TO_BOARD_UNITS;
    ghostOverlayValid.opacity = 0.6;
    ghostOverlayValid.visible = true;

    ghostOverlayInvalid = iCls.spawn(pos.x, pos.y);
    ghostOverlayInvalid.width = cfg.width * CELL_TO_BOARD_UNITS;
    ghostOverlayInvalid.height = cfg.height * CELL_TO_BOARD_UNITS;
    ghostOverlayInvalid.opacity = 0.6;
    ghostOverlayInvalid.visible = false;

    ghostTooltip = createText('', { x: pos.x, y: pos.y - cfg.height * CELL_TO_BOARD_UNITS / 2 - 30 });

    ghostPos = { gx: 0, gy: 0 };
    ghostValid = false;
  }

  function exitPlacement() {
    placementMode = false;
    placementType = null;
    if (ghostBuilding) { ghostBuilding.destroy(); ghostBuilding = null; }
    if (ghostOverlayValid) { ghostOverlayValid.destroy(); ghostOverlayValid = null; }
    if (ghostOverlayInvalid) { ghostOverlayInvalid.destroy(); ghostOverlayInvalid = null; }
    if (ghostTooltip) { ghostTooltip.destroy(); ghostTooltip = null; }
  }

  async function placeBuilding(gx: number, gy: number, typeId: string) {
    if (!fiefdomData) return;
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !$currentCharacter) return;
    try {
      await buildRequest({
        fiefdom_id: fiefdomData.id,
        building_type: typeId,
        x: gx, y: gy,
        character_id: $currentCharacter.id
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
      economyReport = data.economy_report || null;
      updateBuildButtonStates();
      if (data.reserves) {
        const next: Record<string, number> = {};
        for (const res of IMPORT_RESOURCES) {
          next[res] = typeof data.reserves[res] === 'number' ? data.reserves[res] : 0;
        }
        reserveInputs = next;
      }
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : 'Failed to load fiefdom';
    }
  }

  /**
   * Formats a gold amount as a medieval breakdown: 240 pence = 1 gold,
   * 12 pence = 1 shilling, 20 shillings = 1 pound.
   *
   * @param gold - Gold amount (fractional allowed; rounded to the nearest penny)
   * @returns String like "12g 3s 6d"
   */
  function formatGold(gold: number): string {
    const totalPence = Math.round(gold * 240);
    const g = Math.floor(totalPence / 240);
    const rem = totalPence % 240;
    const s = Math.floor(rem / 12);
    const d = rem % 12;
    return `${g}g ${s}s ${d}d`;
  }

  function constructionSignature(): string {
    return (fiefdomData?.buildings ?? [])
      .map(b => `${b.id}:${b.level}:${b.construction_start_ts}`).join('|');
  }

  async function pollConstruction(): Promise<void> {
    if (constructionRefreshInFlight) return;
    constructionRefreshInFlight = true;
    try {
      const before = constructionSignature();
      await loadFiefdomData();
      if (constructionSignature() !== before) {
        clearBuildings();
        renderBuildings();
      }
      const stillUnder = new Set((fiefdomData?.buildings ?? [])
        .filter(b => b.construction_start_ts > 0).map(b => b.id));
      for (const id of completionPollRequested) {
        if (!stillUnder.has(id)) completionPollRequested.delete(id);
      }
    } finally {
      constructionRefreshInFlight = false;
    }
  }

  /**
   * Highlights whichever panel toggle is currently open (Bootstrap-primary
   * fill) and restores the glassy fill for the other.
   */
  function updateToggleButtonStates(): void {
    if (economyBtn) economyBtn.setBackgroundColor(showEconomy ? MANOR_BTN_ACTIVE_COLOR : MANOR_BTN_COLOR);
    if (productionBtn) productionBtn.setBackgroundColor(showProduction ? MANOR_BTN_ACTIVE_COLOR : MANOR_BTN_COLOR);
  }

  async function toggleEconomy() {
    showEconomy = !showEconomy;
    if (showEconomy) {
      showProduction = false;
      await loadFiefdomData();
    }
    updateToggleButtonStates();
  }

  async function toggleProduction() {
    showProduction = !showProduction;
    if (showProduction) {
      showEconomy = false;
      await loadFiefdomData();
    }
    updateToggleButtonStates();
  }

  /**
   * Sets how much of a building output actually runs (0..1). Also scales that
   * output's input requirements. Reloads fiefdom data to reflect server state.
   */
  async function setOutputRate(buildingId: number, output: string, rate: number): Promise<void> {
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !fiefdomData) return;
    try {
      await setBuildingOutputRateRequest(buildingId, output, rate, { username: creds.username, token });
      await loadFiefdomData();
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : 'Failed to set output rate';
    }
  }

  async function toggleImport(resource: string) {
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !fiefdomData || !fiefdomData.import_settings) return;
    const next = !fiefdomData.import_settings[resource];
    try {
      const res = await setFiefdomImportRequest(fiefdomData.id, resource, next, { username: creds.username, token });
      if (fiefdomData) fiefdomData.import_settings = res.import_settings;
      await loadFiefdomData();
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : 'Failed to update import setting';
    }
  }

  async function setReserve(resource: string) {
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !fiefdomData) return;
    const value = reserveInputs[resource] || 0;
    try {
      const res = await setFiefdomReserveRequest(fiefdomData.id, resource, value, { username: creds.username, token });
      if (fiefdomData) fiefdomData.reserves = res.reserves;
      await loadFiefdomData();
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : 'Failed to update reserve';
    }
  }

  function setupGame() {
    renderBuildings();

    buildCol = new Column(0, 0);
    buildCol.hud = true;
    buildCol.visible = false;

    for (const typeId of buildableIds) {
      const cfg = getCfg(typeId);
      if (!cfg) continue;
      const bc = new ButtonClass('b_' + typeId);
      const costStr = formatCost(cfg.costs);
      const label = (manorTexts['ui_building_' + typeId] ?? '') + (costStr ? '  ' + costStr : '');
      const btn = bc.spawn(0, 0, label, cfg.image, {
        width: 280, height: 56,
        color: MANOR_BTN_COLOR, foregroundColor: MANOR_BTN_FG,
        backgroundOpacity: MANOR_BTN_OPACITY, cornerRadius: MANOR_BTN_RADIUS,
        iconLayout: 'left', iconPadding: 10
      });
      btn.hud = true;
      btn.visible = true;
      btn.zIndex = 100;
      btn.onClick(0, () => enterPlacement(typeId));
      setAspectIconSize(btn, 32);
      buildCol.addChild(btn);
      buildButtons.set(typeId, btn);
    }

    buildCol.setGutter(10);
    buildCol.setPadding(0);
    buildCol.setJustify(LayoutJustify.START);
    buildCol.layout();

    updateBuildButtonStates();

    // Top-right action stack: all four HUD buttons as SimpleGame buttons so
    // they share one coordinate space and one glassy style.
    actionCol = new Column(0, 0);
    actionCol.hud = true;
    actionCol.visible = true;

    const toggleStyle = {
      width: 140, height: 40,
      color: MANOR_BTN_COLOR, foregroundColor: MANOR_BTN_FG,
      backgroundOpacity: MANOR_BTN_OPACITY, cornerRadius: MANOR_BTN_RADIUS
    };

    const eb = new ButtonClass('manor_economy');
    economyBtn = eb.spawn(0, 0, manorTexts['ui_manor_economy'], null, toggleStyle);
    economyBtn.hud = true;
    economyBtn.visible = true;
    economyBtn.zIndex = 100;
    economyBtn.onClick(0, () => toggleEconomy());
    actionCol.addChild(economyBtn);

    const pb = new ButtonClass('manor_production');
    productionBtn = pb.spawn(0, 0, manorTexts['ui_manor_production'], null, toggleStyle);
    productionBtn.hud = true;
    productionBtn.visible = true;
    productionBtn.zIndex = 100;
    productionBtn.onClick(0, () => toggleProduction());
    actionCol.addChild(productionBtn);

    const bb = new ButtonClass('manor_build');
    buildBtn = bb.spawn(0, 0, manorTexts['ui_manor_build_btn'], null, toggleStyle);
    buildBtn.hud = true;
    buildBtn.visible = true;
    buildBtn.zIndex = 100;
    buildBtn.onClick(0, () => {
      if (buildCol) buildCol.visible = !buildCol.visible;
    });
    actionCol.addChild(buildBtn);

    const mb = new ButtonClass('manor_main');
    mainBtn = mb.spawn(0, 0, manorTexts['manor_main_btn'], null, toggleStyle);
    mainBtn.hud = true;
    mainBtn.visible = true;
    mainBtn.zIndex = 100;
    mainBtn.onClick(0, () => {
      exitPlacement();
      onBack();
    });
    actionCol.addChild(mainBtn);

    actionCol.setGutter(10);
    actionCol.setPadding(0);
    actionCol.setJustify(LayoutJustify.START);
    actionCol.layout();

    updateToggleButtonStates();
    loading = false;
  }

  /**
   * Re-applies the engine viewport to the canvas's displayed size and
   * re-anchors the on-screen HUD. Called once at init and on window resize.
   * The camera is intentionally NOT re-centered here so a panned position
   * survives a resize (the engine re-clamps it to the board bounds).
   */
  function apply_viewport() {
    if (!canvasEl) return;
    const w = Math.max(1, canvasEl.clientWidth);
    const h = Math.max(1, canvasEl.clientHeight);
    setViewportSize(w, h);
    if (buildCol) {
      buildCol.x = buildCol.getContentWidth() / 2 + 20;
      buildCol.y = buildCol.getContentHeight() / 2 + 20;
      buildCol.layout();
    }
    if (actionCol) {
      actionCol.layout();
      actionCol.x = w - 12 - actionCol.width / 2;
      actionCol.y = 12 + actionCol.height / 2;
      actionCol.layout();
      panelTop = actionCol.y + actionCol.height / 2 + 10;
    }
  }

  async function initialize() {
    const introSeen = await getConfigBoolean('manor_intro_seen', false);
    if (!introSeen) {
      const texts = await loadTexts(['manor_intro']);
      introHtml = texts['manor_intro'] || '';
      if (introHtml) {
        showIntro = true;
        loading = false;
        return;
      }
    }

    loading = true;

    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !$currentCharacter) {
      errorMsg = 'Not authenticated';
      loading = false;
      return;
    }

    // Fetch building configs from server
    try {
      const { configs, build_order } = await getBuildingConfigsRequest({ username: creds.username, token });
      buildingConfigs = configs;
      buildingOrder = build_order;
    } catch (e) {
      errorMsg = 'Failed to load building configs';
      loading = false;
      return;
    }

    // Roads render procedurally (canvas data URLs) until real tile PNGs land,
    // so point the ghost/palette art at a generated tile instead of the PNG path.
    if (buildingConfigs['road']) {
      buildingConfigs['road'] = {
        ...buildingConfigs['road'],
        image: makeRoadTileDataUrl(['n', 'e', 's', 'w']),
        construction_image: makeRoadTileDataUrl(['n', 'e', 's', 'w'])
      };
    }

    await loadFiefdomData();

    if (errorMsg) {
      loading = false;
      return;
    }

    // Auto-place manor house if not already placed (zero cost, construction starts now)
    if (fiefdomData && !fiefdomData.buildings?.some(b => b.name === 'home_base')) {
      try {
        await buildRequest({
          fiefdom_id: fiefdomData.id,
          building_type: 'home_base',
          x: 0, y: 0,
          character_id: $currentCharacter.id
        }, { username: creds.username, token });
        await loadFiefdomData();
      } catch (e) {
        console.log('[ManorMenu] Auto-build manor house failed:', e);
      }
    }

    // home_base is auto-built at start and there can be only one — never buildable.
    // Level-locked buildings stay hidden (they appear when the manor levels up).
    // house is a leftover generic entry with no display_name/image, so the
    // display_name && image guard keeps it (and any placeholder) out of the palette.
    // Order comes from manor_ui.json build_order; ids not listed sort last (stable).
    buildableIds = Object.entries(buildingConfigs)
      .filter(([id, cfg]) => id !== 'home_base' && cfg.display_name && cfg.image
        && fiefdomData && fiefdomData.manor_level >= (cfg.min_manor_level ?? 1))
      .map(([id]) => id)
      .sort((a, b) => {
        const ia = buildingOrder.indexOf(a);
        const ib = buildingOrder.indexOf(b);
        return (ia === -1 ? Number.MAX_SAFE_INTEGER : ia) - (ib === -1 ? Number.MAX_SAFE_INTEGER : ib);
      });

    const textIds = [
      'manor_main_btn', 'ui_manor_build_btn',
      'ui_manor_economy', 'ui_manor_production',
      'ui_manor_stockpiles', 'ui_manor_treasury', 'ui_manor_peasants',
      ...buildableIds.map(id => 'ui_building_' + id)
    ];
    manorTexts = await loadTexts(textIds);

    debugDiv = document.createElement('div');
    initEngine(canvasEl, debugDiv, false, setupGame);
    setBoardSize(BW, BH);
    setBackground(['/images/manor/ground/grass.jpg']);
    setBackgroundMode('tile');
    setBackgroundTileSize(GRASS_TILE, GRASS_TILE);
    setCameraFollowsPlayer(false);
    setBoardPanEnabled(true);

    apply_viewport();
    setCameraPosition(CX, CY);
    window.addEventListener('resize', apply_viewport);
    onKeyDown('Escape', () => exitPlacement());

    everyTick(() => {
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
            ghostTooltip.y = pos.y - cfg.height * CELL_TO_BOARD_UNITS / 2 - 30;
            ghostTooltip.text = result.reason;
            ghostTooltip.opacity = 1;
          }
        } else {
          if (ghostOverlayValid) ghostOverlayValid.visible = true;
          if (ghostOverlayInvalid) ghostOverlayInvalid.visible = false;
          if (ghostTooltip) ghostTooltip.opacity = 0;
        }
      }

      // Detect construction completion → refresh the fiefdom so the server
      // advances construction and the building flips to its built state.
      const now = Date.now() / 1000;
      for (const id of underConstructionSet) {
        const obj = buildingGameObjMap.get(id);
        if (obj && obj.var.construction_duration) {
          const progress = (now - obj.var.construction_start) / obj.var.construction_duration;
          if (progress >= 1 && !completionPollRequested.has(id)) {
            completionPollRequested.add(id);
            pollConstruction();
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
    window.removeEventListener('resize', apply_viewport);
    destroyEngine();
  });
</script>

<div class="manor-container position-relative">
  <canvas bind:this={canvasEl} class="w-100" style="height: 100vh; display: block;"></canvas>

  {#if loading && !showIntro && !errorMsg}
    <div class="position-absolute top-0 start-0 w-100 h-100 d-flex justify-content-center align-items-center">
      <div class="text-center">
        <div class="spinner-border mb-3" role="status">
          <span class="visually-hidden">Loading...</span>
        </div>
        <p class="text-muted">Loading Manor...</p>
        <button class="btn btn-outline-secondary" onclick={onBack}>&larr; Back</button>
      </div>
    </div>
  {:else if showIntro}
    <div class="position-absolute top-0 start-0 w-100 h-100 d-flex justify-content-center align-items-center">
      <div class="container py-5">
        <div class="card">
          <div class="card-body text-center p-5">
            <h3 class="card-title">Your Manor</h3>
            <p class="text-muted mt-3">{introHtml}</p>
            <button class="btn btn-primary mt-4" onclick={dismissIntro}>Begin</button>
          </div>
        </div>
      </div>
    </div>
  {:else if errorMsg}
    <div class="position-absolute top-0 start-0 w-100 h-100 d-flex justify-content-center align-items-center">
      <div class="container py-5">
        <button class="btn btn-outline-secondary mb-4" onclick={onBack}>&larr; Back</button>
        <div class="alert alert-danger">{errorMsg}</div>
      </div>
    </div>
  {/if}

  {#if !loading && !showIntro && !errorMsg}
    {#if showEconomy}
      <div class="card position-absolute end-0 m-3" style="width: 420px; max-height: 80vh; overflow-y: auto; top: {panelTop}px;">
        <div class="card-body">
          <h6 class="card-title">Manor Economy</h6>

          <div class="small mb-2">{manorTexts['ui_manor_peasants']}: {fiefdomData?.peasants ?? 0}</div>
          <div class="mb-3">
            <div class="fw-semibold">{manorTexts['ui_manor_stockpiles']}</div>
            <div class="small">{manorTexts['ui_manor_treasury']}: {formatGold(fiefdomData?.gold ?? 0)}, {fiefdomData?.silver_pence ?? 0} silver pence</div>
            <div class="d-flex flex-wrap gap-1">
              {#each IMPORT_RESOURCES as res}
                {@const amount = ((fiefdomData ?? {}) as unknown as Record<string, number>)[res] ?? 0}
                <span class="badge text-bg-secondary">{RESOURCE_DISPLAY[res] ?? res}: {amount}</span>
              {/each}
            </div>
          </div>

          {#if economyReport}
            <div class="mb-3">
              <div class="fw-semibold">Net gold this period: {economyReport.net_gold.toFixed(2)}</div>
              {#if economyReport.net_silver != null}
                <div class="fw-semibold">Net silver this period: {economyReport.net_silver}d</div>
              {/if}
              {#if Object.keys(economyReport.produced).length > 0}
                <div class="small text-muted">
                  Produced: {Object.entries(economyReport.produced).map(([r, a]) => `${r} ${a.toFixed(1)}`).join(', ')}
                </div>
              {/if}
              {#if Object.keys(economyReport.consumed).length > 0}
                <div class="small text-muted">
                  Consumed: {Object.entries(economyReport.consumed).map(([r, a]) => `${r} ${a.toFixed(1)}`).join(', ')}
                </div>
              {/if}
              {#if Object.keys(economyReport.imported).length > 0}
                <div class="small text-muted">
                  Imported: {Object.entries(economyReport.imported).map(([r, a]) => `${r} ${a.toFixed(1)}`).join(', ')}
                </div>
              {/if}
              {#if Object.keys(economyReport.exported).length > 0}
                <div class="small text-muted">
                  Exported: {Object.entries(economyReport.exported).map(([r, v]) => v.pence != null ? `${r} ${v.amount.toFixed(1)} (${v.pence}d)` : `${r} ${v.amount.toFixed(1)} (${v.gold.toFixed(1)}g)`).join(', ')}
                </div>
              {/if}
            </div>

            {#if economyReport.recommendations && economyReport.recommendations.length > 0}
              <div class="mb-3">
                <div class="fw-semibold mb-1">Advice</div>
                {#each economyReport.recommendations as rec}
                  <div class="small mb-1">
                    <span class="text-warning">&#9654;</span> {rec}
                  </div>
                {/each}
              </div>
            {/if}
          {:else}
            <p class="small text-muted">No economy data yet — check back after resources produce.</p>
          {/if}

          <hr />
          <div class="fw-semibold mb-2">Auto-import (full-buy)</div>
          <div class="d-flex flex-wrap gap-2">
            {#each IMPORT_RESOURCES as resource}
              <button
                class="btn btn-sm {fiefdomData?.import_settings?.[resource] ? 'btn-primary' : 'btn-outline-secondary'}"
                onclick={() => toggleImport(resource)}
              >
                {RESOURCE_DISPLAY[resource] ?? resource}
              </button>
            {/each}
          </div>
          <div class="small text-muted mt-2">
            Full-buy: shortfalls are imported automatically — gold for most resources, silver pence for grain.
          </div>

          <hr />
          <div class="fw-semibold mb-2">Reserves (sell excess above)</div>
          <div class="small text-muted mb-2">Excess stock is sold at each resource's export price (default 50% of import, ironwork 25%) — grain for silver pence, other resources for gold.</div>
          <div class="d-flex flex-wrap gap-2 align-items-center">
            {#each IMPORT_RESOURCES as resource}
              <div class="d-flex align-items-center gap-1">
                <span class="small">{RESOURCE_DISPLAY[resource] ?? resource}:</span>
                <input
                  type="number"
                  min="0"
                  step="1"
                  style="width: 70px;"
                  class="form-control form-control-sm"
                  bind:value={reserveInputs[resource]}
                />
                <button
                  class="btn btn-sm btn-outline-light"
                  onclick={() => setReserve(resource)}
                >
                  Set
                </button>
              </div>
            {/each}
          </div>
          <div class="small text-muted mt-2">
            Excess above each reserve is auto-sold for gold; amounts at or below are kept.
          </div>
        </div>
      </div>
    {/if}

    {#if showProduction}
      <div class="card position-absolute end-0 m-3" style="width: 420px; max-height: 80vh; overflow-y: auto; top: {panelTop}px;">
        <div class="card-body">
          <h6 class="card-title">Production Rates</h6>
          {#each fiefdomData?.buildings ?? [] as building}
            {@const cfg = buildingConfigs[building.name]}
            {@const outputs = (cfg?.outputs ?? []).filter(o => building.level >= (o.min_level ?? 1))}
            {#if outputs.length > 0}
              <div class="mb-3">
                <div class="fw-semibold small">{cfg?.display_name ?? building.name} (L{building.level})</div>
                {#if (fiefdomData?.road_morale?.[building.id] ?? 0) > 0}
                  <div class="small text-success">
                    Road morale: +{Math.round((fiefdomData?.road_morale?.[building.id] ?? 0) * 2)}% production
                  </div>
                {/if}
                {#each outputs as output}
                  <div class="d-flex align-items-center gap-2 mb-1">
                    <span class="small flex-shrink-0" style="width: 110px;">{RESOURCE_DISPLAY[output.resource] ?? output.resource}</span>
                    <input
                      type="range"
                      class="form-range flex-grow-1"
                      min="0"
                      max="100"
                      step="5"
                      value={Math.round(((building.output_rates?.[output.resource] ?? 1) * 100))}
                      onchange={(e) => setOutputRate(building.id, output.resource, Number(e.currentTarget.value) / 100)}
                    />
                    <span class="small flex-shrink-0" style="width: 42px;">{Math.round(((building.output_rates?.[output.resource] ?? 1) * 100))}%</span>
                  </div>
                {/each}
              </div>
            {/if}
          {/each}
          {#if !(fiefdomData?.buildings ?? []).some(b => {
            const cfg = buildingConfigs[b.name];
            return (cfg?.outputs ?? []).some(o => b.level >= (o.min_level ?? 1));
          })}
            <p class="small text-muted">No adjustable outputs on your manor yet.</p>
          {/if}
        </div>
      </div>
    {/if}
  {/if}
</div>
