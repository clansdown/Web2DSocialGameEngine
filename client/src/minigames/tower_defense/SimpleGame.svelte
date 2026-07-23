      <script lang="ts">
  /**
   * Tower Defense game using the simplegame engine.
   * All UI is rendered on-canvas (pure simplegame).
   */
  import { onMount, onDestroy } from 'svelte';
  import { tdRound } from '../../lib/game_state';
  import type { TDRoundKickoffResponse, TDRoundCompleteResponse, SpawnScheduleEntry, EndMiniGameResponse, NewUnlocks } from '../../lib/api';
  import { setConfig, getConfig, deleteConfig } from '../../lib/storage';
  import type {
    EnemyVar, ProjectileVar, PoolObjVar, DragObjVar,
    MobConfig, ProjectileConfig, PieceConfig, AreaEffectConfig,
    KickoffResponse, AdvanceResponse
  } from '../../lib/tower_defense_types';
  import { loadMap } from '../../lib/tower_defense_map';
  import type { BoardMapData, BoardMapExclusionZone } from '../../lib/tower_defense_map';

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
    onComplete: (results: EndMiniGameResponse, forfeited?: boolean) => void;
    onError: (error: string) => void;
  }

  let { characterId, miniGame, levelId, onComplete, onError }: Props = $props();

  interface SavedPlacement {
    configId: string;
    x: number;
    y: number;
    level: number;
    targeting: string;
    flaskId?: string;
  }
  interface SavedTDState {
    session_id: number;
    character_id: number;
    level_id: number;
    gold?: number;
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

  // Typed config lookups (replaces (cls as any).pieceConfig etc.)
  let pieceConfigs = new Map<string, PieceConfig>();
  let mobConfigs = new Map<string, MobConfig>();
  let projectileConfigs = new Map<string, ProjectileConfig>();

  // Runtime state
  interface PlayerCombatant {
    obj: GameObject;
    configId: string;
    level: number;
    piece: any;
    targeting: string;
    flaskId?: string;
    atkTimer?: number;
    attackPeriod: number;
    /** Offset from visual center to logical position (bottom-center = height/2) */
    originY: number;
    /** Projectile spawn offset from visual center (from config) */
    projOrigin: { x: number; y: number };
    _pathSegCache?: { seg: { ax: number; ay: number; bx: number; by: number }; dist: number; t0: number; t1: number }[];
    selectableTarget?: { x: number; y: number };
  }

  let activeCombatants: PlayerCombatant[] = [];
  let spawnQueue: { enemyId: string; remaining: number; intervalMs: number; initialDelayMs: number; spawnTimer: number; initialDone: boolean; spawnPointId?: string }[] = [];
  let allSpawned = false;
  let roundStarted = false;
  let gameState: 'idle' | 'battle' | 'won' | 'lost' = 'idle';
  let currentGold = 100;
  let currentLives = 1;
  let initialLives = 1;
  let currentRound = 1;
  let totalRounds = 1;
  let roundInTransition = false;
  let nextRoundSpawnSchedule: SpawnScheduleEntry[] | null = null;
  let leakedEnemies: Record<string, number> = {};
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
  let pathDepthCache: Map<string, number> = new Map();
  let exclusionZones: BoardMapExclusionZone[] = [];
  let pathBuffers: { cx: number; cy: number; r: number }[] = [];
  let mapMetadata: BoardMapData | null = null;

  // UI objects
  let resourceText: Text | null = null;
  let livesText: Text | null = null;
  let rpButton: Button | null = null;
  let fbtn: any = null;
  let ringButtons: Button[] = [];
  let showingTargetingPicker = false;

  let resizeHandler: (() => void) | null = null;

  // Ground effect pools
  let groundPools: {
    id: string; obj: GameObject;
    timer: number; opacity: number;
    slowFactor: number; damagePerSecond: number;
    damageType: string | null; radius: number;
  }[] = [];
  let poolClasses: Record<string, GameObjectClass> = {};

  // Path segments for nearest_path targeting
  let pathSegments: { ax: number; ay: number; bx: number; by: number }[] = [];
  let showingFlaskPicker = false;

  // Sidebar column for hide/show on drag/place
  let towerColumn: Column | null = null;
  let unitColumn: Column | null = null;

  function setSidebarVisible(visible: boolean) {
    if (towerColumn) towerColumn.setVisible(visible);
    if (unitColumn) unitColumn.setVisible(visible);
    if (fbtn) fbtn.visible = visible;
  }

  // OPFS persistence for unit placements
  let pendingRestore: SavedPlacement[] | null = null;
  let pendingGold: number | null = null;
  let dirty = false;
  let saveInterval: ReturnType<typeof setInterval> | null = null;

  // ============================================================
  // Constants
  // ============================================================

  /** Board width in board units */
  const BW = 1400;
  /** Board height in board units */
  const BH = 780;
  /** Sidebar overlay width in board units. Sidebar overlays the right edge of the board at x ∈ [BW - SIDEBAR_W, BW]. */
  const SIDEBAR_W = 420;

  // ============================================================
  // Helpers
  // ============================================================

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

  function generatePathBuffers(path: any, bufferR: number): { cx: number; cy: number; r: number }[] {
    const zones: { cx: number; cy: number; r: number }[] = [];
    const wps = path.waypoints || [];
    for (let i = 0; i < wps.length; i++) {
      zones.push({ cx: wps[i].x, cy: wps[i].y, r: bufferR });
      if (i > 0) {
        const px = wps[i - 1].x, py = wps[i - 1].y;
        const cx = wps[i].x, cy = wps[i].y;
        const segLen = Math.hypot(cx - px, cy - py);
        const steps = Math.ceil(segLen / (bufferR * 0.6));
        for (let s = 1; s < steps; s++) {
          const t = s / steps;
          zones.push({ cx: px + (cx - px) * t, cy: py + (cy - py) * t, r: bufferR });
        }
      }
    }
    return zones;
  }

  function isBlocked(x: number, y: number, unitTypeId: string, movingObj?: GameObject): boolean {
    for (const z of exclusionZones) {
      if (z.type === 'circle' && pointInCircle(x, y, z.centerX, z.centerY, z.radius)) return true;
      if (z.type === 'polygon' && z.vertices) {
        if (pointInPolygon(x, y, z.vertices as Position2D[])) return true;
      }
    }
    const placingR = pieceConfigs.get(unitTypeId)?.exclusion_radius || 0;
    for (const pb of pathBuffers) {
      const effectiveR = Math.max(pb.r, placingR / 2);
      if (pointInCircle(x, y, pb.cx, pb.cy, effectiveR)) return true;
    }
    for (const t of activeCombatants) {
      if (t.obj === movingObj) continue;
      const tR = t.piece.exclusion_radius || 0;
      const r = Math.max(tR, placingR);
      if (r > 0 && Math.hypot(t.obj.x - x, t.obj.y + t.originY - y) < r) return true;
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

  function closestPointOnSegment(px: number, py: number, ax: number, ay: number, bx: number, by: number): { x: number; y: number } {
    const dx = bx - ax, dy = by - ay;
    const lenSq = dx * dx + dy * dy;
    if (lenSq === 0) return { x: ax, y: ay };
    let t = ((px - ax) * dx + (py - ay) * dy) / lenSq;
    t = Math.max(0, Math.min(1, t));
    return { x: ax + t * dx, y: ay + t * dy };
  }

  function findNearestPathSegInfo(px: number, py: number): { seg: { ax: number; ay: number; bx: number; by: number }; point: { x: number; y: number }; dist: number } | null {
    let best: any = null;
    let bestDist = Infinity;
    for (const seg of pathSegments) {
      const pt = closestPointOnSegment(px, py, seg.ax, seg.ay, seg.bx, seg.by);
      const d = Math.hypot(pt.x - px, pt.y - py);
      if (d < bestDist) {
        bestDist = d;
        best = { seg, point: pt, dist: d };
      }
    }
    return best;
  }

  /**
   * Returns the [t0, t1] parameter range of a segment that falls within
   * a circle of given radius centered at (cx, cy). t ∈ [0,1] along the
   * segment. Returns null if no portion is inside the circle.
   */
  function segmentCircleRange(
    ax: number, ay: number, bx: number, by: number,
    cx: number, cy: number, radius: number
  ): [number, number] | null {
    const vx = bx - ax, vy = by - ay;
    const wx = ax - cx, wy = ay - cy;
    const a = vx * vx + vy * vy;
    const b = 2 * (wx * vx + wy * vy);
    const c = wx * wx + wy * wy - radius * radius;
    if (a === 0) {
      // Degenerate segment (point)
      return c <= 0 ? [0, 1] : null;
    }
    const disc = b * b - 4 * a * c;
    if (disc < 0) return null;
    const sqrtDisc = Math.sqrt(disc);
    let t0 = (-b - sqrtDisc) / (2 * a);
    let t1 = (-b + sqrtDisc) / (2 * a);
    t0 = Math.max(0, t0);
    t1 = Math.min(1, t1);
    return t0 <= t1 ? [t0, t1] : null;
  }

  function pick_weighted_branch(branches: { pathId: string; weight: number }[]): { pathId: string; weight: number } {
    const total = branches.reduce((s, b) => s + b.weight, 0);
    let roll = Math.random() * total;
    for (const b of branches) {
      roll -= b.weight;
      if (roll <= 0) return b;
    }
    return branches[branches.length - 1];
  }

  function find_next_waypoint(wps: { x: number; y: number }[], startIdx: number, cx: number, cy: number, delta: number = 5): number {
    for (let i = startIdx; i < wps.length; i++) {
      if (Math.hypot(wps[i].x - cx, wps[i].y - cy) > delta) return i;
    }
    return wps.length;
  }

  function cachePathSegTargets(t: PlayerCombatant, range: number) {
    const cache: { seg: { ax: number; ay: number; bx: number; by: number }; dist: number; t0: number; t1: number }[] = [];
    const cx = t.obj.x, cy = t.obj.y + t.originY;
    for (const seg of pathSegments) {
      const tr = segmentCircleRange(seg.ax, seg.ay, seg.bx, seg.by, cx, cy, range);
      if (!tr) continue;
      const pt = closestPointOnSegment(cx, cy, seg.ax, seg.ay, seg.bx, seg.by);
      const dist = Math.hypot(pt.x - cx, pt.y - cy);
      cache.push({ seg, dist, t0: tr[0], t1: tr[1] });
    }
    t._pathSegCache = cache;
  }

  function getWeightedPathTarget(t: PlayerCombatant): { x: number; y: number } | null {
    const cache = t._pathSegCache;
    if (!cache || cache.length === 0) {
      // Fallback: nearest point on any path
      const info = findNearestPathSegInfo(t.obj.x, t.obj.y + t.originY);
      return info ? info.point : null;
    }
    let totalWeight = 0;
    const weights: number[] = [];
    for (const entry of cache) {
      const w = 1 / (entry.dist + 1);
      weights.push(w);
      totalWeight += w;
    }
    let r = Math.random() * totalWeight;
    for (let i = 0; i < cache.length; i++) {
      r -= weights[i];
      if (r <= 0) {
        const entry = cache[i];
        const tParam = entry.t0 + Math.random() * (entry.t1 - entry.t0);
        return {
          x: entry.seg.ax + tParam * (entry.seg.bx - entry.seg.ax),
          y: entry.seg.ay + tParam * (entry.seg.by - entry.seg.ay)
        };
      }
    }
    // Fallback (shouldn't reach here)
    const last = cache[cache.length - 1];
    const tParam = last.t0 + Math.random() * (last.t1 - last.t0);
    return {
      x: last.seg.ax + tParam * (last.seg.bx - last.seg.ax),
      y: last.seg.ay + tParam * (last.seg.by - last.seg.ay)
    };
  }

  function spawnGroundPool(x: number, y: number, aeConfig: AreaEffectConfig, _targetX: number, _targetY: number) {
    const radiusPx = aeConfig.radius;  // area_effect.radius in board units
    const clsName = `pool_${aeConfig.id}`;
    let cls = poolClasses[clsName];
    if (!cls) {
      cls = new GameObjectClass(clsName, `/images/tower_defense/projectiles/${aeConfig.image_file}`, null);
      cls.defaultWidth = radiusPx * 2;
      cls.defaultHeight = radiusPx * 2;
      cls.setBoundingBox(cls.defaultWidth, cls.defaultHeight);
      poolClasses[clsName] = cls;
    }
    const seg = findNearestPathSegInfo(x, y);
    let angle = 0;
    if (seg) {
      angle = Math.atan2(seg.seg.by - seg.seg.ay, seg.seg.bx - seg.seg.ax) * (180 / Math.PI);
    }
    const obj = new GameObject(cls, x, y);
    obj.setOrientation(angle);
    obj.opacity = aeConfig.opacity || 1.0;
    if (aeConfig.z_index != null) obj.zIndex = aeConfig.z_index;
    (obj.var as PoolObjVar).poolData = {
      id: aeConfig.id,
      slowFactor: aeConfig.slow_factor || 0,
      damagePerSecond: aeConfig.damage_per_second || 0,
    };
    (obj.var as PoolObjVar).damageMap = new Map();
    // Instance-level collision — fires each frame an enemy overlaps the pool's hitbox
    obj.onCollisionWith(EnemyClass.rootEnemyClass, (_enemy: GameObject) => {
      const pd = (obj.var as PoolObjVar).poolData;
      const ev = _enemy.var as EnemyVar;
      if (pd.slowFactor > ev.slowFactor) {
        ev.slowFactor = pd.slowFactor;
      }
      if (pd.damagePerSecond > 0) {
        const map = (obj.var as PoolObjVar).damageMap;
        const now = obj.timeExistedMillis;
        const lastTime = map.get(_enemy) ?? (now - 50);
        const elapsed = now - lastTime;
        if (elapsed >= 50) {
          const dmg = pd.damagePerSecond * (elapsed / 1000);
          hitEnemy(_enemy, dmg);
          map.set(_enemy, now);
        }
      }
    });
    gameObjects.add(obj);
    cls.gameObjects.add(obj);
    groundPools.push({
      id: aeConfig.id, obj,
      timer: aeConfig.duration_ms || 0,
      opacity: aeConfig.opacity || 1.0,
      slowFactor: aeConfig.slow_factor || 0,
      damagePerSecond: aeConfig.damage_per_second || 0,
      damageType: aeConfig.damage_type || null,
      radius: radiusPx,
    });
  }

  function poolTick(dt: number) {
    for (let i = groundPools.length - 1; i >= 0; i--) {
      const pool = groundPools[i];
      pool.timer -= dt * 1000;
      if (pool.timer <= 0) {
        pool.obj.destroy();
        groundPools.splice(i, 1);
        continue;
      }
      if (pool.timer < 500) {
        pool.obj.opacity = (pool.timer / 500) * pool.opacity;
      }
    }
    for (const e of enemies) {
      const ev = e.var as EnemyVar;
      const sf = ev.slowFactor || 0;
      if (ev.baseVelocity != null) {
        e.velocity = sf > 0 ? ev.baseVelocity * (1 - sf) : ev.baseVelocity;
      }
      ev.slowFactor = 0;
    }
  }

  // ============================================================
  // OPFS persistence
  // ============================================================

  async function savePlacements() {
    if (!dirty || !tdData) return;
    dirty = false;
    try {
      const data: SavedTDState = {
        session_id: (tdData as unknown as KickoffResponse).session_id,
        character_id: characterId,
        level_id: levelId,
        gold: currentGold,
        placements: activeCombatants.map(t => ({
          configId: t.configId,
          x: t.obj.x,
          y: t.obj.y,
          level: t.level,
          targeting: t.targeting,
          flaskId: t.flaskId
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
    const piece = pieceConfigs.get(p.configId);
    if (!piece) return;
    const targeting = p.targeting || piece.default_targeting || 'first';
    const flaskId = p.flaskId || (piece.projectile_ids?.includes('naptha_flask') ? 'naptha_flask' : piece.projectile_ids?.[0]);
    const originY = (piece.height || 48) / 2;
    const projOrigin: { x: number; y: number } = (piece as any).projectile_origin || { x: 0, y: 0 };
    activeCombatants.push({
      obj,
      configId: p.configId,
      level: p.level,
      piece,
      targeting,
      flaskId,
      originY,
      projOrigin,
      attackPeriod: 1.0 / (piece.attack_rate || 1.0)
    });
    if (soldierIds.has(p.configId)) {
      const towerEntry = activeCombatants[activeCombatants.length - 1];
      obj.draggable = true;
      obj.dragFollowsCursor = true;
      obj.onDragStart(0, () => {
        (obj.var as DragObjVar)._dragOrigX = obj.x;
        (obj.var as DragObjVar)._dragOrigY = obj.y;
      });
      obj.onDragEnd(0, () => {
        if (isBlocked(obj.x, obj.y + towerEntry.originY, p.configId, obj)) {
          obj.x = (obj.var as DragObjVar)._dragOrigX;
          obj.y = (obj.var as DragObjVar)._dragOrigY;
        }
        cachePathSegTargets(towerEntry, towerEntry.piece.range || 0.2);
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
    const data = tdData as unknown as KickoffResponse;
    setCameraFollowsPlayer(false);

    currentGold = data.gold;
    initialLives = data.lives;
    currentLives = data.lives;
    currentRound = data.round_number || 1;
    totalRounds = data.total_rounds || 1;
    mapMetadata = loadMap(data.map_metadata, BW, BH);
    exclusionZones = mapMetadata.exclusionZones;

    // Path buffers — buffer radius as fraction of board width
    pathBuffers = [];
    if (mapMetadata.paths.length > 0) {
      const bufferR = 30;  // path collision buffer in board units
      for (const p of mapMetadata.paths) {
        pathBuffers.push(...generatePathBuffers(p, bufferR));
      }
    }
    pathMap = new Map();
    intersectionMap = new Map();
    for (const p of mapMetadata.paths) pathMap.set(p.id, p);
    for (const i of mapMetadata.intersections) intersectionMap.set(i.id, i);
    pathSegments = [];
    for (const p of mapMetadata.paths) {
      const wps = p.waypoints || [];
      for (let i = 0; i < wps.length - 1; i++) {
        pathSegments.push({
          ax: wps[i].x, ay: wps[i].y,
          bx: wps[i + 1].x, by: wps[i + 1].y,
        });
      }
    }

    // Build path depth cache (BFS backward from endpoint paths)
    // Higher depth = closer to the map exit
    {
      const reverseAdj = new Map<string, string[]>();
      for (const p of mapMetadata.paths) {
        if (!p.endAtIntersectionId) continue;
        const ix = intersectionMap.get(p.endAtIntersectionId);
        if (!ix) continue;
        for (const br of ix.branches) {
          const list = reverseAdj.get(br.pathId);
          if (list) { list.push(p.id); }
          else { reverseAdj.set(br.pathId, [p.id]); }
        }
      }
      const depths = new Map<string, number>();
      const childCount = new Map<string, number>();
      const maxChildDepth = new Map<string, number>();
      const queue: string[] = [];
      for (const p of mapMetadata.paths) {
        if (p.endAtEndPointId) {
          depths.set(p.id, 0);
          queue.push(p.id);
        }
      }
      for (const p of mapMetadata.paths) {
        if (!p.endAtIntersectionId) continue;
        const ix = intersectionMap.get(p.endAtIntersectionId);
        childCount.set(p.id, ix?.branches.length || 0);
        maxChildDepth.set(p.id, -1);
      }
      while (queue.length > 0) {
        const childPid = queue.shift()!;
        const childDepth = depths.get(childPid)!;
        const parents = reverseAdj.get(childPid);
        if (!parents) continue;
        for (const parentPid of parents) {
          const curMax = maxChildDepth.get(parentPid) ?? -1;
          if (childDepth > curMax) maxChildDepth.set(parentPid, childDepth);
          const remaining = (childCount.get(parentPid) ?? 1) - 1;
          childCount.set(parentPid, remaining);
          if (remaining === 0) {
            const parentDepth = maxChildDepth.get(parentPid)! + 1;
            depths.set(parentPid, parentDepth);
            queue.push(parentPid);
          }
        }
      }
      pathDepthCache = depths;
    }

    // Background - map image stretched to fill board
    const mapUrl = mapMetadata.imageFilename
      ? `/images/tower_defense/maps/${mapMetadata.imageFilename}`
      : null;
    if (mapUrl) {
      setBackground([mapUrl]);
      setBackgroundMode('stretch');
    }

    // Enemy classes
    const mobs = data.mobs?.mobs || {};
    for (const id of Object.keys(mobs)) {
      const m = mobs[id];
      const cls = new EnemyClass(`m_${id}`, m.image_url || null, m.hp || 20);
      cls.setDefaultSpeed(m.speed || 1);
      cls.defaultWidth = m.width || 48;
      cls.defaultHeight = m.height || 48;
      cls.setBoundingBox(cls.defaultWidth, cls.defaultHeight);
      cls.defaultSpriteForwardVector = m.forward_vector || [1, 0];
      mobConfigs.set(id, m);
      mobEnemyClassMap.set(id, cls);
    }

    // Projectile classes from config
    projectileClasses = {};
    const projConfig = data.projectiles?.projectiles || {};
    for (const [id, cfg] of Object.entries(projConfig)) {
      const entry = cfg as any;
      const cls = new ProjectileClass(`proj_${id}`, `/images/tower_defense/projectiles/${entry.image_file}`);
      cls.defaultWidth = entry.width || 16;
      cls.defaultHeight = entry.height || 8;
      cls.setBoundingBox(cls.defaultWidth, cls.defaultHeight);
      cls.defaultSingleCollisionOnly = true;
      cls.defaultSpriteForwardVector = entry.forward_vector || [1, 0];
      projectileConfigs.set(id, entry);
      projectileClasses[id] = cls;
    }

    // Tower/Unit classes
    const allPieces = { ...(data.towers?.towers || {}), ...(data.units?.units || {}) };
    for (const id of Object.keys(allPieces)) {
      const p = allPieces[id];
      const cls = new GameObjectClass(`p_${id}`, p.image_url || null, null);
      pieceConfigs.set(id, p);
      cls.defaultWidth = p.width || 48;
      cls.defaultHeight = p.height || 48;
      cls.setBoundingBox(cls.defaultWidth, cls.defaultHeight);
      towerGameClassMap.set(id, cls);
    }
    // Track which config IDs are soldiers (from units, not towers)
    for (const id of Object.keys(data.units?.units || {})) soldierIds.add(id);

    // Sidebar UI (overlays right edge of board)
    const sX = BW - SIDEBAR_W;
    const sW = SIDEBAR_W;
    const cX = sX + sW / 2;

    const copperImages: InlineImageMap = {
      copper: { image: '/images/ui/coin_copper.png', width: 20, height: 20 }
    };
    resourceText = createText(
      `{img:copper} ${currentGold}`,
      { x: sX + 100, y: 35 },
      copperImages
    );
    resourceText.size = 22;
    resourceText.foreground = '#FFD700';
    resourceText.setTextAlign('center');
    resourceText.setShadow('#000000', 4, 2, 2);

    if (currentLives > 1) {
      livesText = createText(`Lives: ${currentLives}`, { x: sX + 100, y: 65 });
      livesText.size = 20;
      livesText.foreground = '#FF6666';
      livesText.setShadow('#000000', 4, 2, 2);
    }

    let bY = 100;
    const sbBtnH = 130;
    const sbBtnW = 180;
    const cX_tower = sX + 300;
    const cX_unit = sX + 100;

    const tcol = new Column(sX + 300, 0);
    tcol.setGutter(10);
    const ucol = new Column(sX + 100, 0);
    ucol.setGutter(10);

    for (const id of Object.keys(allPieces)) {
      const p = allPieces[id];
      const isUnit = soldierIds.has(id);
      const cX = isUnit ? cX_unit : cX_tower;
      const cost = p.cost?.[0]?.gold || 0;
      const label = `${p.name || id} (${cost}g)`;
      const iconH = Math.round(sbBtnH * 0.6);
      const iconW = Math.round(iconH * (p.width || 64) / (p.height || 64));
      const bc = new ButtonClass(`sb_${id}`);
      const btn = bc.spawn(cX, 0, label, p.image_url, {
        width: sbBtnW, height: sbBtnH, color: '#4A4A6A',
        iconWidth: iconW, iconHeight: iconH,
        iconPadding: 10, iconLayout: "above",
        backgroundOpacity: 0.8
      });
      const col = isUnit ? ucol : tcol;
      col.addChild(btn);
      unitButtons.set(id, btn);
      btn.hoverColor = '#5A5A7A';
      btn.clickColor = '#3A3A5A';
      btn.draggable = true;
      btn.dragFollowsCursor = false;
      btn.zIndex = 100;
      btn.setOnClick(() => {
        setSelectedTower(null);
        console.log('sidebar click:', id);
        setSidebarVisible(false);
        setTimeout(() => { placementMode = id; }, 1);
      });
      btn.onDragStart(0, () => {
        console.log('dragStart:', id, 'at', btn.x, btn.y);
        setSidebarVisible(false);
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
        console.log('dragEnd:', id, 'released at', pos.x, pos.y, 'on board?', pos.x < BW);
        if (pos.x < BW && pos.y >= 0 && pos.y < BH && !isBlocked(pos.x, pos.y, id)) {
          const cfg = allPieces[id];
          const pcost = cfg?.cost?.[0]?.gold || 0;
          if (currentGold >= pcost) tryPlace(pos.x, pos.y, id);
        }
        dragUnitId = null;
        if (!placementMode && !roundStarted) setSidebarVisible(true);
      });
    }
    refreshButtonStates();

    // Combined round/pause button — standalone, not in any column
    const rpCls = new ButtonClass('sb_round');
    rpButton = rpCls.spawn(sX + 320, 35, `Round ${currentRound}/${totalRounds}`, null, { width: 140, height: 38, color: '#2D7A2D', backgroundOpacity: 0.8 });
    rpButton.zIndex = 100;
    rpButton.setOnClick(() => {
      setSelectedTower(null);
      if (roundStarted && gameState !== 'won' && gameState !== 'lost') {
        togglePause();
      } else if (!roundStarted) {
        startRound();
      }
    });

    const fbc = new ButtonClass('sb_forfeit');
    fbtn = fbc.spawn(BW - sbBtnW / 2 - 10, BH - 21 - 10, 'Try Again Later', null, { width: sbBtnW, height: 42, color: '#7A2D2D', backgroundOpacity: 0.8 });
    fbtn.zIndex = 100;
    fbtn.setOnClick(() => forfeitGame());

    // Menu toggle button — standalone, next to rpButton
    const menuBtnCls = new ButtonClass('sb_menutoggle');
    const menuBtn = menuBtnCls.spawn(sX + 200, 35, '☰', null, { width: 30, height: 38, color: '#4A4A6A', backgroundOpacity: 0.8 });
    menuBtn.zIndex = 100;
    menuBtn.setOnClick(() => setSidebarVisible(!towerColumn?.visible));

    // Layout columns and adjust so first child top is at Y=100
    tcol.layout();
    ucol.layout();
    tcol.y = 100 + tcol.height / 2;
    ucol.y = 100 + ucol.height / 2;
    tcol.layout();
    ucol.layout();
    towerColumn = tcol;
    unitColumn = ucol;

    createText('───', { x: cX, y: BH - 15 }).foreground = '#444';

    // Spawn queue
    const schedule = data.spawn_schedule || [];
    for (const e of schedule) {
      spawnQueue.push({
        enemyId: e.enemy_id, remaining: e.count,
        intervalMs: e.interval_ms, initialDelayMs: e.initial_delay_ms,
        spawnTimer: 0, initialDone: false,
        spawnPointId: e.spawn_point_id
      });
    }

    // Game tick (dt is passed at runtime despite the () => void type)
    (everyTick as unknown as (fn: (dt: number) => void) => void)((dt: number) => {
      if (gameState !== 'battle') return;
      spawnTick(dt);
      combatTick(dt);
      poolTick(dt);
      // Proximity fallback for flask projectiles that miss enemies
      for (const p of projectiles) {
        const pv = p.var as ProjectileVar;
        const tX = pv.targetX;
        const tY = pv.targetY;
        const aeCfg = pv.aeCfg;
        if (tX != null && tY != null && aeCfg) {
          if (Math.hypot(p.x - tX, p.y - tY) < 15) {
            spawnGroundPool(p.x, p.y, aeCfg, tX, tY);
            p.destroy();
          }
        }
      }
      checkEnd();
    });

    // Pause lifecycle callbacks
    onPause(() => { if (rpButton) { rpButton.text = 'Resume'; rpButton.color = '#7A7A2D'; } });
    onResume(() => { if (rpButton) { rpButton.text = 'Pause'; rpButton.color = '#555555'; } });

    // Click handler — placement (click-to-place) + tower selection + selectable targeting + ring buttons
    onMouseClick(0, (_e, x, y) => {
      if (x >= BW) { console.log('click in sidebar'); return; }
      if (placementMode) {
        console.log('board click with placementMode:', placementMode, 'at', x, y);
        tryPlace(x, y, placementMode);
        placementMode = null;
        setSidebarVisible(!roundStarted);
        return;
      }
      // Selectable targeting — if a tower with selectable mode is selected, set its target
      if (selectedTower) {
        const twr = activeCombatants.find(t => t.obj === selectedTower!.obj);
        if (twr && twr.targeting === 'selectable') {
          const range = twr.piece.range || 0.2;  // range in board units
          if (Math.hypot(x - twr.obj.x, y - (twr.obj.y + twr.originY)) <= range) {
            twr.selectableTarget = { x, y };
            setSelectedTower(null);
            return;
          }
        }
      }
      // Ring button clicks are handled by ButtonClass natively
      for (const t of activeCombatants) {
        if (Math.hypot(t.obj.x - x, t.obj.y - y) < 28) { setSelectedTower({ obj: t.obj, configId: t.configId }); return; }
      }
      setSelectedTower(null);
    });

    // Ghost + ring + range rendering
    afterDraw((ctx, _offsetX, _offsetY) => {
      const unitId = placementMode ?? dragUnitId;
      if (unitId) {
        const cls = towerGameClassMap.get(unitId);
        const pos = getMousePosition();
        // Ghost sprite
        if (cls?.image?.complete && cls.image.naturalWidth > 0) {
          ctx.save();
          ctx.globalAlpha = 0.5;
          ctx.drawImage(cls.image, pos.x - cls.defaultWidth / 2, pos.y - cls.defaultHeight,
            cls.defaultWidth, cls.defaultHeight);
          ctx.restore();
        }
        // Validity check for range circle
        const cfg = pieceConfigs.get(unitId);
        const cost = cfg?.cost?.[0]?.gold || 0;
        const canPlace = !isBlocked(pos.x, pos.y, unitId) && currentGold >= cost;
        // Range circle — blue when valid, red when invalid
        if (cfg) {
          const range = cfg.range || 0.2;  // range in board units
          ctx.save();
          ctx.beginPath();
          ctx.arc(pos.x, pos.y, range, 0, Math.PI * 2);
          if (canPlace) {
            ctx.fillStyle = 'rgba(0, 100, 255, 0.16)';
            ctx.strokeStyle = 'rgba(0, 100, 255, 1.0)';
          } else {
            ctx.fillStyle = 'rgba(255, 0, 0, 0.16)';
            ctx.strokeStyle = 'rgba(255, 0, 0, 1.0)';
          }
          ctx.fill();
          ctx.lineWidth = 1.5;
          ctx.stroke();
          ctx.restore();
        }
      }
      // Selectable target crosshair for units in selectable mode
      for (const t of activeCombatants) {
        if (t.targeting === 'selectable' && t.selectableTarget) {
          const st = t.selectableTarget;
          ctx.save();
          ctx.strokeStyle = 'rgba(255, 200, 0, 0.6)';
          ctx.lineWidth = 2;
          const cs = 8;
          ctx.beginPath();
          ctx.moveTo(st.x - cs, st.y); ctx.lineTo(st.x + cs, st.y);
          ctx.moveTo(st.x, st.y - cs); ctx.lineTo(st.x, st.y + cs);
          ctx.stroke();
          ctx.beginPath();
          ctx.arc(st.x, st.y, cs * 1.5, 0, Math.PI * 2);
          ctx.stroke();
          ctx.restore();
        }
      }
      // Ring buttons handled by ButtonClass via updateRingButtons()
      // Range circle for dragged placed units
      for (const t of activeCombatants) {
        if (t.obj.isDragging && t.obj.draggable) {
      const range = t.piece.range || 0.2;  // range in board units
      const ly = t.obj.y + t.originY;
          const canPlace = !isBlocked(t.obj.x, ly, t.configId, t.obj);
          ctx.save();
          ctx.beginPath();
          ctx.arc(t.obj.x, ly, range, 0, Math.PI * 2);
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
    if (pendingGold !== null) {
      currentGold = pendingGold;
      pendingGold = null;
      if (resourceText) {
        resourceText.text = `{img:copper} ${currentGold}`;
        resourceText.setInlineImages({
          copper: { image: '/images/ui/coin_copper.png', width: 20, height: 20 }
        });
      }
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
    showingFlaskPicker = false;
    updateRingButtons();
  }

  function updateRingButtons() {
    for (const b of ringButtons) b.destroy();
    ringButtons = [];
    if (!selectedTower) return;
    const twr = activeCombatants.find(t => t.obj === selectedTower!.obj);
    if (!twr) return;

    const btnW = 90, btnH = 42, gap = 8;
    const yOff = twr.obj.y + 38;
    const modes = ['closest', 'first', 'strongest', 'weakest', 'nearest_path', 'selectable'];
    const upgradeTarget = twr.piece.becomes;
    const targetExists = upgradeTarget ? towerGameClassMap.has(upgradeTarget) : false;
    const modeLabel = twr.targeting || 'closest';

    type BtnSpec = { text: string; color: string; disabled: boolean; action: () => void };
    let specs: BtnSpec[];

    if (showingTargetingPicker) {
      specs = modes.map(m => ({
        text: m === 'nearest_path' ? 'Nearest Path' : (m === 'selectable' ? 'Selectable' : m.charAt(0).toUpperCase() + m.slice(1)),
        color: twr.targeting === m ? '#2A5A3A' : '#3A5A8B',
        disabled: false,
        action: () => { twr.targeting = m; setSelectedTower(null); }
      }));
    } else if (showingFlaskPicker && twr.piece.projectile_ids) {
      const flaskIds: string[] = twr.piece.projectile_ids;
      specs = flaskIds.map((fid: string) => {
        const cfg = projectileConfigs.get(fid);
        const label = cfg?.name || fid.charAt(0).toUpperCase() + fid.slice(1);
        return {
          text: label,
          color: twr.flaskId === fid ? '#2A5A3A' : '#5A3A8B',
          disabled: false,
          action: () => { twr.flaskId = fid; dirty = true; savePlacements(); setSelectedTower(null); }
        };
      });
    } else {
      specs = [
        { text: 'Recall', color: '#8B3A3A', disabled: false, action: doSell },
        { text: modeLabel === 'nearest_path' ? 'Nearest Path' : (modeLabel === 'selectable' ? 'Selectable' : modeLabel.charAt(0).toUpperCase() + modeLabel.slice(1)), color: '#3A5A8B', disabled: false, action: () => { showingTargetingPicker = true; updateRingButtons(); } }
      ];
      if (twr.piece.projectile_ids) {
        const fCfg = twr.flaskId ? projectileClasses[twr.flaskId] : null;
        const fLabel = twr.flaskId ? (projectileConfigs.get(twr.flaskId)?.name || twr.flaskId) : 'Flask';
        specs.push({
          text: 'Flask: ' + fLabel, color: '#5A3A8B',
          disabled: false,
          action: () => { showingFlaskPicker = true; updateRingButtons(); }
        });
      }
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
      const cfg = pieceConfigs.get(id);
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
    const cfg = pieceConfigs.get(unitId);
    if (!cfg) { console.log('tryPlace: no config for', unitId); return; }
    const cost = cfg?.cost?.[0]?.gold || 0;
    if (currentGold < cost) { debug('Not enough gold!'); console.log('tryPlace: not enough gold, have', currentGold, 'need', cost); return; }
    if (isBlocked(x, y, unitId)) { debug('Cannot place here!'); console.log('tryPlace: blocked at', x, y); return; }
    const originY = (cfg.height || 48) / 2;
    const obj = new GameObject(cls, x, y - originY);
    obj.direction_x = 0;
    obj.direction_y = 0;
    gameObjects.add(obj);
    cls.gameObjects.add(obj);
    const targeting = cfg.default_targeting || 'first';
    const projIds = cfg.projectile_ids as string[] | undefined;
    const flaskId = projIds?.includes('naptha_flask')
      ? 'naptha_flask'
      : (projIds?.[0]) || undefined;
    const projOrigin: { x: number; y: number } = (cfg as any).projectile_origin || { x: 0, y: 0 };
    const towerEntry: PlayerCombatant = { obj, configId: unitId, level: 0, piece: cfg, targeting, flaskId, originY, projOrigin, attackPeriod: 1.0 / (cfg.attack_rate || 1.0) };
    activeCombatants.push(towerEntry);
    if (targeting === 'nearest_path') {
      cachePathSegTargets(towerEntry, cfg.range || 0.2);
    } else if (targeting === 'selectable') {
      const info = findNearestPathSegInfo(x, y);
      if (info) towerEntry.selectableTarget = { x: info.point.x, y: info.point.y };
    }
    if (soldierIds.has(unitId)) {
      obj.draggable = true;
      obj.dragFollowsCursor = true;
      obj.onDragStart(0, () => {
        (obj.var as DragObjVar)._dragOrigX = obj.x;
        (obj.var as DragObjVar)._dragOrigY = obj.y;
      });
      obj.onDragEnd(0, () => {
        if (isBlocked(obj.x, obj.y + towerEntry.originY, unitId, obj)) {
          obj.x = (obj.var as DragObjVar)._dragOrigX;
          obj.y = (obj.var as DragObjVar)._dragOrigY;
        }
        cachePathSegTargets(towerEntry, towerEntry.piece.range || 0.2);
      });
    }
    currentGold -= cost;
    if (resourceText) {
      resourceText.text = `{img:copper} ${currentGold}`;
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
    const tower = activeCombatants.find(t => t.obj === selectedTower!.obj);
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
        (newObj.var as DragObjVar)._dragOrigX = newObj.x;
        (newObj.var as DragObjVar)._dragOrigY = newObj.y;
      });
      newObj.onDragEnd(0, () => {
        if (isBlocked(newObj.x, newObj.y, piece.becomes, newObj)) {
          newObj.x = (newObj.var as DragObjVar)._dragOrigX;
          newObj.y = (newObj.var as DragObjVar)._dragOrigY;
        }
      });
    }

    const newIdx = activeCombatants.indexOf(tower);
    const pieceCfg = pieceConfigs.get(piece.becomes);
    if (!pieceCfg) { debug('Upgrade target config missing'); return; }
    const newOriginY = (pieceCfg.height || 48) / 2;
    const newProjOrigin: { x: number; y: number } = (pieceCfg as any).projectile_origin || { x: 0, y: 0 };
    activeCombatants[newIdx] = { obj: newObj, configId: piece.becomes, level: tower.level + 1, piece: pieceCfg, targeting: tower.targeting, flaskId: tower.flaskId, originY: newOriginY, projOrigin: newProjOrigin, attackPeriod: 1.0 / (pieceCfg.attack_rate || 1.0) };
    currentGold -= goldCost;
    if (resourceText) {
      resourceText.text = `{img:copper} ${currentGold}`;
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
    const towerIdx = activeCombatants.findIndex(t => t.obj === selectedTower!.obj);
    if (towerIdx < 0) { setSelectedTower(null); return; }
    const tower = activeCombatants[towerIdx];
    const cost = tower.piece.cost?.[0]?.gold || 0;
    const refund = Math.floor(cost * 0.5);
    currentGold += refund;
    if (resourceText) {
      resourceText.text = `{img:copper} ${currentGold}`;
      resourceText.setInlineImages({
        copper: { image: '/images/ui/coin_copper.png', width: 20, height: 20 }
      });
    }
    refreshButtonStates();
    tower.obj.destroy();
    activeCombatants.splice(towerIdx, 1);
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
    setSidebarVisible(false);
    if (rpButton) { rpButton.text = 'Pause'; rpButton.color = '#555555'; }
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
          doSpawn(sq.enemyId, sq);
          sq.remaining--;
        }
        continue;
      }
      sq.spawnTimer += dt * 1000;
      if (sq.spawnTimer >= sq.intervalMs) {
        sq.spawnTimer = 0;
        doSpawn(sq.enemyId, sq);
        sq.remaining--;
      }
    }
    if (!anyLeft) allSpawned = true;
  }

  function doSpawn(enemyId: string, sq?: { spawnPointId?: string }) {
    const cls = mobEnemyClassMap.get(enemyId);
    if (!cls || !mapMetadata?.spawnPoints.length) return;
    const sp = sq?.spawnPointId
      ? mapMetadata.spawnPoints.find((s: any) => s.id === sq.spawnPointId)
      : null;
    const spawn = sp ?? mapMetadata.spawnPoints[0];
    const e = cls.spawn(spawn.x, spawn.y);
    const ev = e.var as EnemyVar;
    ev.enemyId = enemyId;
    ev.hp = cls.defaultHitpoints;
    ev.maxHp = cls.defaultHitpoints;

    const p = pathMap.get(spawn.targetPathId);
    if (!p?.waypoints?.length) {
      console.log(`[TD] doSpawn: no waypoints for path ${spawn.targetPathId}`);
      return;
    }
    const mobCfg = mobConfigs.get(enemyId);
    const spd = mobCfg?.speed || 1.0;
    ev.baseSpeed = spd;
    const wps = p.waypoints;
    const startIdx = find_next_waypoint(wps, 0, spawn.x, spawn.y);
    if (startIdx >= wps.length) {
      console.log(`[TD] doSpawn: ${enemyId} all waypoints at spawn, stuck`);
      return;
    }
    ev.waypointIndex = startIdx;
    ev.pathId = spawn.targetPathId;
    e.decelerationDistance = 0;
    // Enable sprite mirroring based on movement direction (spriteForwardVector inherited from class default)
    e.mirrorOnDirection = true;

    e.onArrival(() => {
      const ev2 = e.var as EnemyVar;
      const idx = ev2.waypointIndex;
      const pid = ev2.pathId;
      const wpList = pathMap.get(pid)?.waypoints;
      if (!wpList) return;
      const nextIdx = find_next_waypoint(wpList, idx + 1, e.x, e.y);
      ev2.waypointIndex = nextIdx;
      // Still waypoints left on current path → normal move
      if (nextIdx < wpList.length) {
        const nw = wpList[nextIdx];
        const s2 = mobConfigs.get(ev2.enemyId)?.speed || 1.0;
        const dx = nw.x - e.x;
        const dy = nw.y - e.y;
        const dist = Math.hypot(dx, dy);
        e.moveTo({ x: nw.x, y: nw.y }, dist / (s2 * 60));
        ev2.baseVelocity = e.velocity;
        return;
      }
      // Path waypoints exhausted — check endpoint
      const currentPath = pathMap.get(pid);
      if (!currentPath) return;
      // End point → terminal, enemy escapes
      if (currentPath.endAtEndPointId) {
        enemyEscaped(e);
        return;
      }
      // Intersection → follow a branch to the next path
      if (currentPath.endAtIntersectionId) {
        const intersection = intersectionMap.get(currentPath.endAtIntersectionId);
        if (intersection?.branches?.length) {
          const chosen = intersection.branches.length === 1
            ? intersection.branches[0]
            : pick_weighted_branch(intersection.branches);
          ev2.pathId = chosen.pathId;
          const nextPath = pathMap.get(chosen.pathId);
          if (nextPath?.waypoints?.length) {
            // Enemy is already standing at wps[0] of the new path (the intersection)
            const firstIdx = find_next_waypoint(nextPath.waypoints, 1, e.x, e.y);
            if (firstIdx >= nextPath.waypoints.length) {
              console.log(`[TD] onArrival: ${ev2.enemyId} new path ${chosen.pathId} all waypoints at intersection`);
              return;
            }
            ev2.waypointIndex = firstIdx;
            const firstWp = nextPath.waypoints[firstIdx];
            const s2 = mobConfigs.get(ev2.enemyId)?.speed || 1.0;
            const dx = firstWp.x - e.x;
            const dy = firstWp.y - e.y;
            e.moveTo({ x: firstWp.x, y: firstWp.y }, Math.hypot(dx, dy) / (s2 * 60));
            ev2.baseVelocity = e.velocity;
            return;
          }
        }
        console.log(`[TD] onArrival: ${ev2.enemyId} stuck at intersection ${currentPath.endAtIntersectionId}`);
        return;
      }
      console.log(`[TD] onArrival: ${ev2.enemyId} path ${pid} has no end point or intersection, stopped`);
    });

    if (wps[startIdx]) {
      const dx = wps[startIdx].x - e.x;
      const dy = wps[startIdx].y - e.y;
      const dist = Math.hypot(dx, dy);
      e.moveTo({ x: wps[startIdx].x, y: wps[startIdx].y }, dist / (spd * 60));
      ev.baseVelocity = e.velocity;
    }
    console.log(`[TD] doSpawn: ${enemyId} at (${spawn.x.toFixed(0)}, ${spawn.y.toFixed(0)}) hp=${cls.defaultHitpoints} spd=${spd} waypoints=${wps.length} startIdx=${startIdx}`);
    e.logMovement();
  }

  function enemyEscaped(e: Enemy) {
    currentLives--;
    const ev = e.var as EnemyVar;
    const eid = ev.enemyId;
    if (eid) leakedEnemies[eid] = (leakedEnemies[eid] || 0) + 1;
    const wp = pathMap.get(ev.pathId);
    console.log(`[TD] enemyEscaped: ${eid} path=${ev.pathId} wpIdx=${ev.waypointIndex}/${wp?.waypoints?.length} at (${e.x.toFixed(0)}, ${e.y.toFixed(0)}) lives=${currentLives}`);
    if (livesText) livesText.text = `Lives: ${currentLives}`;
    enemies.delete(e);
    e.destroy();
    if (currentLives <= 0) { gameState = 'lost'; endGame(); }
  }

  function getProjectileType(t: { configId: string; flaskId?: string; piece: any }): string | null {
    const projIds = t.piece.projectile_ids as string[] | undefined;
    if (projIds && projIds.length > 0) {
      return t.flaskId || projIds[0];
    }
    const pid = t.piece.projectile_id as string | undefined;
    if (pid) return pid;
    if (['shortbow_archer', 'single_archer_tower', 'three_archer_tower'].includes(t.configId)) return 'hunting_arrow';
    if (['longbow_archer', 'ballista'].includes(t.configId)) return 'war_arrow';
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
    for (const t of activeCombatants) {
      const range = t.piece.range || 0.2;  // range in board units
      const tdmg = t.piece.damage?.[Math.min(t.level, (t.piece.damage?.length || 1) - 1)] || 10;
      const targeting = t.targeting;

      t.atkTimer = (t.atkTimer === undefined ? t.attackPeriod : t.atkTimer) + dt;
      if (t.atkTimer < t.attackPeriod) continue;

      let fireX: number | null = null;
      let fireY: number | null = null;
      let reasonEnemy: Enemy | null = null;

      if (targeting === 'nearest_path') {
        const pt = getWeightedPathTarget(t);
        if (pt) { fireX = pt.x; fireY = pt.y; }
      } else if (targeting === 'selectable') {
        const st = t.selectableTarget;
        if (st) { fireX = st.x; fireY = st.y; }
      } else {
        let target: Enemy | null = null;
        const tLy = t.obj.y + t.originY;
        if (targeting === 'closest') {
          let best = range;
          for (const e of enemies) {
            const d = Math.hypot(e.x - t.obj.x, e.y - tLy);
            if (d > range) continue;
            if (d < best) { best = d; target = e; }
          }
        } else if (targeting === 'first') {
          let bestDepth = Infinity;
          let bestWpIdx = -1;
          for (const e of enemies) {
            const d = Math.hypot(e.x - t.obj.x, e.y - tLy);
            if (d > range) continue;
            const ev = e.var as EnemyVar;
            const depth = pathDepthCache.get(ev.pathId) ?? Infinity;
            const wpIdx = ev.waypointIndex;
            if (depth < bestDepth || (depth === bestDepth && wpIdx > bestWpIdx)) {
              bestDepth = depth;
              bestWpIdx = wpIdx;
              target = e;
            }
          }
        } else {
          let bestVal = targeting === 'strongest' ? -1 : Infinity;
          for (const e of enemies) {
            const d = Math.hypot(e.x - t.obj.x, e.y - tLy);
            if (d > range) continue;
            const ev = e.var as EnemyVar;
            const val = targeting === 'strongest' ? (ev.maxHp || 0) : (ev.hp || 0);
            if (targeting === 'strongest' ? val > bestVal : val < bestVal) { bestVal = val; target = e; }
          }
        }
        if (target) {
          fireX = target.x; fireY = target.y;
          reasonEnemy = target;
        }
      }

      if (fireX === null) continue;
      const fx = fireX;
      const fy = fireY!;

      t.atkTimer = 0;
      const projType = getProjectileType(t);
      if (!projType) continue;
      const pClass = projectileClasses[projType];
      if (!pClass) continue;
      const p = pClass.spawn(t.obj.x + t.projOrigin.x, t.obj.y + t.projOrigin.y);
      const cfg = projectileConfigs.get(projType)!;
      const pv = p.var as ProjectileVar;

      if (cfg.orbit_radius) {
        pv.isOrbital = true;
        p.singleCollisionOnly = false;
        pv.hitEnemies = new Set();
        p.alignToTravel = false;
        const angleDeg = Math.atan2(fy - t.obj.y, fx - t.obj.x) * (180 / Math.PI);
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
          const hitSet = pv.hitEnemies;
          if (hitSet.has(enemy)) return;
          hitSet.add(enemy);
          hitEnemy(enemy, tdmg);
        });
        continue;
      }

      const aeCfg = cfg.area_effect;
      if (aeCfg) {
        // Flask projectile — no direct damage, spawns pool on impact
        p.setSpeed(cfg.speed || 400);
        p.mirrorOnDirection = true;
        p.setOrientationTowards({ x: fx, y: fy });
        pv.targetX = fx;
        pv.targetY = fy;
        pv.aeCfg = aeCfg;
      } else {
        // Standard homing projectile (arrows)
        pv.dmg = tdmg;
        pv.aoe = (t.piece.area_of_effect?.radius || 0);  // AoE radius in board units
        p.setSpeed(cfg.speed || 1600);
        p.mirrorOnDirection = true;
        if (reasonEnemy) {
          p.setOrientationTowards(predictIntercept(t.obj, reasonEnemy, cfg.speed || 1600));
        } else {
          p.setOrientationTowards({ x: fx, y: fy });
        }
        p.onCollisionWithEnemy((enemy: Enemy) => {
          const hitDmg = pv.dmg || 10;
          const hitAoe = pv.aoe || 0;
          console.log(`[TD] projHit: ${t.configId} → ${(enemy.var as EnemyVar).enemyId} dmg=${hitDmg} dist=${Math.hypot(enemy.x - p.x, enemy.y - p.y).toFixed(1)}`);
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
  }

  function hitEnemy(e: Enemy, dmg: number) {
    const ev = e.var as EnemyVar;
    const before = ev.hp;
    ev.hp = (ev.hp || 0) - dmg;
    console.log(`[TD] hitEnemy: ${ev.enemyId} ${before} -> ${ev.hp} dmg=${dmg}`);
    if (ev.hp <= 0) {
      const mobId = ev.enemyId;
      const mc = mobConfigs.get(mobId);
      currentGold += mc?.reward_gold || 1;
      if (resourceText) {
        resourceText.text = `{img:copper} ${currentGold}`;
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
        session_id: (tdData as unknown as KickoffResponse).session_id,
        lives_lost: Math.max(0, initialLives - currentLives),
        leaked_enemies: leakedEnemies
      });
      const ar = result as unknown as AdvanceResponse;
      console.log('[TD] advanceRound response:', ar);
      if (ar.next_round) {
        currentRound = ar.round_number;
        totalRounds = ar.total_rounds;
        // Clear old enemies and projectiles
        for (const e of enemies) { enemies.delete(e); e.destroy(); }
        for (const p of projectiles) { p.destroy(); }
        // Reset spawn queue with new schedule
        spawnQueue = [];
        const schedule = ar.spawn_schedule || [];
        for (const e of schedule) {
          spawnQueue.push({
            enemyId: e.enemy_id, remaining: e.count,
            intervalMs: e.interval_ms, initialDelayMs: e.initial_delay_ms,
            spawnTimer: 0, initialDone: false,
            spawnPointId: e.spawn_point_id
          });
        }
        allSpawned = false;
        roundStarted = false;
        setSidebarVisible(true);
        // Update lives from server response
        if (ar.lives != null) currentLives = ar.lives;
        leakedEnemies = {};
        refreshButtonStates();
        // Update UI
        if (resourceText) {
          resourceText.text = `{img:copper} ${currentGold}`;
          resourceText.setInlineImages({
            copper: { image: '/images/ui/coin_copper.png', width: 20, height: 20 }
          });
        }
        if (livesText) livesText.text = `Lives: ${currentLives}`;
        if (rpButton) {
          rpButton.text = `Round ${currentRound}/${totalRounds}`;
          rpButton.color = '#2D7A2D';
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
          new_best_score: ar.new_best_score || 0,
          times_played: ar.times_played || 0,
          all_levels_done: ar.all_levels_done || false,
          base_unlocked: ar.base_unlocked || false,
          game_phase: ar.game_phase || 'initial_mission',
          next_level_id: null,
          rewards: ar.rewards || {},
          land_patent_earned: ar.land_patent_earned || false,
          duke_right_earned: ar.duke_right_earned || false,
          new_unlocks: ar.new_unlocks as NewUnlocks | undefined
        });
      }
    } catch (e) {
      onError(e instanceof Error ? e.message : 'Failed to advance round');
    } finally {
      roundInTransition = false;
    }
  }

  async function endGame(forceLoss = false, forfeited = false) {
    placementMode = null;
    boardDragActive = false;
    dragUnitId = null;
    selectedTower = null;
    setSidebarVisible(true);
    const score = currentGold + currentLives * 10;

    try {
      const result = await tdRound(characterId, {
        session_id: (tdData as unknown as KickoffResponse).session_id,
        lives_lost: forceLoss ? 100 : Math.max(0, initialLives - currentLives),
        leaked_enemies: leakedEnemies
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
        duke_right_earned: cr.duke_right_earned || false,
        new_unlocks: cr.new_unlocks
      }, forfeited);
    } catch (e) {
      onError(e instanceof Error ? e.message : 'Failed to end game');
    }
  }

  function forfeitGame() {
    if (gameState === 'won' || gameState === 'lost') return;
    deleteConfig("td_placements").catch(e => console.log('[TD] Failed to clear saved state:', e));
    if (!roundStarted && currentRound === 1) {
      gameState = 'lost';
      placementMode = null;
      boardDragActive = false;
      dragUnitId = null;
      selectedTower = null;
      onComplete({ completed: false, score: 0, new_best_score: 0, times_played: 0, all_levels_done: false, base_unlocked: false, game_phase: 'initial_mission', next_level_id: null, rewards: {}, land_patent_earned: false, duke_right_earned: false }, true);
      return;
    }
    gameState = 'lost';
    endGame(true, true);
  }

  // ============================================================
  // Init
  // ============================================================

  async function init() {
    try {
      const result = await tdRound(characterId, { mini_game: miniGame, level_id: levelId });
      tdData = result as TDRoundKickoffResponse;

      const kd = tdData as unknown as KickoffResponse;

      // Load saved placements from OPFS on resume
      if (kd.resumed) {
        try {
          const saved = await getConfig<SavedTDState>("td_placements");
          if (saved && saved.session_id === kd.session_id && saved.character_id === characterId) {
            pendingRestore = saved.placements;
            pendingGold = saved.gold ?? null;
          }
        } catch (e) {
          console.log('[TD] Failed to load saved placements:', e);
        }
      }

      // Canvas sized to board dimensions (sidebar is an overlay)
      canvasEl.width = BW;
      canvasEl.height = BH;
      setBoardSize(BW, BH);
      initEngine(canvasEl, debugEl, false, setupGame);

      function resizeCanvas() {
        const vw = window.innerWidth;
        const vh = window.innerHeight;
        const aspect = BW / BH;
        let w = vw, h = vw / aspect;
        if (h > vh) { h = vh; w = h * aspect; }
        canvasEl.style.width = `${w}px`;
        canvasEl.style.height = `${h}px`;
      }
      resizeCanvas();
      resizeHandler = resizeCanvas;
      window.addEventListener('resize', resizeCanvas);

      // Native listeners for board click-and-drag placement
      const onMouseDown = (e: MouseEvent) => {
        if (placementMode && e.button === 0) {
          // Mouse position in board units: canvas pixel × (BW / canvasWidth) = clientX since canvas = BW px
          const bx = BW * (e.clientX / canvasEl.clientWidth);
          console.log('native mousedown: placementMode=', placementMode, 'bx=', bx, 'bw=', BW);
          if (bx < BW) boardDragActive = true;
        }
      };
      const onMouseUp = (e: MouseEvent) => {
        if (e.button !== 0) return;
        boardDragActive = false;
        if (placementMode) {
          const bx = BW * (e.clientX / canvasEl.clientWidth);
          const by = BH * (e.clientY / canvasEl.clientHeight);
          console.log('native mouseup: placing', placementMode, 'at', bx, by);
          if (bx < BW && by >= 0 && by < BH && !isBlocked(bx, by, placementMode!)) {
            const cfg = pieceConfigs.get(placementMode!);
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
    if (resizeHandler) window.removeEventListener('resize', resizeHandler);
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
