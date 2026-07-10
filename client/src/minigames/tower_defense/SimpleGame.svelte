      <script lang="ts">
  /**
   * Tower Defense game using the simplegame engine.
   * All UI is rendered on-canvas (pure simplegame).
   */
  import { onMount, onDestroy } from 'svelte';
  import { tdRound } from '../../lib/game_state';
  import type { TDRoundKickoffResponse, TDRoundCompleteResponse, SpawnScheduleEntry, EndMiniGameResponse } from '../../lib/api';

  import {
    initEngine, setCameraFollowsPlayer, setBoardSize, setButtonDebugLevel,
    clear, gameObjects, enemies, projectiles,
    boardWidth, boardHeight, getMousePosition, debug,
    everyTick, onMouseClick, whenLoaded, removeEventListeners, afterDraw,
    onPause, onResume, togglePause, isPaused
  } from '../../../SimpleGame/ui/src/lib/simplegame';

  import {
    GameObjectClass, GameObject, PlayerClass, Player,
    EnemyClass, Enemy, ProjectileClass, Projectile,
    TextClass, Text, createText
  } from '../../../SimpleGame/ui/src/lib/gameclasses';

  import { ButtonClass, Button } from '../../../SimpleGame/ui/src/lib/button';
  import { Overlay } from '../../../SimpleGame/ui/src/lib/overlay';
  import type { Position2D } from '../../../SimpleGame/ui/src/lib/util';

  interface Props {
    characterId: number;
    miniGame: string;
    levelId: number;
    onComplete: (results: EndMiniGameResponse) => void;
    onError: (error: string) => void;
  }

  let { characterId, miniGame, levelId, onComplete, onError }: Props = $props();

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
  let activeTowers: { obj: GameObject; configId: string; level: number; piece: any }[] = [];
  let spawnQueue: { enemyId: string; remaining: number; intervalMs: number; initialDelayMs: number; spawnTimer: number; initialDone: boolean }[] = [];
  let allSpawned = false;
  let roundStarted = false;
  let gameState: 'idle' | 'battle' | 'won' | 'lost' = 'idle';
  let currentGold = 100;
  let currentLives = 20;
  let selectedTower: { obj: GameObject; configId: string } | null = null;
  let placementMode: string | null = null;
  let boardDragActive = false;

  // Drag-to-place state
  let dragUnitId: string | null = null;
  let showOverlay = false;
  let placementOverlay: Overlay | null = null;
  let btnOrigin = new Map<string, { x: number; y: number }>();
  let pathMap = new Map<string, any>();
  let intersectionMap = new Map<string, any>();
  let exclusionZones: any[] = [];
  let pathBuffers: { cx: number; cy: number; r: number }[] = [];
  let bw = 1200;
  let bh = 800;
  let sidebarW = 200;
  let mapMetadata: any = null;

  // UI objects
  let goldText: Text | null = null;
  let livesText: Text | null = null;
  let startButton: Button | null = null;
  let pauseButton: Button | null = null;
  let autoAdvance = false;

  // ============================================================
  // Helpers
  // ============================================================

  function nx(v: number): number { return v * bw; }
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

  function isBlocked(x: number, y: number): boolean {
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
    return false;
  }

  function buildPlacementOverlay() {
    const ov = new Overlay(
      [{ x: 0, y: 0 }, { x: bw, y: 0 }, { x: bw, y: bh }, { x: 0, y: bh }],
      'rgba(0, 180, 0, 0.18)'
    );
    for (const z of exclusionZones) {
      if (z.type === 'circle') ov.addCircleCutout(nx(z.center_x), ny(z.center_y), z.radius * bw, 24);
      else if (z.type === 'polygon' && z.vertices) {
        ov.addCutout(z.vertices.map((v: any) => ({ x: nx(v.x), y: ny(v.y) })));
      }
    }
    for (const pb of pathBuffers) ov.addCircleCutout(pb.cx, pb.cy, pb.r, 16);
    for (const t of activeTowers) ov.addCircleCutout(t.obj.x, t.obj.y, 20, 16);
    placementOverlay = ov;
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
    // Game Setup (called by initEngine as the 4th arg)
  // ============================================================

  function setupGame() {
    if (!tdData) {
      onError('Failed to load game session data');
      return;
    }
    const data = tdData;
    setCameraFollowsPlayer(false);

    // bw/bh already set by init() — don't read boardWidth which is now bw+sidebarW
    currentGold = data.gold;
    currentLives = data.lives;
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

    // Background - map as GameObject (no tiling), aspect-ratio-preserving
    const mapUrl = mapMetadata?.image_filename
      ? `/images/tower_defense/maps/${mapMetadata.image_filename}`
      : null;
    const bgCls = new GameObjectClass('_map_bg', mapUrl, null);
    const bgObj = new GameObject(bgCls, 0, 0);
    bgObj.width = 0;
    bgObj.height = 0;
    gameObjects.add(bgObj);
    whenLoaded(() => {
      if (bgCls.image?.naturalWidth > 0 && bgCls.image?.naturalHeight > 0) {
        const sc = Math.min(bw / bgCls.image.naturalWidth, bh / bgCls.image.naturalHeight);
        bgObj.width = Math.floor(bgCls.image.naturalWidth * sc);
        bgObj.height = Math.floor(bgCls.image.naturalHeight * sc);
        bgObj.x = bw / 2;
        bgObj.y = bh / 2;
      }
    });

    // Enemy classes
    const mobs = (data as any).mobs?.mobs || {};
    for (const id of Object.keys(mobs)) {
      const m = mobs[id];
      const cls = new EnemyClass(`m_${id}`, m.image_url || null, m.hp || 20);
      cls.setDefaultSpeed(m.speed || 1);
      cls.defaultWidth = m.width || 48;
      cls.defaultHeight = m.height || 48;
      cls.setBoundingBox(cls.defaultWidth, cls.defaultHeight);
      (cls as any).mobConfig = m;
      (cls as any).mobId = id;
      mobEnemyClassMap.set(id, cls);
    }

    // Projectile classes
    projectileClasses = {};
    const pHunting = new ProjectileClass('proj_hunting', '/images/tower_defense/projectiles/hunting_arrow.png');
    pHunting.setBoundingBox(16, 6);
    projectileClasses.hunting_arrow = pHunting;
    const pWar = new ProjectileClass('proj_war', '/images/tower_defense/projectiles/war_arrow.png');
    pWar.setBoundingBox(20, 8);
    projectileClasses.war_arrow = pWar;

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

    // Sidebar UI (fixed-width sidebar)
    const sX = bw;
    const sW = sidebarW;
    const cX = sX + sW / 2;

    goldText = createText(`Gold: ${currentGold}`, { x: cX, y: 35 });
    goldText.size = 22;
    goldText.foreground = '#FFD700';

    livesText = createText(`Lives: ${currentLives}`, { x: cX, y: 65 });
    livesText.size = 20;
    livesText.foreground = '#FF6666';

    let bY = 100;
    for (const id of Object.keys(allPieces)) {
      const p = allPieces[id];
      const cost = p.cost?.[0]?.gold || 0;
      const label = `${p.name || id} (${cost}g)`;
      const bc = new ButtonClass(`sb_${id}`);
      const btn = bc.spawn(cX, bY, label, sW - 20, 34, undefined, '#4A4A6A', p.image_url);
      btn.iconSize = 24;
      btn.iconPadding = 10;
      btn.hoverColor = '#5A5A7A';
      btn.clickColor = '#3A3A5A';
      btn.draggable = true;
      btn.dragFollowsCursor = false;
      btn.setOnClick(() => {
        console.log('sidebar click:', id);
        placementMode = id;
        buildPlacementOverlay();
        showOverlay = true;
      });
      btn.onDragStart(0, () => {
        console.log('dragStart:', id, 'at', btn.x, btn.y);
        dragUnitId = id;
        btnOrigin.set(id, { x: btn.x, y: btn.y });
        btn.color = '#3A3A5A';
        buildPlacementOverlay();
        showOverlay = true;
      });
      btn.onDragEnd(0, () => {
        const orig = btnOrigin.get(id);
        if (orig) { btn.x = orig.x; btn.y = orig.y; }
        btn.color = '#4A4A6A';
        showOverlay = false;
        placementOverlay = null;
        const pos = getMousePosition();
        console.log('dragEnd:', id, 'released at', pos.x, pos.y, 'on board?', pos.x < bw);
        if (pos.x < bw && pos.y >= 0 && pos.y < bh && !isBlocked(pos.x, pos.y)) {
          const cfg = allPieces[id];
          const pcost = cfg?.cost?.[0]?.gold || 0;
          if (currentGold >= pcost) tryPlace(pos.x, pos.y, id);
        }
        dragUnitId = null;
      });
      bY += 40;
    }

    const sbc = new ButtonClass('sb_start');
    startButton = sbc.spawn(cX, bY, 'Start Round', sW - 20, 42);
    startButton.color = '#2D7A2D';
    startButton.setOnClick(() => startRound());
    bY += 50;

    {
      const pauseCls = new ButtonClass('sb_pause');
      pauseButton = pauseCls.spawn(cX, bY, 'Pause', sW - 20, 30);
      pauseButton.color = '#555555';
      pauseButton.setOnClick(() => {
        if (roundStarted && gameState !== 'won' && gameState !== 'lost') togglePause();
      });
      bY += 42;
    }

    const fbc = new ButtonClass('sb_forfeit');
    const fbtn = fbc.spawn(cX, bY, 'Try Again Later', sW - 20, 42);
    fbtn.color = '#7A2D2D';
    fbtn.setOnClick(() => forfeitGame());
    bY += 50;

    {
      const upgCls = new ButtonClass('sb_upgrade');
      const upgBtn = upgCls.spawn(cX, bY, 'Upgrade', sW - 20, 34);
      upgBtn.color = '#3D6A3D';
      upgBtn.setOnClick(() => doUpgrade());
      bY += 38;
    }
    {
      const sellCls = new ButtonClass('sb_sell');
      const sellBtn = sellCls.spawn(cX, bY, 'Sell (50%)', sW - 20, 34);
      sellBtn.color = '#6A3D3D';
      sellBtn.setOnClick(() => doSell());
      bY += 46;
    }

    {
      const aaCls = new ButtonClass('sb_auto');
      const autoBtn = aaCls.spawn(cX, bY, 'Auto: OFF', sW - 20, 30);
      autoBtn.color = '#555555';
      autoBtn.setOnClick(() => {
        autoAdvance = !autoAdvance;
        autoBtn.text = autoAdvance ? 'Auto: ON' : 'Auto: OFF';
        autoBtn.color = autoAdvance ? '#2D5A2D' : '#555555';
      });
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
      projTick(dt);
      checkEnd();
    });

    // Pause lifecycle callbacks
    onPause(() => { if (pauseButton) pauseButton.text = 'Resume'; });
    onResume(() => { if (pauseButton) pauseButton.text = 'Pause'; });

    // Click handler — placement (click-to-place) + tower selection
    onMouseClick(0, (_e, x, y) => {
      if (x >= bw) { console.log('click in sidebar'); return; }
      if (placementMode) {
        console.log('board click with placementMode:', placementMode, 'at', x, y);
        tryPlace(x, y, placementMode);
        placementMode = null;
        showOverlay = false;
        placementOverlay = null;
        return;
      }
      for (const t of activeTowers) {
        if (Math.hypot(t.obj.x - x, t.obj.y - y) < 28) { selectedTower = { obj: t.obj, configId: t.configId }; return; }
      }
      selectedTower = null;
    });

    // Ghost + overlay rendering
    afterDraw((ctx, _offsetX, _offsetY) => {
      const unitId = placementMode ?? dragUnitId;
      if (unitId) {
        const cls = towerGameClassMap.get(unitId);
        if (showOverlay && placementOverlay) placementOverlay.draw(ctx, 0, 0);
        // Ghost sprite
        if (cls?.image?.complete && cls.image.naturalWidth > 0) {
          const pos = getMousePosition();
          ctx.save();
          ctx.globalAlpha = 0.5;
          ctx.drawImage(cls.image, pos.x - cls.defaultWidth / 2, pos.y - cls.defaultHeight / 2,
            cls.defaultWidth, cls.defaultHeight);
          ctx.restore();
        }
        // Range circle
        if (cls) {
          const cfg = (cls as any).pieceConfig;
          if (cfg) {
            const range = (cfg.range || 0.2) * bw;
            const pos = getMousePosition();
            ctx.save();
            ctx.beginPath();
            ctx.arc(pos.x, pos.y, range, 0, Math.PI * 2);
            ctx.fillStyle = 'rgba(0, 100, 255, 0.08)';
            ctx.fill();
            ctx.strokeStyle = 'rgba(0, 100, 255, 0.5)';
            ctx.lineWidth = 1.5;
            ctx.stroke();
            ctx.restore();
          }
        }
      }
    });

    // Loading timeout
    const loadTimer = setTimeout(() => { loading = false; }, 5000);
    whenLoaded(() => { clearTimeout(loadTimer); loading = false; });
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
    if (isBlocked(x, y)) { debug('Cannot place here!'); console.log('tryPlace: blocked at', x, y); return; }
    const obj = new GameObject(cls, x, y);
    gameObjects.add(obj);
    cls.gameObjects.add(obj);
    activeTowers.push({ obj, configId: unitId, level: 0, piece: cfg });
    currentGold -= cost;
    if (goldText) goldText.text = `Gold: ${currentGold}`;
  }

  function doUpgrade() {
    if (!selectedTower) { debug('No tower selected'); return; }
    const tower = activeTowers.find(t => t.obj === selectedTower!.obj);
    if (!tower) { selectedTower = null; return; }
    const piece = tower.piece;
    if (!piece.becomes) { debug('Max level'); return; }
    const upgCost = piece.upgrade_cost?.[tower.level];
    if (!upgCost) { debug('No upgrade available'); return; }
    const goldCost = upgCost.gold || 0;
    if (currentGold < goldCost) { debug('Not enough gold for upgrade!'); return; }

    const x = tower.obj.x;
    const y = tower.obj.y;
    tower.obj.destroy();

    const newCls = towerGameClassMap.get(piece.becomes);
    if (!newCls) return;
    const newObj = new GameObject(newCls, x, y);
    gameObjects.add(newObj);
    newCls.gameObjects.add(newObj);

    const newIdx = activeTowers.indexOf(tower);
    activeTowers[newIdx] = { obj: newObj, configId: piece.becomes, level: tower.level + 1, piece: (newCls as any).pieceConfig };
    currentGold -= goldCost;
    if (goldText) goldText.text = `Gold: ${currentGold}`;
    selectedTower = { obj: newObj, configId: piece.becomes };
  }

  function doSell() {
    if (!selectedTower) { debug('No tower selected'); return; }
    const towerIdx = activeTowers.findIndex(t => t.obj === selectedTower!.obj);
    if (towerIdx < 0) { selectedTower = null; return; }
    const tower = activeTowers[towerIdx];
    const cost = tower.piece.cost?.[0]?.gold || 0;
    const refund = Math.floor(cost * 0.5);
    currentGold += refund;
    if (goldText) goldText.text = `Gold: ${currentGold}`;
    tower.obj.destroy();
    activeTowers.splice(towerIdx, 1);
    selectedTower = null;
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
      e.moveTo({ x: nx(nw.x), y: ny(nw.y) }, 1.0 / s2);
    });

    if (wps[startIdx]) {
      e.moveTo({ x: nx(wps[startIdx].x), y: ny(wps[startIdx].y) }, 1.0 / spd);
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
    if (['shortbow_archer', 'single_archer_tower', 'three_archer_tower'].includes(configId)) return 'hunting_arrow';
    if (['longbow_archer', 'ballista'].includes(configId)) return 'war_arrow';
    return null;
  }

  function combatTick(dt: number) {
    for (const t of activeTowers) {
      const range = (t.piece.range || 0.2) * bw;
      const rate = t.piece.attack_rate || 1.0;
      const tier = Math.min(t.level, (t.piece.damage?.length || 1) - 1);
      const dmg = t.piece.damage?.[tier] || 10;
      let target: Enemy | null = null;
      let bestD = range;
      for (const e of enemies) {
        const d = Math.hypot(e.x - t.obj.x, e.y - t.obj.y);
        if (d <= bestD) { bestD = d; target = e; }
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
            (p as any).trg = target;
            (p as any).dmg = dmg;
            (p as any).aoe = (t.piece.area_of_effect?.radius || 0) * bw;
            p.setSpeed(400);
            const dx = target.x - t.obj.x;
            const dy = target.y - t.obj.y;
            p.orientation = Math.atan2(dy, dx) + Math.PI;
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

  function projTick(dt: number) {
    for (const p of projectiles) {
      const trg = (p as any).trg as Enemy | undefined;
      if (!trg || (trg as any).hp <= 0) { p.destroy(); continue; }
      const dx = trg.x - p.x, dy = trg.y - p.y;
      const dist = Math.hypot(dx, dy);
      if (dist < 12) {
        const dmg = (p as any).dmg || 10;
        const aoe = (p as any).aoe || 0;
        if (aoe > 0) {
          for (const e of enemies) {
            if (Math.hypot(e.x - trg.x, e.y - trg.y) <= aoe) hitEnemy(e, dmg);
          }
        } else {
          hitEnemy(trg, dmg);
        }
        p.destroy();
      } else {
        const spd = p.speed || 400;
        const amt = spd * dt;
        p.x += (dx / dist) * amt;
        p.y += (dy / dist) * amt;
        const ndx = trg.x - p.x;
        const ndy = trg.y - p.y;
        p.orientation = Math.atan2(ndy, ndx) + Math.PI;
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
      if (goldText) goldText.text = `Gold: ${currentGold}`;
      console.log(`[TD] hitEnemy: ${mobId} killed, reward=${mc?.reward_gold || 1}`);
      for (const spawnId of (mc?.spawn_on_death || [])) doSpawn(spawnId);
      enemies.delete(e);
      e.destroy();
    }
  }

  function checkEnd() {
    if (allSpawned && enemies.size === 0 && currentLives > 0) {
      gameState = 'won';
      endGame();
    }
  }

  async function endGame() {
    placementMode = null;
    boardDragActive = false;
    dragUnitId = null;
    showOverlay = false;
    placementOverlay = null;
    const score = currentGold + currentLives * 10;

    try {
      const result = await tdRound(characterId, {
        session_id: (tdData as any).session_id as number,
        lives_lost: Math.max(0, 20 - currentLives),
        gold_earned: Math.max(0, currentGold - 100)
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
    if (!roundStarted) {
      gameState = 'lost';
      placementMode = null;
      boardDragActive = false;
      dragUnitId = null;
      showOverlay = false;
      placementOverlay = null;
      onComplete({ completed: false, score: 0, new_best_score: false, times_played: 0, all_levels_done: false, base_unlocked: false, game_phase: 'initial_mission', next_level_id: null, rewards: {}, land_patent_earned: false, duke_right_earned: false });
      return;
    }
    gameState = 'lost';
    endGame();
  }

  // ============================================================
  // Init
  // ============================================================

  async function init() {
    try {
      const result = await tdRound(characterId, { mini_game: miniGame, level_id: levelId });
      tdData = result as TDRoundKickoffResponse;
      const ratio = 16 / 9;
      let boardW = window.innerHeight * ratio;
      let boardH = window.innerHeight;
      if (boardW + sidebarW > window.innerWidth) {
        boardW = window.innerWidth - sidebarW;
        boardH = boardW / ratio;
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
          if (bx < bw && by >= 0 && by < bh && !isBlocked(bx, by)) {
            const cls = towerGameClassMap.get(placementMode);
            const cfg = cls ? (cls as any).pieceConfig : null;
            const pcost = cfg?.cost?.[0]?.gold || 0;
            if (currentGold >= pcost) tryPlace(bx, by, placementMode);
          }
          placementMode = null;
          showOverlay = false;
          placementOverlay = null;
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
