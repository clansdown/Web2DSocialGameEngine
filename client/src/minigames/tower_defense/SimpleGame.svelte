      <script lang="ts">
  /**
   * Tower Defense game using the simplegame engine.
   * All UI is rendered on-canvas (pure simplegame).
   */
  import { onMount, onDestroy } from 'svelte';
  import { tdRound } from '../../lib/game_state';
  import type { TDRoundKickoffResponse, TDRoundCompleteResponse, SpawnScheduleEntry, EndMiniGameResponse } from '../../lib/api';
  import { setConfig, getConfig } from '../../lib/storage';

  import {
    initEngine, setCameraFollowsPlayer, setBoardSize, setButtonDebugLevel,
    clear, gameObjects, enemies, projectiles,
    boardWidth, boardHeight, getMousePosition, debug,
    everyTick, onMouseClick, whenLoaded, removeEventListeners, afterDraw,
    onPause, onResume, togglePause, isPaused,
    setBackground, setBackgroundMode
  } from '../../../SimpleGame/ui/src/lib/simplegame';

  import {
    GameObjectClass, GameObject, PlayerClass, Player,
    EnemyClass, Enemy, ProjectileClass, Projectile,
    TextClass, Text, createText
  } from '../../../SimpleGame/ui/src/lib/gameclasses';

  import { ButtonClass, Button } from '../../../SimpleGame/ui/src/lib/button';
  import { Column } from '../../../SimpleGame/ui/src/lib/layout';
  // import { Overlay } removed — placement overlay replaced by range indicator
  import type { Position2D } from '../../../SimpleGame/ui/src/lib/util';
  import type { InlineImageMap } from '../../../SimpleGame/ui/src/lib/gameclasses';

  interface Props {
    characterId: number;
    miniGame: string;
    levelId: number;
    onComplete: (results: EndMiniGameResponse) => void;
    onError: (error: string) => void;
  }

  let { characterId, miniGame, levelId, onComplete, onError }: Props = $props();

  interface SavedPlacement {
    configId: string;
    x: number;
    y: number;
    level: number;
    targeting: string;
  }
  interface SavedTDState {
    session_id: number;
    character_id: number;
    level_id: number;
    placements: SavedPlacement[];
  }

  let canvasEl: HTMLCanvasElement;
  let debugEl: HTMLDivElement;
  let loading = $state(true);
  let errorMsg = $state<string | null>(null);

  let tdData: TDRoundKickoffResponse | null = null;

  // Game classes
  let mobEnemyClassMap = new Map<string, EnemyClass>();
  let towerGameClassMap = new Map<string, GameObjectClass>();
  let projectileClasses: Record<string, ProjectileClass> = {};

  // Runtime state
  let activeTowers: { obj: GameObject; configId: string; level: number; piece: any; targeting: string }[] = [];
  let spawnQueue: { enemyId: string; remaining: number; intervalMs: number; initialDelayMs: number; spawnTimer: number; initialDone: boolean }[] = [];
  let allSpawned = false;
  let roundStarted = false;
  let gameState: 'idle' | 'battle' | 'won' | 'lost' = 'idle';
  let currentGold = 100;
  let roundStartGold = 100;
  let currentLives = 1;
  let initialLives = 1;
  let currentRound = 1;
  let totalRounds = 1;
  let roundInTransition = false;
  let nextRoundSpawnSchedule: SpawnScheduleEntry[] | null = null;
  let selectedTower: { obj: GameObject; configId: string } | null = null;
  let placementMode: string | null = null;
  let boardDragActive = false;

  // Drag-to-place state
  let dragUnitId: string | null = null;
  let soldierIds = new Set<string>();
  let btnOrigin = new Map<string, { x: number; y: number }>();
  let unitButtons = new Map<string, Button>();
  let pathMap = new Map<string, any>();
  let intersectionMap = new Map<string, any>();
  let exclusionZones: any[] = [];
  let pathBuffers: { cx: number; cy: number; r: number }[] = [];
  let bw = 1200;
  let bh = 800;
  let sidebarW = 200;
  let mapMetadata: any = null;

  // UI objects
  let resourceText: Text | null = null;
  let livesText: Text | null = null;
  let startButton: Button | null = null;
  let pauseButton: Button | null = null;
  let autoAdvance = false;
  let ringButtons: Button[] = [];
  let showingTargetingPicker = false;

  // Sidebar column for hide/show on drag/place
  let sidebarColumn: Column | null = null;

  // OPFS persistence for unit placements
  let pendingRestore: SavedPlacement[] | null = null;
  let dirty = false;
  let saveInterval: ReturnType<typeof setInterval> | null = null;

  // ============================================================
  // Helpers
  // ============================================================

  function nx(v: number): number { return v * (bw + sidebarW); }
  function ny(v: number): number { return v * bh; }

  function pointInCircle(px: number, py: number, cx: number, cy: number, r: number): boolean {
    const dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r;
  }

  function pointInPolygon(px: number, py: number, verts: Position2D[]): boolean {
    let inside = false;
    for (let i = 0, j = verts.length - 1; i < verts.length; j = i++) {
      const xi = verts[i].x, yi = verts[i].y, xj = verts[j].x, yj = verts[j].y;
      if ((yi > py) !== (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
  }

  function generatePathBuffers(path: any, bufferPx: number): { cx: number; cy: number; r: number }[] {
    const zones: { cx: number; cy: number; r: number }[] = [];
    const wps = path.waypoints || [];
    for (let i = 0; i < wps.length; i++) {
      zones.push({ cx: nx(wps[i].x), cy: ny(wps[i].y), r: bufferPx });
      if (i > 0) {
        const px = nx(wps[i - 1].x), py = ny(wps[i - 1].y);
        const cx = nx(wps[i].x), cy = ny(wps[i].y);
        const segLen = Math.hypot(cx - px, cy - py);
        const steps = Math.ceil(segLen / (bufferPx * 0.6));
        for (let s = 1; s < steps; s++) {
          const t = s / steps;
          zones.push({ cx: px + (cx - px) * t, cy: py + (cy - py) * t, r: bufferPx });
        }
      }
    }
    return zones;
  }

  function isBlocked(x: number, y: number, unitTypeId: string, movingObj?: GameObject): boolean {
    for (const z of exclusionZones) {
      if (z.type === 'circle' && pointInCircle(x, y, nx(z.center_x), ny(z.center_y), z.radius * bw)) return true;
      if (z.type === 'polygon' && z.vertices) {
        const verts = z.vertices.map((v: any) => ({ x: nx(v.x), y: ny(v.y) }));
        if (pointInPolygon(x, y, verts)) return true;
      }
    }
    for (const pb of pathBuffers) {
      if (pointInCircle(x, y, pb.cx, pb.cy, pb.r)) return true;
    }
    const cls = towerGameClassMap.get(unitTypeId);
    const placingR = (cls as any)?.pieceConfig?.exclusion_radius || 0;
    for (const t of activeTowers) {
      if (t.obj === movingObj) continue;
      const tR = t.piece.exclusion_radius || 0;
      const r = Math.max(tR, placingR);
      if (r > 0 && Math.hypot(t.obj.x - x, t.obj.y - y) < r * bw) return true;
    }
    return false;
  }


  function pickBranch(intersection: any): string | null {
    if (!intersection?.branches?.length) return null;
    const totalW = intersection.branches.reduce((s: number, b: any) => s + b.weight, 0);
    let r = Math.random() * totalW;
    for (const b of intersection.branches) {
      r -= b.weight;
      if (r <= 0) return b.path_id;
    }
    return intersection.branches[0]?.path_id || null;
  }

  // ============================================================
  // OPFS persistence
  // ============================================================

  async function savePlacements() {
    if (!dirty || !tdData) return;
    dirty = false;
    try {
      const data: SavedTDState = {
        session_id: (tdData as any).session_id as number,
        character_id: characterId,
        level_id: levelId,
        placements: activeTowers.map(t => ({
          configId: t.configId,
          x: t.obj.x,
          y: t.obj.y,
          level: t.level,
          targeting: t.targeting
        }))
      };
      await setConfig("td_placements", data);
    } catch (e) {
      console.log('[TD] Failed to save placements:', e);
    }
  }

  function restorePlacement(p: SavedPlacement) {
    const cls = towerGameClassMap.get(p.configId);
    if (!cls) return;
    const obj = new GameObject(cls, p.x, p.y);
    obj.direction_x = 0;
    obj.direction_y = 0;
    gameObjects.add(obj);
    cls.gameObjects.add(obj);
    activeTowers.push({
      obj,
      configId: p.configId,
      level: p.level,
      piece: (cls as any).pieceConfig,
      targeting: p.targeting
    });
    if (soldierIds.has(p.configId)) {
      obj.draggable = true;
      obj.dragFollowsCursor = true;
      obj.onDragStart(0, () => {
        (obj as any)._dragOrigX = obj.x;
        (obj as any)._dragOrigY = obj.y;
      });
      obj.onDragEnd(0, () => {
        if (isBlocked(obj.x, obj.y, p.configId, obj)) {
          obj.x = (obj as any)._dragOrigX;
          obj.y = (obj as any)._dragOrigY;
        }
      });
    }
  }

  // ============================================================
    // Game Setup (called by initEngine as the 4th arg)
  // ============================================================

  function setupGame() {
    if (!tdData) {
      onError('Failed to load game session data');
      return;
    }
    const data = tdData;
    setCameraFollowsPlayer(false);

    currentGold = data.gold;
    roundStartGold = data.gold;
    initialLives = data.lives;
    currentLives = data.lives;
    currentRound = (data as any).round_number || 1;
    totalRounds = (data as any).total_rounds || 1;
    mapMetadata = (data as any).map_metadata || undefined;
    exclusionZones = mapMetadata?.exclusion_zones || [];

    // Path buffers
    pathBuffers = [];
    if (mapMetadata?.paths) {
      const bPx = bw * 0.035;
      for (const p of mapMetadata.paths) {
        pathBuffers.push(...generatePathBuffers(p, bPx));
      }
    }
    pathMap = new Map();
    intersectionMap = new Map();
    if (mapMetadata?.paths) for (const p of mapMetadata.paths) pathMap.set(p.id, p);
    if (mapMetadata?.intersections) for (const i of mapMetadata.intersections) intersectionMap.set(i.id, i);

    // Background - map image stretched to fill canvas
    const mapUrl = mapMetadata?.image_filename
      ? `/images/tower_defense/maps/${mapMetadata.image_filename}`
      : null;
    if (mapUrl) {
      setBackground([mapUrl]);
      setBackgroundMode('stretch');
    }

    // Enemy classes
    const mobs = (data as any).mobs?.mobs || {};
    for (const id of Object.keys(mobs)) {
      const m = mobs[id];
      const cls = new EnemyClass(`m_${id}`, m.image_url || null, m.hp || 20);
      cls.setDefaultSpeed(m.speed || 1);
      cls.defaultWidth = m.width || 48;
      cls.defaultHeight = m.height || 48;
      cls.setBoundingBox(cls.defaultWidth, cls.defaultHeight);
      cls.defaultSpriteForwardVector = m.forward_vector || [1, 0];
      (cls as any).mobConfig = m;
      (cls as any).mobId = id;
      mobEnemyClassMap.set(id, cls);
    }

    // Projectile classes from config
    projectileClasses = {};
    const projConfig = (data as any).projectiles?.projectiles || {};
    for (const [id, cfg] of Object.entries(projConfig)) {
      const entry = cfg as any;
      const cls = new ProjectileClass(`proj_${id}`, `/images/tower_defense/projectiles/${entry.image_file}`);
      cls.defaultWidth = entry.width || 16;
      cls.defaultHeight = entry.height || 8;
      cls.setBoundingBox(cls.defaultWidth, cls.defaultHeight);
      cls.defaultSingleCollisionOnly = true;
      cls.defaultSpriteForwardVector = entry.forward_vector || [1, 0];
      (cls as any).config = entry;
      projectileClasses[id] = cls;
    }

    // Tower/Unit classes
    const allPieces = { ...((data as any).towers?.towers || {}), ...((data as any).units?.units || {}) };
    for (const id of Object.keys(allPieces)) {
      const p = allPieces[id];
      const cls = new GameObjectClass(`p_${id}`, p.image_url || null, null);
      (cls as any).pieceConfig = p;
      cls.defaultWidth = p.width || 48;
      cls.defaultHeight = p.height || 48;
      cls.setBoundingBox(cls.defaultWidth, cls.defaultHeight);
      towerGameClassMap.set(id, cls);
    }
    // Track which config IDs are soldiers (from units, not towers)
    for (const id of Object.keys((data as any).units?.units || {})) soldierIds.add(id);

    // Sidebar UI (fixed-width sidebar)
    const sX = bw;
    const sW = sidebarW;
    const cX = sX + sW / 2;

    const copperImages: InlineImageMap = {
      copper: { image: '/images/ui/coin_copper.png', width: 20, height: 20 }
    };
    resourceText = createText(
      `{img:copper} ${currentGold}   Round ${currentRound}/${totalRounds}`,
      { x: cX, y: 35 },
      copperImages
    );
    resourceText.size = 22;
    resourceText.foreground = '#FFD700';
    resourceText.setTextAlign('center');
    resourceText.setShadow('#000000', 4, 2, 2);

    if (currentLives > 1) {
      livesText = createText(`Lives: ${currentLives}`, { x: cX, y: 65 });
      livesText.size = 20;
      livesText.foreground = '#FF6666';
      livesText.setShadow('#000000', 4, 2, 2);
    }

    let bY = 100;
    const sbBtnH = 100;
    const sbBtnW = sW - 20;
    for (const id of Object.keys(allPieces)) {
      const p = allPieces[id];
      const cost = p.cost?.[0]?.gold || 0;
      const label = `${p.name || id} (${cost}g)`;
      const cfgW = p.width || 64;
      const cfgH = p.height || 64;
      const targetArea = 4096;
      const aspect = cfgH / cfgW;
      const iconW = Math.round(Math.sqrt(targetArea / aspect));
      const iconH = Math.round(Math.sqrt(targetArea * aspect));
      const bc = new ButtonClass(`sb_${id}`);
      const btn = bc.spawn(cX, bY, label, p.image_url, {
        width: sbBtnW, height: sbBtnH, color: '#4A4A6A',
        iconWidth: iconW, iconHeight: iconH,
        iconPadding: 10, iconLayout: "above",
        backgroundOpacity: 0.8
      });
      unitButtons.set(id, btn);
      btn.hoverColor = '#5A5A7A';
      btn.clickColor = '#3A3A5A';
      btn.draggable = true;
      btn.dragFollowsCursor = false;
      btn.setOnClick(() => {
        setSelectedTower(null);
        console.log('sidebar click:', id);
        setTimeout(() => { placementMode = id; }, 1);
      });
      btn.onDragStart(0, () => {
        console.log('dragStart:', id, 'at', btn.x, btn.y);
        dragUnitId = id;
        btnOrigin.set(id, { x: btn.x, y: btn.y });
        btn.color = '#3A3A5A';
      });
      btn.onDragEnd(0, () => {
        const orig = btnOrigin.get(id);
        if (orig) { btn.x = orig.x; btn.y = orig.y; }
        btn.color = '#4A4A6A';
        const pos = getMousePosition();
        setSelectedTower(null);
        console.log('dragEnd:', id, 'released at', pos.x, pos.y, 'on board?', pos.x < bw);
        if (pos.x < bw && pos.y >= 0 && pos.y < bh && !isBlocked(pos.x, pos.y, id)) {
          const cfg = allPieces[id];
          const pcost = cfg?.cost?.[0]?.gold || 0;
          if (currentGold >= pcost) tryPlace(pos.x, pos.y, id);
        }
        dragUnitId = null;
      });
      bY += sbBtnH + 10;
    }
    refreshButtonStates();

    const sbc = new ButtonClass('sb_start');
    startButton = sbc.spawn(cX, bY, `Round ${currentRound}/${totalRounds}`, null, { width: sbBtnW, height: 42, color: '#2D7A2D', backgroundOpacity: 0.8 });
    startButton.setOnClick(() => { setSelectedTower(null); startRound(); });
    bY += 50;

    {
      const pauseCls = new ButtonClass('sb_pause');
      pauseButton = pauseCls.spawn(cX, bY, 'Pause', null, { width: sbBtnW, height: 30, color: '#555555', backgroundOpacity: 0.8 });
      pauseButton.setOnClick(() => {
        setSelectedTower(null);
        if (roundStarted && gameState !== 'won' && gameState !== 'lost') togglePause();
      });
      bY += 42;
    }

    const fbc = new ButtonClass('sb_forfeit');
    const fbtn = fbc.spawn(cX, bY, 'Try Again Later', null, { width: sbBtnW, height: 42, color: '#7A2D2D', backgroundOpacity: 0.8 });
    fbtn.setOnClick(() => forfeitGame());
    bY += 50;

    let autoBtn: Button;
    {
      const aaCls = new ButtonClass('sb_auto');
      autoBtn = aaCls.spawn(cX, bY, 'Auto: OFF', null, { width: sbBtnW, height: 30, color: '#555555', backgroundOpacity: 0.8 });
      autoBtn.setOnClick(() => {
        setSelectedTower(null);
        autoAdvance = !autoAdvance;
        autoBtn.text = autoAdvance ? 'Auto: ON' : 'Auto: OFF';
        autoBtn.color = autoAdvance ? '#2D5A2D' : '#555555';
      });
    }

    // Group sidebar buttons in a Column for visibility toggling
    {
      const col = new Column(0, 0);
      for (const [, btn] of unitButtons) col.addChild(btn);
      col.addChild(startButton!);
      col.addChild(pauseButton!);
      col.addChild(fbtn);
      col.addChild(autoBtn);
      sidebarColumn = col;
    }

    createText('───', { x: cX, y: bh - 15 }).foreground = '#444';

    // Spawn queue
    const schedule: SpawnScheduleEntry[] = (data as any).spawn_schedule || [];
    for (const e of schedule) {
      spawnQueue.push({
        enemyId: e.enemy_id, remaining: e.count,
        intervalMs: e.interval_ms, initialDelayMs: e.initial_delay_ms,
        spawnTimer: 0, initialDone: false
      });
    }

    // Game tick (dt is passed at runtime despite the () => void type)
    (everyTick as unknown as (fn: (dt: number) => void) => void)((dt: number) => {
      if (gameState !== 'battle') return;
      spawnTick(dt);
      combatTick(dt);
      checkEnd();
    });

    // Pause lifecycle callbacks
    onPause(() => { if (pauseButton) pauseButton.text = 'Resume'; });
    onResume(() => { if (pauseButton) pauseButton.text = 'Pause'; });

    // Click handler — placement (click-to-place) + tower selection + ring buttons
    onMouseClick(0, (_e, x, y) => {
      if (x >= bw) { console.log('click in sidebar'); return; }
      if (placementMode) {
        console.log('board click with placementMode:', placementMode, 'at', x, y);
        tryPlace(x, y, placementMode);
        placementMode = null;
        return;
      }
      // Ring button clicks are handled by ButtonClass natively
      for (const t of activeTowers) {
        if (Math.hypot(t.obj.x - x, t.obj.y - y) < 28) { setSelectedTower({ obj: t.obj, configId: t.configId }); return; }
      }
      setSelectedTower(null);
    });

    // Ghost + ring + range rendering
    afterDraw((ctx, _offsetX, _offsetY) => {
      // Toggle sidebar visibility when dragging/placing units
      if (sidebarColumn) {
        const shouldHide = !!(placementMode || dragUnitId);
        if (shouldHide !== !sidebarColumn.visible) {
          sidebarColumn.setVisible(!shouldHide);
        }
      }
      const unitId = placementMode ?? dragUnitId;
      if (unitId) {
        const cls = towerGameClassMap.get(unitId);
        const pos = getMousePosition();
        // Ghost sprite
        if (cls?.image?.complete && cls.image.naturalWidth > 0) {
          ctx.save();
          ctx.globalAlpha = 0.5;
          ctx.drawImage(cls.image, pos.x - cls.defaultWidth / 2, pos.y - cls.defaultHeight / 2,
            cls.defaultWidth, cls.defaultHeight);
          ctx.restore();
        }
        // Validity check for range circle
        const cfg = (cls as any).pieceConfig;
        const cost = cfg?.cost?.[0]?.gold || 0;
        const canPlace = !isBlocked(pos.x, pos.y, unitId) && currentGold >= cost;
        // Range circle — blue when valid, red when invalid
        if (cfg) {
          const range = (cfg.range || 0.2) * bw;
          ctx.save();
          ctx.beginPath();
          ctx.arc(pos.x, pos.y, range, 0, Math.PI * 2);
          if (canPlace) {
            ctx.fillStyle = 'rgba(0, 100, 255, 0.08)';
            ctx.strokeStyle = 'rgba(0, 100, 255, 0.5)';
          } else {
            ctx.fillStyle = 'rgba(255, 0, 0, 0.08)';
            ctx.strokeStyle = 'rgba(255, 0, 0, 0.5)';
          }
          ctx.fill();
          ctx.lineWidth = 1.5;
          ctx.stroke();
          ctx.restore();
        }
      }
      // Ring buttons handled by ButtonClass via updateRingButtons()
      // Range circle for dragged placed units
      for (const t of activeTowers) {
        if (t.obj.isDragging && t.obj.draggable) {
          const range = (t.piece.range || 0.2) * bw;
          const canPlace = !isBlocked(t.obj.x, t.obj.y, t.configId, t.obj);
          ctx.save();
          ctx.beginPath();
          ctx.arc(t.obj.x, t.obj.y, range, 0, Math.PI * 2);
          ctx.fillStyle = canPlace ? 'rgba(0, 100, 255, 0.08)' : 'rgba(255, 0, 0, 0.08)';
          ctx.strokeStyle = canPlace ? 'rgba(0, 100, 255, 0.5)' : 'rgba(255, 0, 0, 0.5)';
          ctx.fill();
          ctx.lineWidth = 1.5;
          ctx.stroke();
          ctx.restore();
        }
      }
    });

    // Restore units from OPFS if resuming
    if (pendingRestore) {
      for (const p of pendingRestore) restorePlacement(p);
      pendingRestore = null;
      refreshButtonStates();
    }

    // Periodic save interval
    saveInterval = setInterval(() => savePlacements(), 10000);

    // Loading timeout
    const loadTimer = setTimeout(() => { loading = false; }, 5000);
    whenLoaded(() => { clearTimeout(loadTimer); loading = false; });
  }

  function setSelectedTower(val: { obj: GameObject; configId: string } | null) {
    selectedTower = val;
    showingTargetingPicker = false;
    updateRingButtons();
  }

  function updateRingButtons() {
    for (const b of ringButtons) b.destroy();
    ringButtons = [];
    if (!selectedTower) return;
    const twr = activeTowers.find(t => t.obj === selectedTower!.obj);
    if (!twr) return;

    const btnW = 90, btnH = 42, gap = 8;
    const yOff = twr.obj.y + 38;
    const modes = ['closest', 'first', 'strongest', 'weakest'];
    const upgradeTarget = twr.piece.becomes;
    const targetExists = upgradeTarget ? towerGameClassMap.has(upgradeTarget) : false;
    const modeLabel = twr.targeting || 'closest';

    type BtnSpec = { text: string; color: string; disabled: boolean; action: () => void };
    let specs: BtnSpec[];

    if (showingTargetingPicker) {
      specs = modes.map(m => ({
        text: m.charAt(0).toUpperCase() + m.slice(1),
        color: twr.targeting === m ? '#2A5A3A' : '#3A5A8B',
        disabled: false,
        action: () => { twr.targeting = m; setSelectedTower(null); }
      }));
    } else {
      specs = [
        { text: 'Recall', color: '#8B3A3A', disabled: false, action: doSell },
        { text: modeLabel.charAt(0).toUpperCase() + modeLabel.slice(1), color: '#3A5A8B', disabled: false, action: () => { showingTargetingPicker = true; updateRingButtons(); } }
      ];
      if (targetExists) {
        specs.push({
          text: 'Upgrade', color: '#3A8B3A',
          disabled: currentGold < (twr.piece.upgrade_cost?.[twr.level]?.gold || 0),
          action: doUpgrade
        });
      } else if (!upgradeTarget) {
        specs.push({ text: 'Max Lvl', color: '#555', disabled: true, action: () => {} });
      }
    }

    const totalW = specs.length * btnW + (specs.length - 1) * gap;
    const startX = twr.obj.x - totalW / 2 + btnW / 2;
    for (let i = 0; i < specs.length; i++) {
      const s = specs[i];
      const bc = new ButtonClass(`ring_${i}`);
      const btn = bc.spawn(startX + i * (btnW + gap), yOff, s.text, null, { width: btnW, height: btnH, color: s.color, backgroundOpacity: 0.8 });
      btn.setDisabled(s.disabled);
      btn.setOnClick(s.action);
      ringButtons.push(btn);
    }
  }

  function refreshButtonStates() {
    for (const [id, btn] of unitButtons) {
      const cls = towerGameClassMap.get(id);
      if (!cls) continue;
      const cfg = (cls as any).pieceConfig;
      const cost = cfg?.cost?.[0]?.gold || 0;
      btn.setDisabled(currentGold < cost);
    }
  }

  // ============================================================
  // Placement
  // ============================================================

  function tryPlace(x: number, y: number, unitId: string) {
    console.log('tryPlace:', unitId, 'at', x, y);
    const cls = towerGameClassMap.get(unitId);
    if (!cls) { console.log('tryPlace: no class for', unitId); return; }
    const cfg = (cls as any).pieceConfig;
    const cost = cfg?.cost?.[0]?.gold || 0;
    if (currentGold < cost) { debug('Not enough gold!'); console.log('tryPlace: not enough gold, have', currentGold, 'need', cost); return; }
    if (isBlocked(x, y, unitId)) { debug('Cannot place here!'); console.log('tryPlace: blocked at', x, y); return; }
    const obj = new GameObject(cls, x, y);
    obj.direction_x = 0;
    obj.direction_y = 0;
    gameObjects.add(obj);
    cls.gameObjects.add(obj);
    activeTowers.push({ obj, configId: unitId, level: 0, piece: cfg, targeting: 'first' });
    if (soldierIds.has(unitId)) {
      obj.draggable = true;
      obj.dragFollowsCursor = true;
      obj.onDragStart(0, () => {
        (obj as any)._dragOrigX = obj.x;
        (obj as any)._dragOrigY = obj.y;
      });
      obj.onDragEnd(0, () => {
        if (isBlocked(obj.x, obj.y, unitId, obj)) {
          obj.x = (obj as any)._dragOrigX;
          obj.y = (obj as any)._dragOrigY;
        }
      });
    }
    currentGold -= cost;
    if (resourceText) {
      resourceText.text = `{img:copper} ${currentGold}   Round ${currentRound}/${totalRounds}`;
      resourceText.setInlineImages({
        copper: { image: '/images/ui/coin_copper.png', width: 20, height: 20 }
      });
    }
    refreshButtonStates();
    dirty = true;
    savePlacements();
  }

  function doUpgrade() {
    if (!selectedTower) { debug('No tower selected'); return; }
    const tower = activeTowers.find(t => t.obj === selectedTower!.obj);
    if (!tower) { setSelectedTower(null); return; }
    const piece = tower.piece;
    if (!piece.becomes) { debug('Max level'); return; }
    const upgCost = piece.upgrade_cost?.[tower.level];
    if (!upgCost) { debug('No upgrade available'); return; }
    const goldCost = upgCost.gold || 0;
    if (currentGold < goldCost) { debug('Not enough gold for upgrade!'); return; }

    const newCls = towerGameClassMap.get(piece.becomes);
    if (!newCls) { debug('Upgrade not available'); return; }

    const x = tower.obj.x;
    const y = tower.obj.y;
    tower.obj.destroy();

    const newObj = new GameObject(newCls, x, y);
    newObj.direction_x = 0;
    newObj.direction_y = 0;
    gameObjects.add(newObj);
    newCls.gameObjects.add(newObj);

    if (soldierIds.has(piece.becomes)) {
      newObj.draggable = true;
      newObj.dragFollowsCursor = true;
      newObj.onDragStart(0, () => {
        (newObj as any)._dragOrigX = newObj.x;
        (newObj as any)._dragOrigY = newObj.y;
      });
      newObj.onDragEnd(0, () => {
        if (isBlocked(newObj.x, newObj.y, piece.becomes, newObj)) {
          newObj.x = (newObj as any)._dragOrigX;
          newObj.y = (newObj as any)._dragOrigY;
        }
      });
    }

    const newIdx = activeTowers.indexOf(tower);
    activeTowers[newIdx] = { obj: newObj, configId: piece.becomes, level: tower.level + 1, piece: (newCls as any).pieceConfig, targeting: tower.targeting };
    currentGold -= goldCost;
    if (resourceText) {
      resourceText.text = `{img:copper} ${currentGold}   Round ${currentRound}/${totalRounds}`;
      resourceText.setInlineImages({
        copper: { image: '/images/ui/coin_copper.png', width: 20, height: 20 }
      });
    }
    refreshButtonStates();
    setSelectedTower(null);
    dirty = true;
    savePlacements();
  }

  function doSell() {
    if (!selectedTower) { debug('No tower selected'); return; }
    const towerIdx = activeTowers.findIndex(t => t.obj === selectedTower!.obj);
    if (towerIdx < 0) { setSelectedTower(null); return; }
    const tower = activeTowers[towerIdx];
    const cost = tower.piece.cost?.[0]?.gold || 0;
    const refund = Math.floor(cost * 0.5);
    currentGold += refund;
    if (resourceText) {
      resourceText.text = `{img:copper} ${currentGold}   Round ${currentRound}/${totalRounds}`;
      resourceText.setInlineImages({
        copper: { image: '/images/ui/coin_copper.png', width: 20, height: 20 }
      });
    }
    refreshButtonStates();
    tower.obj.destroy();
    activeTowers.splice(towerIdx, 1);
    setSelectedTower(null);
    dirty = true;
    savePlacements();
  }

  // ============================================================
  // Round
  // ============================================================

  function startRound() {
    if (roundStarted) return;
    roundStarted = true;
    gameState = 'battle';
    if (startButton) startButton.text = 'In Progress...';
  }

  function spawnTick(dt: number) {
    if (allSpawned) return;
    let anyLeft = false;
    for (const sq of spawnQueue) {
      if (sq.remaining <= 0) continue;
      anyLeft = true;
      if (!sq.initialDone) {
        sq.spawnTimer += dt * 1000;
        if (sq.spawnTimer >= sq.initialDelayMs) {
          sq.initialDone = true;
          sq.spawnTimer = 0;
          doSpawn(sq.enemyId);
          sq.remaining--;
        }
        continue;
      }
      sq.spawnTimer += dt * 1000;
      if (sq.spawnTimer >= sq.intervalMs) {
        sq.spawnTimer = 0;
        doSpawn(sq.enemyId);
        sq.remaining--;
      }
    }
    if (!anyLeft) allSpawned = true;
  }

  function doSpawn(enemyId: string) {
    const cls = mobEnemyClassMap.get(enemyId);
    if (!cls || !mapMetadata?.spawn_points?.length) return;
    const sp = mapMetadata.spawn_points[0];
    const e = cls.spawn(nx(sp.x), ny(sp.y));
    (e as any).enemyId = enemyId;
    (e as any).hp = cls.defaultHitpoints;

    const p = pathMap.get(sp.target_path_id);
    if (!p?.waypoints?.length) {
      console.log(`[TD] doSpawn: no waypoints for path ${sp.target_path_id}`);
      return;
    }
    const mobCfg = (cls as any).mobConfig as any;
    const spd = mobCfg?.speed || 1.0;
    const wps = p.waypoints;
    const startIdx = (wps.length > 1
      && Math.abs(wps[0].x - sp.x) < 0.0001
      && Math.abs(wps[0].y - sp.y) < 0.0001) ? 1 : 0;
    (e as any).waypointIndex = startIdx;
    (e as any).pathId = sp.target_path_id;
    e.decelerationDistance = 0;
    // Enable sprite mirroring based on movement direction (spriteForwardVector inherited from class default)
    e.mirrorOnDirection = true;

    e.onArrival(() => {
      const idx = (e as any).waypointIndex as number;
      const pid = (e as any).pathId as string;
      const wpList = pathMap.get(pid)?.waypoints;
      if (!wpList) return;
      const nextIdx = idx + 1;
      (e as any).waypointIndex = nextIdx;
      if (nextIdx >= wpList.length) {
        console.log(`[TD] onArrival: ${(e as any).enemyId} reached end of path, escaping`);
        enemyEscaped(e);
        return;
      }
      const nw = wpList[nextIdx];
      const mId = (e as any).enemyId as string;
      const mCls = mId ? mobEnemyClassMap.get(mId) : null;
      const s2 = mCls ? ((mCls as any).mobConfig?.speed || 1.0) : 1.0;
      const dx = nx(nw.x) - e.x;
      const dy = ny(nw.y) - e.y;
      const dist = Math.hypot(dx, dy);
      e.moveTo({ x: nx(nw.x), y: ny(nw.y) }, dist / (s2 * 60));
    });

    if (wps[startIdx]) {
      const dx = nx(wps[startIdx].x) - e.x;
      const dy = ny(wps[startIdx].y) - e.y;
      const dist = Math.hypot(dx, dy);
      e.moveTo({ x: nx(wps[startIdx].x), y: ny(wps[startIdx].y) }, dist / (spd * 60));
    }
    console.log(`[TD] doSpawn: ${enemyId} at (${nx(sp.x).toFixed(0)}, ${ny(sp.y).toFixed(0)}) hp=${cls.defaultHitpoints} spd=${spd} waypoints=${wps.length} startIdx=${startIdx}`);
    e.logMovement();
  }

  function enemyEscaped(e: Enemy) {
    currentLives--;
    console.log(`[TD] enemyEscaped: ${(e as any).enemyId} at (${e.x.toFixed(0)}, ${e.y.toFixed(0)}) lives=${currentLives}`);
    if (livesText) livesText.text = `Lives: ${currentLives}`;
    enemies.delete(e);
    e.destroy();
    if (currentLives <= 0) { gameState = 'lost'; endGame(); }
  }

  function getProjectileType(configId: string): string | null {
    const cls = towerGameClassMap.get(configId);
    const pid = (cls as any)?.pieceConfig?.projectile_id;
    if (pid) return pid;
    if (['shortbow_archer', 'single_archer_tower', 'three_archer_tower'].includes(configId)) return 'hunting_arrow';
    if (['longbow_archer', 'ballista'].includes(configId)) return 'war_arrow';
    return null;
  }

  function predictIntercept(shooter: { x: number; y: number }, target: Enemy, projectileSpeed: number): { x: number; y: number } {
    const dx = target.x - shooter.x;
    const dy = target.y - shooter.y;
    const dist = Math.hypot(dx, dy);
    const travelTime = dist / projectileSpeed;
    return {
      x: target.x + target.direction_x * target.velocity * travelTime,
      y: target.y + target.direction_y * target.velocity * travelTime,
    };
  }

  function combatTick(dt: number) {
    for (const t of activeTowers) {
      const range = (t.piece.range || 0.2) * bw;
      const rate = t.piece.attack_rate || 1.0;
      const tier = Math.min(t.level, (t.piece.damage?.length || 1) - 1);
      const dmg = t.piece.damage?.[tier] || 10;
      let target: Enemy | null = null;
      const targeting = (t as any).targeting || 'closest';
      if (targeting === 'closest' || targeting === 'first') {
        let best = targeting === 'closest' ? range : -1;
        for (const e of enemies) {
          const d = Math.hypot(e.x - t.obj.x, e.y - t.obj.y);
          if (d > range) continue;
          if (targeting === 'closest') {
            if (d < best) { best = d; target = e; }
          } else {
            if ((e as any).timeExistedMillis > best) { best = (e as any).timeExistedMillis; target = e; }
          }
        }
      } else {
        let bestVal = targeting === 'strongest' ? -1 : Infinity;
        for (const e of enemies) {
          const d = Math.hypot(e.x - t.obj.x, e.y - t.obj.y);
          if (d > range) continue;
          const hp = (e as any).hp || 0;
          if (targeting === 'strongest' ? hp > bestVal : hp < bestVal) { bestVal = hp; target = e; }
        }
      }
      if (!target) continue;
      (t as any).atkTimer = ((t as any).atkTimer || 0) + dt;
      if ((t as any).atkTimer >= 1.0 / rate) {
        (t as any).atkTimer = 0;
          const projType = getProjectileType(t.configId);
          if (projType) {
            const pClass = projectileClasses[projType];
            if (pClass) {
              const p = pClass.spawn(t.obj.x, t.obj.y);
              (p as any).dmg = dmg;
              const cfg = (pClass as any).config as any;
              if (cfg.orbit_radius) {
                // Orbital melee swing (swordsman, etc.)
                (p as any).isOrbital = true;
                p.singleCollisionOnly = false;
                (p as any).hitEnemies = new Set();
                p.alignToTravel = false;
                const angleDeg = Math.atan2(target.y - t.obj.y, target.x - t.obj.x) * (180 / Math.PI);
                const arcDeg = cfg.arc_degrees || 90;
                p.circleAround({
                  center: t.obj,
                  radius: cfg.orbit_radius,
                  velocity: cfg.speed || 800,
                  startAngleDeg: angleDeg - arcDeg / 2,
                  arcDeg: arcDeg,
                  facing: { x: 0, y: 1 },
                  fadeInTime: (cfg.fade_in_ms || 15) / 1000,
                  fadeOutTime: (cfg.fade_out_ms || 15) / 1000,
                  onComplete: () => p.destroy(),
                });
                p.onCollisionWithEnemy((enemy: Enemy) => {
                  const hitSet = (p as any).hitEnemies as Set<Enemy>;
                  if (hitSet.has(enemy)) return;
                  hitSet.add(enemy);
                  hitEnemy(enemy, dmg);
                });
              } else {
                (p as any).trg = target;
                (p as any).aoe = (t.piece.area_of_effect?.radius || 0) * bw;
                p.setSpeed(cfg.speed || 1600);
                p.mirrorOnDirection = true;
                p.setOrientationTowards(predictIntercept(t.obj, target, cfg.speed || 1600));
                p.onCollisionWithEnemy((enemy: Enemy) => {
                  const hitDmg = (p as any).dmg as number || 10;
                  const hitAoe = (p as any).aoe as number || 0;
                  console.log(`[TD] projHit: ${t.configId} → ${(enemy as any).enemyId} dmg=${hitDmg} dist=${Math.hypot(enemy.x - p.x, enemy.y - p.y).toFixed(1)}`);
                  if (hitAoe > 0) {
                    for (const e of enemies) {
                      if (Math.hypot(e.x - enemy.x, e.y - enemy.y) <= hitAoe) hitEnemy(e, hitDmg);
                    }
                  } else {
                    hitEnemy(enemy, hitDmg);
                  }
                  p.destroy();
                });
              }
            }
        } else {
          const aoe = (t.piece.area_of_effect?.radius || 0) * bw;
          if (aoe > 0) {
            for (const e of enemies) {
              if (Math.abs(e.x - target.x) <= aoe && Math.abs(e.y - target.y) <= aoe) hitEnemy(e, dmg);
            }
          } else {
            hitEnemy(target, dmg);
          }
        }
      }
    }
  }

  function hitEnemy(e: Enemy, dmg: number) {
    const before = (e as any).hp;
    (e as any).hp = ((e as any).hp || 0) - dmg;
    console.log(`[TD] hitEnemy: ${(e as any).enemyId} ${before} -> ${(e as any).hp} dmg=${dmg}`);
    if ((e as any).hp <= 0) {
      const mobId = (e as any).enemyId;
      const mc = mobId ? (mobEnemyClassMap.get(mobId) as any)?.mobConfig : null;
      currentGold += mc?.reward_gold || 1;
      if (resourceText) {
        resourceText.text = `{img:copper} ${currentGold}   Round ${currentRound}/${totalRounds}`;
        resourceText.setInlineImages({
          copper: { image: '/images/ui/coin_copper.png', width: 20, height: 20 }
        });
      }
      refreshButtonStates();
      console.log(`[TD] hitEnemy: ${mobId} killed, reward=${mc?.reward_gold || 1}`);
      for (const spawnId of (mc?.spawn_on_death || [])) doSpawn(spawnId);
      enemies.delete(e);
      e.destroy();
    }
  }

  function checkEnd() {
    if (allSpawned && enemies.size === 0 && currentLives > 0 && !roundInTransition) {
      if (currentRound < totalRounds) {
        advanceRound();
      } else {
        gameState = 'won';
        endGame();
      }
    }
  }

  async function advanceRound() {
    roundInTransition = true;
    gameState = 'idle';
    try {
      const result = await tdRound(characterId, {
        session_id: (tdData as any).session_id as number,
        lives_lost: Math.max(0, initialLives - currentLives),
        gold_earned: Math.max(0, currentGold - roundStartGold)
      });
      const ar = result as any;
      console.log('[TD] advanceRound response:', ar);
      if (ar.next_round) {
        currentRound = ar.round_number;
        totalRounds = ar.total_rounds;
        // Clear old enemies and projectiles
        for (const e of enemies) { enemies.delete(e); e.destroy(); }
        for (const p of projectiles) { p.destroy(); }
        // Reset spawn queue with new schedule
        spawnQueue = [];
        const schedule: SpawnScheduleEntry[] = ar.spawn_schedule || [];
        for (const e of schedule) {
          spawnQueue.push({
            enemyId: e.enemy_id, remaining: e.count,
            intervalMs: e.interval_ms, initialDelayMs: e.initial_delay_ms,
            spawnTimer: 0, initialDone: false
          });
        }
        allSpawned = false;
        roundStarted = false;
        // Update gold/lives from server response
        if (ar.gold != null) { currentGold = ar.gold; roundStartGold = ar.gold; }
        if (ar.lives != null) currentLives = ar.lives;
        refreshButtonStates();
        // Update UI
        if (resourceText) {
          resourceText.text = `{img:copper} ${currentGold}   Round ${currentRound}/${totalRounds}`;
          resourceText.setInlineImages({
            copper: { image: '/images/ui/coin_copper.png', width: 20, height: 20 }
          });
        }
        if (livesText) livesText.text = `Lives: ${currentLives}`;
        if (startButton) {
          if (autoAdvance) {
            startButton.text = 'Auto...';
            setTimeout(() => startRound(), 2000);
          } else {
            startButton.text = `Round ${currentRound}/${totalRounds}`;
            startButton.color = '#2D5A2D';
          }
        }
      } else {
        // No next_round — server already ended the game
        console.log('[TD] advanceRound: no next_round, ending game');
        gameState = ar.won ? 'won' : 'lost';
        placementMode = null;
        boardDragActive = false;
        dragUnitId = null;
        setSelectedTower(null);
        onComplete({
          completed: ar.completed || false,
          score: ar.score || 0,
          new_best_score: ar.new_best_score || false,
          times_played: ar.times_played || 0,
          all_levels_done: ar.all_levels_done || false,
          base_unlocked: ar.base_unlocked || false,
          game_phase: ar.game_phase || 'initial_mission',
          next_level_id: null,
          rewards: ar.rewards || {},
          land_patent_earned: ar.land_patent_earned || false,
          duke_right_earned: ar.duke_right_earned || false
        });
      }
    } catch (e) {
      onError(e instanceof Error ? e.message : 'Failed to advance round');
    } finally {
      roundInTransition = false;
    }
  }

  async function endGame(forceLoss = false) {
    placementMode = null;
    boardDragActive = false;
    dragUnitId = null;
    selectedTower = null;
    const score = currentGold + currentLives * 10;

    try {
      const result = await tdRound(characterId, {
        session_id: (tdData as any).session_id as number,
        lives_lost: forceLoss ? 100 : Math.max(0, initialLives - currentLives),
        gold_earned: Math.max(0, currentGold - roundStartGold)
      });
      const cr = result as TDRoundCompleteResponse;
      onComplete({
        completed: cr.completed,
        score: cr.score,
        new_best_score: cr.new_best_score,
        times_played: cr.times_played,
        all_levels_done: cr.all_levels_done,
        base_unlocked: cr.base_unlocked || false,
        game_phase: cr.game_phase || 'initial_mission',
        next_level_id: null,
        rewards: cr.rewards || {},
        land_patent_earned: cr.land_patent_earned || false,
        duke_right_earned: cr.duke_right_earned || false
      });
    } catch (e) {
      onError(e instanceof Error ? e.message : 'Failed to end game');
    }
  }

  function forfeitGame() {
    if (gameState === 'won' || gameState === 'lost') return;
    if (!roundStarted && currentRound === 1) {
      gameState = 'lost';
      placementMode = null;
      boardDragActive = false;
      dragUnitId = null;
      selectedTower = null;
      onComplete({ completed: false, score: 0, new_best_score: 0, times_played: 0, all_levels_done: false, base_unlocked: false, game_phase: 'initial_mission', next_level_id: null, rewards: {}, land_patent_earned: false, duke_right_earned: false });
      return;
    }
    gameState = 'lost';
    endGame(true);
  }

  // ============================================================
  // Init
  // ============================================================

  async function init() {
    try {
      const result = await tdRound(characterId, { mini_game: miniGame, level_id: levelId });
      tdData = result as TDRoundKickoffResponse;

      // Load saved placements from OPFS on resume
      if ((tdData as any).resumed) {
        try {
          const saved = await getConfig<SavedTDState>("td_placements");
          if (saved && saved.session_id === (tdData as any).session_id && saved.character_id === characterId) {
            pendingRestore = saved.placements;
          }
        } catch (e) {
          console.log('[TD] Failed to load saved placements:', e);
        }
      }

      // Load map image to determine its aspect ratio
      let mapAspect = 16 / 9;
      const mapFilename = (tdData as any).map_metadata?.image_filename as string | undefined;
      if (mapFilename) {
        const img = new Image();
        img.src = `/images/tower_defense/maps/${mapFilename}`;
        await img.decode();
        mapAspect = img.naturalWidth / img.naturalHeight;
      }

      // Size board to viewport, preserving map aspect ratio
      let boardW = window.innerHeight * mapAspect;
      let boardH = window.innerHeight;
      if (boardW + sidebarW > window.innerWidth) {
        boardW = window.innerWidth - sidebarW;
        boardH = boardW / mapAspect;
      }
      bw = Math.floor(boardW);
      bh = Math.floor(boardH);
      canvasEl.width = bw + sidebarW;
      canvasEl.height = bh;
      canvasEl.style.width = `${canvasEl.width}px`;
      canvasEl.style.height = `${canvasEl.height}px`;
      setBoardSize(bw + sidebarW, bh);
      initEngine(canvasEl, debugEl, false, setupGame);

      // Native listeners for board click-and-drag placement
      const onMouseDown = (e: MouseEvent) => {
        if (placementMode && e.button === 0) {
          const bx = (bw + sidebarW) * (e.clientX / canvasEl.clientWidth);
          console.log('native mousedown: placementMode=', placementMode, 'bx=', bx, 'bw=', bw);
          if (bx < bw) boardDragActive = true;
        }
      };
      const onMouseUp = (e: MouseEvent) => {
        if (e.button !== 0) return;
        boardDragActive = false;
        if (placementMode) {
          const bx = (bw + sidebarW) * (e.clientX / canvasEl.clientWidth);
          const by = bh * (e.clientY / canvasEl.clientHeight);
          console.log('native mouseup: placing', placementMode, 'at', bx, by);
          if (bx < bw && by >= 0 && by < bh && !isBlocked(bx, by, placementMode!)) {
            const cls = towerGameClassMap.get(placementMode);
            const cfg = cls ? (cls as any).pieceConfig : null;
            const pcost = cfg?.cost?.[0]?.gold || 0;
          if (currentGold >= pcost) tryPlace(bx, by, placementMode);
        }
        placementMode = null;
      }
      };
      canvasEl.addEventListener('mousedown', onMouseDown);
      canvasEl.addEventListener('mouseup', onMouseUp);
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : 'Failed to start TD';
      loading = false;
      onError(errorMsg);
    }
  }

  onMount(() => { init(); });

  onDestroy(() => {
    if (saveInterval) clearInterval(saveInterval);
    savePlacements();
    try { removeEventListeners(); clear(); } catch (_e) { /* ignore */ }
  });
</script>

  <div style="width: 100%; height: 100vh; position: relative; display: flex; justify-content: center; align-items: center; background: #000;">
  {#if loading}
    <div class="d-flex justify-content-center align-items-center position-absolute top-0 start-0 w-100 h-100" style="z-index: 10; background: rgba(0,0,0,0.7);">
      <div class="text-center">
        <div class="spinner-border mb-3 text-light" role="status">
          <span class="visually-hidden">Loading...</span>
        </div>
        <p class="text-light">Loading Tower Defense...</p>
      </div>
    </div>
  {/if}
  {#if errorMsg}
    <div class="position-absolute top-0 start-0 w-100 p-3" style="z-index: 11;">
      <div class="alert alert-danger">{errorMsg}</div>
    </div>
  {/if}
  <canvas
    bind:this={canvasEl}
    tabindex="0"
    style="display: block;"
  ></canvas>
  <div bind:this={debugEl} style="display: none;"></div>
</div>
