<script lang="ts">
  import { onDestroy } from 'svelte';
  import { currentCharacter } from '../lib/stores';
  import { getFiefdomRequest, buildRequest, getBuildingConfigsRequest, setFiefdomImportRequest, setFiefdomReserveRequest, setBuildingOutputRateRequest, upgradeBuildingRequest, convertBuildingRequest, demolishBuildingRequest, upgradePondTypeRequest, getReeveReportRequest } from '../lib/api';
  import type { FiefdomResponse, FiefdomBuilding, BuildingTypeConfig, EconomyReport, ReeveReport, ReeveTarget, ReeveAction } from '../lib/api';
  import { asset_url } from '../lib/public_urls';
  import RetinuePanel from './RetinuePanel.svelte';
  import ReevePanel from './ReevePanel.svelte';
  import { execute_reeve_action } from '../lib/reeve_actions';
  import { reeve_error_text_id } from '../lib/reeve_text';
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
  let riverObjMap = new Map<string, GameObject>();
  let waterBadgeMap = new Map<number, GameObject>();
  let pondLabelMap = new Map<number, Text>();

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
  let candidateIds: string[] = [];
  let buildButtons = new Map<string, Button>();

  let constructionRefreshInFlight = false;
  let completionPollRequested = new Set<number>();

  let showIntro = $state(false);
  let introHtml = $state('');

  let showEconomy = $state(false);
  let showProduction = $state(false);
  let showRetinue = $state(false);
  let showReeve = $state(false);
  let retinueBtn: Button | null = null;
  let reeveBtn: Button | null = null;
  let reeveReport = $state<ReeveReport | null>(null);
  let reeveLoading = $state(false);
  let reeveError = $state('');
  let reeveActionError = $state('');
  let reeveActionBusy = $state(false);
  let roadLinkPreview = $state<Array<{ x: number; y: number }> | null>(null);
  let roadLinkOverlays: GameObject[] = [];
  let ghostPreload: { mouse_x: number; mouse_y: number; gx: number; gy: number } | null = null;
  let reevePlacementServer: { typeId: string; x: number; y: number; gx: number; gy: number } | null = null;
  let economyReport = $state<EconomyReport | null>(null);
  let selectedBuildingId = $state<number | null>(null);
  let buildingCardBusy = $state(false);
  let buildingCardError = $state('');
  const IMPORT_RESOURCES = ['grain', 'wood', 'steel', 'bronze', 'leather', 'mana', 'charcoal', 'iron', 'ironwork', 'fancy_ironwork', 'beams', 'boards'];
  const RESOURCE_DISPLAY: Record<string, string> = {
    grain: 'Grain', wood: 'Wood', steel: 'Steel', bronze: 'Bronze',
    leather: 'Leather', mana: 'Mana', charcoal: 'Charcoal',
    iron: 'Iron', ironwork: 'Ironwork', fancy_ironwork: 'Fancy Ironwork',
    beams: 'Beams', boards: 'Boards'
  };
  let reserveInputs = $state<Record<string, number>>({});

  /**
   * Resolves a resource's display name from the text system, falling back to
   * the legacy English map only if a translation is not loaded yet.
   *
   * @param resource - Fiefdom resource key
   * @returns Display label
   */
  function resourceLabel(resource: string): string {
    return manorTexts['ui_resource_' + resource] ?? RESOURCE_DISPLAY[resource] ?? resource;
  }

  /**
   * Resolves a building type's display name from the text system.
   *
   * @param typeId - Building type id
   * @param fallback - Config display name fallback
   * @returns Display label
   */
  function buildingLabel(typeId: string, fallback: string): string {
    return manorTexts['ui_building_' + typeId] ?? fallback;
  }

  /**
   * Formats a manor text id with simple token substitution.
   *
   * @param textId - Text-system id
   * @param tokens - Token values to substitute
   * @returns Localized text (empty when the id is not loaded)
   */
  function formatManorText(textId: string, tokens: Record<string, string | number> = {}): string {
    let text = manorTexts[textId] ?? '';
    for (const [key, value] of Object.entries(tokens)) {
      text = text.split(`{${key}}`).join(String(value));
    }
    return text;
  }

  function createOverlayClass(color: string, id: string): ItemClass {
    const c = document.createElement('canvas');
    c.width = 1;
    c.height = 1;
    const ctx = c.getContext('2d')!;
    ctx.fillStyle = color;
    ctx.fillRect(0, 0, 1, 1);
    return new ItemClass(id, c.toDataURL());
  }

  // ── Channel tiles (roads / head races / tail races) ──────────────────────
  // Roads and races auto-tile: each 1x1 tile picks a base image
  // (straight/corner/three_way/four_way) from the config's `*_tiles_canonical`
  // and is rotated via the engine's setOrientation to match its same-type
  // neighbors. The tile art is generated procedurally (canvas data URLs) so the
  // network always renders with aligned seams; real art can later replace the
  // PNGs referenced by `*_tiles` (see tools/generate_road_tiles.py).

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

  /**
   * Draws a race channel tile. Head races are wooden, elevated launders carrying
   * water (blue channel over plank); tail races are ground channels (dry earth).
   *
   * @param dirs - Screen directions the channel runs across
   * @param kind - 'head_race' (wooden, water) or 'tail_race' (earth channel)
   * @param size - Tile pixel size
   * @returns A canvas data URL
   */
  function makeRaceTileDataUrl(dirs: string[], kind: 'head_race' | 'tail_race', size: number = 128): string {
    const c = document.createElement('canvas');
    c.width = size;
    c.height = size;
    const ctx = c.getContext('2d')!;
    const isHead = kind === 'head_race';
    // Base: planks (head) vs dirt (tail)
    ctx.fillStyle = isHead ? '#3a332a' : '#5d5340';
    ctx.fillRect(0, 0, size, size);
    for (let i = 0; i < 400; i++) {
      const shade = 50 + Math.floor(Math.random() * 30);
      ctx.fillStyle = isHead
        ? `rgb(${shade}, ${shade - 6}, ${shade - 18})`
        : `rgb(${shade + 10}, ${shade}, ${shade - 22})`;
      ctx.fillRect(Math.floor(Math.random() * size), Math.floor(Math.random() * size), 2, 2);
    }
    // Channel band across connected sides (water for head, gravel for tail)
    const band = Math.round(size * 0.42);
    const half = Math.round(band / 2);
    const cx = Math.round(size / 2);
    const cy = Math.round(size / 2);
    ctx.fillStyle = isHead ? '#5b7f9e' : '#8a7f6e';
    if (dirs.includes('n')) ctx.fillRect(cx - half, 0, band, cy + half);
    if (dirs.includes('s')) ctx.fillRect(cx - half, cy - half, band, size - cy + half);
    if (dirs.includes('e')) ctx.fillRect(cx - half, cy - half, size - cx + half, band);
    if (dirs.includes('w')) ctx.fillRect(0, cy - half, cx + half, band);
    // Edge lines
    ctx.strokeStyle = isHead ? '#2e2517' : '#5b5044';
    ctx.lineWidth = 2;
    ctx.strokeRect(cx - half, 0, band, cy + half);
    ctx.strokeRect(cx - half, cy - half, band, size - cy + half);
    ctx.strokeRect(cx - half, cy - half, size - cx + half, band);
    ctx.strokeRect(0, cy - half, cx + half, band);
    return c.toDataURL();
  }

  /** Draws a procedural water tile for the river. */
  function makeWaterTileDataUrl(size: number = 128): string {
    const c = document.createElement('canvas');
    c.width = size;
    c.height = size;
    const ctx = c.getContext('2d')!;
    ctx.fillStyle = '#2f5d8c';
    ctx.fillRect(0, 0, size, size);
    for (let i = 0; i < 600; i++) {
      const shade = 30 + Math.floor(Math.random() * 45);
      const g = 70 + Math.floor(Math.random() * 30);
      const b = 110 + Math.floor(Math.random() * 50);
      ctx.fillStyle = `rgb(${shade}, ${g}, ${b})`;
      ctx.fillRect(Math.floor(Math.random() * size), Math.floor(Math.random() * size), 3, 2);
    }
    ctx.fillStyle = 'rgba(255,255,255,0.22)';
    for (let i = 0; i < 20; i++) {
      ctx.fillRect(Math.floor(Math.random() * size), Math.floor(Math.random() * size), 2, 2);
    }
    return c.toDataURL();
  }

  /** Draws a small solid dot (used for the powered/unpowered badge). */
  function makeDotDataUrl(color: string, size: number = 32): string {
    const c = document.createElement('canvas');
    c.width = size;
    c.height = size;
    const ctx = c.getContext('2d')!;
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(size / 2, size / 2, size / 2 - 2, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = '#111111';
    ctx.lineWidth = 2;
    ctx.stroke();
    return c.toDataURL();
  }

  let channelTileClasses = new Map<string, ItemClass>();
  let riverTileClass: ItemClass | null = null;
  let poweredDotClass: ItemClass | null = null;
  let unpoweredDotClass: ItemClass | null = null;

  /**
   * Resolves which base tile + rotation (degrees) to use for a channel tile at
   * the given grid cell, based on its orthogonal same-type neighbors.
   *
   * @param gx - Cell x
   * @param gy - Cell y
   * @param typeId - 'road' | 'head_race' | 'tail_race'
   * @param cfg - The building config (*_tiles / *_tiles_canonical)
   * @param buildings - All fiefdom buildings (for neighbor lookup)
   * @returns The tile key and orientation; falls back to the straight tile
   */
  function resolveChannelTile(gx: number, gy: number, typeId: string, cfg: BuildingTypeConfig,
                              buildings: Array<{ name: string; x: number; y: number }>):
    { key: string; orientation: number } {
    const actual: string[] = [];
    if (buildings.some(b => b.name === typeId && b.x === gx && b.y === gy - 1)) actual.push('n');
    if (buildings.some(b => b.name === typeId && b.x === gx + 1 && b.y === gy)) actual.push('e');
    if (buildings.some(b => b.name === typeId && b.x === gx && b.y === gy + 1)) actual.push('s');
    if (buildings.some(b => b.name === typeId && b.x === gx - 1 && b.y === gy)) actual.push('w');

    const canonicalKey = typeId === 'road' ? 'road_tiles_canonical' : 'race_tiles_canonical';
    const canonical = (cfg[canonicalKey] as Record<string, string[]> | undefined) ?? {};
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

  /** Builds (and caches) ItemClasses for a channel type's four base tiles. */
  function ensureChannelTileClasses(cfg: BuildingTypeConfig, typeId: string): void {
    const prefix = typeId + ':';
    for (const existing of channelTileClasses.keys()) {
      if (existing.startsWith(prefix)) return;
    }
    const canonicalKey = typeId === 'road' ? 'road_tiles_canonical' : 'race_tiles_canonical';
    const canonical = (cfg[canonicalKey] as Record<string, string[]> | undefined) ?? {};
    const tileDirs: Record<string, string[]> = {
      straight: canonical.straight ?? ['e', 'w'],
      corner: canonical.corner ?? ['n', 'e'],
      three_way: canonical.three_way ?? ['n', 'e', 'w'],
      four_way: ['n', 'e', 's', 'w']
    };
    for (const [key, dirs] of Object.entries(tileDirs)) {
      if (channelTileClasses.has(prefix + key)) continue;
      const url = typeId === 'road'
        ? makeRoadTileDataUrl(dirs)
        : makeRaceTileDataUrl(dirs, typeId as 'head_race' | 'tail_race');
      channelTileClasses.set(prefix + key, new ItemClass(typeId + '_' + key, url));
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

  /**
   * Resolves a building's stage chain [root, ..., building] by walking
   * `built_from` links. A building's chain includes itself and every lower
   * stage it could have been converted from. Chain depth is unbounded.
   *
   * @param typeId - Building type id from the config
   * @returns Ordered chain from the root stage down to the given building
   */
  function stageChain(typeId: string): string[] {
    const chain: string[] = [];
    let cur: string | undefined = typeId;
    const seen = new Set<string>();
    while (cur && !seen.has(cur)) {
      seen.add(cur);
      chain.push(cur);
      cur = getCfg(cur)?.built_from;
    }
    return chain.reverse();
  }

  /**
   * Total length of a building's stage chain (all ancestors and all successors).
   *
   * @param typeId - Building type id from the config
   * @returns The number of stages in the chain that includes typeId
   */
  function fullChainLength(typeId: string): number {
    const chain = stageChain(typeId);
    let len = chain.length;
    let cur = chain[chain.length - 1];
    while (true) {
      const next = Object.keys(buildingConfigs).find(id => buildingConfigs[id]?.built_from === cur);
      if (!next) break;
      len++;
      cur = next;
    }
    return len;
  }

  /**
   * Highest level among the fiefdom's buildings whose stage chain includes the
   * required stage at or below their own stage (a villein/yeoman counts as a
   * peasant; a plain peasant never counts as a villein). Mirrors the server's
   * chain-aware prerequisite resolution.
   *
   * @param requiredId - The building stage a prerequisite requires
   * @returns Max level across eligible buildings (0 if none)
   */
  function satisfiedLevel(requiredId: string): number {
    let best = 0;
    for (const b of fiefdomData?.buildings ?? []) {
      if (stageChain(b.name).includes(requiredId)) {
        best = Math.max(best, b.level);
      }
    }
    return best;
  }

  // Cost array field → resource name pairs (mirrors the server cost system).
  const COST_FIELDS: Array<[string, string]> = [
    ['gold_cost', 'gold'], ['silver_pence_cost', 'silver_pence'], ['wood_cost', 'wood'],
    ['steel_cost', 'steel'], ['bronze_cost', 'bronze'],
    ['grain_cost', 'grain'], ['leather_cost', 'leather'], ['mana_cost', 'mana'],
    ['charcoal_cost', 'charcoal'], ['iron_cost', 'iron'], ['ironwork_cost', 'ironwork'],
    ['fancy_ironwork_cost', 'fancy_ironwork'], ['beams_cost', 'beams'], ['boards_cost', 'boards']
  ];

  /**
   * A building type's level cost (level-1 cost for a fresh build; costs[level]
   * for an upgrade from level to level+1).
   *
   * @param typeId - Building type id from the config
   * @param levelIndex - 0-based level index into the cost arrays
   * @returns Map of resource name → cost amount for that step
   */
  function levelCost(typeId: string, levelIndex: number): Record<string, number> {
    const cfg = getCfg(typeId);
    const out: Record<string, number> = {};
    if (!cfg) return out;
    for (const [field, res] of COST_FIELDS) {
      const arr = cfg[field];
      if (Array.isArray(arr) && typeof arr[levelIndex] === 'number') {
        out[res] = arr[levelIndex] as number;
      }
    }
    return out;
  }

  /**
   * Cumulative cost spent on a building up to (but not including) its current
   * level — used for the convert discount (80% credit).
   *
   * @param typeId - Building type id from the config
   * @param level - The building's current level
   * @returns Map of resource name → cumulative spent
   */
  function cumulativeCost(typeId: string, level: number): Record<string, number> {
    const out: Record<string, number> = {};
    for (let i = 0; i < level; i++) {
      for (const [res, amt] of Object.entries(levelCost(typeId, i))) {
        out[res] = (out[res] ?? 0) + amt;
      }
    }
    return out;
  }

  /**
   * The successor building this building converts into (the config whose
   * `built_from` matches), or undefined if the building is a chain's leaf.
   *
   * @param typeId - Building type id from the config
   * @returns Successor type id, or undefined
   */
  function stageSuccessor(typeId: string): string | undefined {
    return Object.keys(buildingConfigs).find(id => buildingConfigs[id]?.built_from === typeId);
  }

  /**
   * Conversion price for turning a building into its successor, mirroring the
   * server: max(0, successor level-1 cost − 80% × old cumulative spent).
   *
   * @param b - The building instance being converted
   * @returns Map of resource name → convert price (may be empty = free)
   */
  function convertCost(b: FiefdomBuilding): Record<string, number> {
    const successor = stageSuccessor(b.name);
    const out: Record<string, number> = {};
    if (!successor) return out;
    const succLvl1 = levelCost(successor, 0);
    const oldCum = cumulativeCost(b.name, b.level);
    for (const [res, amt] of Object.entries(succLvl1)) {
      const price = amt - (oldCum[res] ?? 0) * 0.8;
      if (price > 0) out[res] = Math.round(price * 100) / 100;
    }
    return out;
  }

  /**
   * Returns the construction_times array to use for a building's progress bar.
   * Mill ponds read from their current pond type (timber/stone have their own
   * build times); all other buildings use the top-level array.
   *
   * @param b - The building instance (uses pond_type for mill ponds)
   * @param cfg - The building type config
   * @returns The construction_times array (falls back to the top-level array)
   */
  function getConstructionTimes(b: FiefdomBuilding, cfg: BuildingTypeConfig): number[] {
    if (b.name === 'mill_pond' && cfg.pond_types?.length) {
      const stored = b.pond_type && cfg.pond_types.some(t => t.id === b.pond_type)
        ? b.pond_type : cfg.pond_types[0].id;
      const typeCfg = cfg.pond_types.find(t => t.id === stored) ?? cfg.pond_types[0];
      if (typeCfg.construction_times?.length) return typeCfg.construction_times;
    }
    return cfg.construction_times;
  }

  function checkVal(gx: number, gy: number, typeId: string): { valid: boolean; reason: string } {
    if (!fiefdomData) return { valid: false, reason: formatManorText('ui_manor_no_fiefdom') };
    const cfg = getCfg(typeId);
    if (!cfg) return { valid: false, reason: formatManorText('ui_manor_unknown_building') };
    if (fiefdomData.manor_level < cfg.min_manor_level) {
      return {
        valid: false,
        reason: formatManorText('ui_manor_need_manor_level', { level: cfg.min_manor_level })
      };
    }
    const maxCount = cfg.max_per_fiefdom as number | undefined;
    if (maxCount != null) {
      const count = (fiefdomData.buildings ?? []).filter(b => b.name === typeId).length;
      if (count >= maxCount) {
        return { valid: false, reason: formatManorText('ui_manor_max_reached') };
      }
    }
    const prereq = ((cfg.prerequisites as Array<Record<string, number>> | undefined)?.[0]) ?? {};
    for (const [key, reqLevel] of Object.entries(prereq)) {
      if (key === 'manor_level') continue;
      const have = satisfiedLevel(key);
      if (have < reqLevel) {
        return {
          valid: false,
          reason: formatManorText('ui_manor_need_building', {
            building: buildingLabel(key, key),
            level: reqLevel
          })
        };
      }
    }
    const ghostRect = getRect(gx, gy, cfg.width, cfg.height);
    for (const b of fiefdomData.buildings || []) {
      const existing = getCfg(b.name);
      if (!existing) continue;
      if (overlap(ghostRect, getRect(b.x, b.y, existing.width, existing.height))) {
        return {
          valid: false,
          reason: formatManorText('ui_manor_overlap', {
            building: buildingLabel(b.name, existing.display_name)
          })
        };
      }
    }
    for (const [rx, ry] of fiefdomData.river_cells ?? []) {
      if (overlap(ghostRect, getRect(rx, ry, 1, 1))) {
        return { valid: false, reason: formatManorText('ui_manor_on_river') };
      }
    }
    for (const [res, amt] of Object.entries(cfg.costs)) {
      const current = (fiefdomData as unknown as Record<string, number>)[res] || 0;
      if (current < amt) {
        return {
          valid: false,
          reason: formatManorText('ui_manor_not_enough_resource', {
            resource: resourceLabel(res),
            amount: amt
          })
        };
      }
    }
    const acres = cfg.arable_acres ?? 0;
    if (acres > 0 && (fiefdomData.arable_land?.available ?? 0) + 0.0001 < acres) {
      return {
        valid: false,
        reason: formatManorText('ui_manor_not_enough_arable', { acres })
      };
    }
    const forest = cfg.forest_acres ?? 0;
    if (forest > 0 && (fiefdomData.forest_land?.available ?? 0) + 0.0001 < forest) {
      return {
        valid: false,
        reason: formatManorText('ui_manor_not_enough_forest', { acres: forest })
      };
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
   * @returns String like "10s 10w" (gold in shillings/pence, wood= w, steel= stl);
   *          empty if no costs
   */
  function formatCost(costs: Record<string, number>): string {
    const units: Record<string, string> = {
      wood: 'w', steel: 'stl', bronze: 'brz',
      grain: 'g', leather: 'lth', mana: 'ma',
      charcoal: 'ch', iron: 'fe', ironwork: 'iw', fancy_ironwork: 'fi',
      beams: 'bm', boards: 'bd'
    };
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
   * mirroring the server's build checks: manor level, max_per_fiefdom, the
   * exclusive group (only one building from the group may exist), level-1
   * prerequisites (non-manor_level keys = required buildings), level-1 cost
   * affordability, and the arable/forest land gates.
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
      const have = satisfiedLevel(key);
      if (have < reqLevel) return false;
    }
    for (const [res, amt] of Object.entries(cfg.costs)) {
      const have = (fiefdomData as unknown as Record<string, number>)[res] ?? 0;
      if (have < amt) return false;
    }
    // Arable land: the new building must fit in the manor's remaining acres.
    const acres = cfg.arable_acres ?? 0;
    if (acres > 0) {
      const available = fiefdomData.arable_land?.available ?? 0;
      if (available + 0.0001 < acres) return false;
    }
    // Forest land: the wood producers claim off-map forest acres.
    const forest = cfg.forest_acres ?? 0;
    if (forest > 0) {
      const available = fiefdomData.forest_land?.available ?? 0;
      if (available + 0.0001 < forest) return false;
    }
    // Exclusive group: only one building from the group may exist at a time.
    const group = cfg.exclusive_group;
    if (group && isExclusiveGroupOccupied(group)) return false;
    return true;
  }

  /**
   * True when a building whose type belongs to `group` already exists in the
   * fiefdom. Drives both the palette hiding and the `canBuild` guard.
   *
   * @param group - The `exclusive_group` value from the building config
   * @returns True if any existing building shares the group
   */
  function isExclusiveGroupOccupied(group: string): boolean {
    if (!fiefdomData) return false;
    return (fiefdomData.buildings ?? []).some(
      (b) => getCfg(b.name)?.exclusive_group === group
    );
  }

  /**
   * The build-palette entries currently shown: every candidate whose manor
   * level is met and whose exclusive group is not already occupied, in
   * `manor_ui.json` build_order. Level-locked and exclusive-group-conflicting
   * types are hidden (not greyed); affordability/prerequisite/max-per-fiefdom
   * still grey out via `updateBuildButtonStates`.
   *
   * @returns Ordered list of visible building type ids
   */
  function computeBuildableIds(): string[] {
    if (!fiefdomData) return [];
    return candidateIds.filter((typeId) => {
      const cfg = getCfg(typeId);
      if (!cfg) return false;
      if (fiefdomData!.manor_level < (cfg.min_manor_level ?? 1)) return false;
      const group = cfg.exclusive_group;
      if (group && isExclusiveGroupOccupied(group)) return false;
      return true;
    });
  }

  /**
   * Reconciles the build column's children with `computeBuildableIds()`.
   * SimpleGame's `Column.layout()` sizes itself over all children (it does not
   * skip invisible ones), so hidden entries must be removed from the column
   * rather than toggled `visible`. Re-adds the wanted buttons in order so
   * build_order is preserved, then re-lays out, re-anchors the HUD, and
   * refreshes the grey-out states.
   *
   * @returns void - Updates the on-screen build column
   */
  function syncBuildColumn(): void {
    if (!buildCol) return;
    const wanted = computeBuildableIds();
    for (const child of [...buildCol.children]) buildCol.removeChild(child);
    for (const typeId of wanted) {
      const btn = buildButtons.get(typeId);
      if (btn) buildCol.addChild(btn);
    }
    buildCol.setGutter(10);
    buildCol.setPadding(0);
    buildCol.setJustify(LayoutJustify.START);
    buildCol.layout();
    apply_viewport();
    updateBuildButtonStates();
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
    channelTileClasses.clear();
    for (const obj of riverObjMap.values()) obj.destroy();
    riverObjMap.clear();
    for (const obj of waterBadgeMap.values()) obj.destroy();
    waterBadgeMap.clear();
    for (const obj of pondLabelMap.values()) obj.destroy();
    pondLabelMap.clear();
    underConstructionSet.clear();
  }

  /** Renders the fiefdom's river cells as static water tiles behind buildings. */
  function renderRiver() {
    if (!fiefdomData?.river_cells) return;
    if (!riverTileClass) riverTileClass = new ItemClass('river_tile', makeWaterTileDataUrl());
    for (const [x, y] of fiefdomData.river_cells) {
      const key = x + ',' + y;
      if (riverObjMap.has(key)) continue;
      const pos = g2b(x, y, 1, 1);
      const obj = riverTileClass.spawn(pos.x, pos.y);
      obj.width = 1 * CELL_TO_BOARD_UNITS;
      obj.height = 1 * CELL_TO_BOARD_UNITS;
      riverObjMap.set(key, obj);
    }
  }

  function renderBuildings() {
    if (!fiefdomData) return;
    renderRiver();
    const homeBasePlaced = fiefdomData.buildings?.some(b => b.name === 'home_base');

    for (const b of fiefdomData.buildings || []) {
      if (b.name === 'home_base') continue;
      const cfg = getCfg(b.name);
      if (!cfg) continue;

      const underConstruction = b.construction_start_ts > 0;

      // Roads/races auto-tile: pick the base image + rotation from orthogonal
      // same-type neighbors. Roads and tail races are instant (level >= 1);
      // head races build over 10s and show their construction variant first.
      if ((b.name === 'road' || b.name === 'head_race' || b.name === 'tail_race') && !underConstruction) {
        ensureChannelTileClasses(cfg, b.name);
        const resolved = resolveChannelTile(b.x, b.y, b.name, cfg, fiefdomData.buildings ?? []);
        const tileKey = b.name + ':' + resolved.key;
        let cls = channelTileClasses.get(tileKey);
        if (!cls) {
          cls = new ItemClass(
            b.name + '_' + resolved.key,
            b.name === 'road' ? makeRoadTileDataUrl(['e', 'w']) : makeRaceTileDataUrl(['e', 'w'], b.name as 'head_race' | 'tail_race')
          );
          channelTileClasses.set(tileKey, cls);
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

      // Completed buildings are clickable → building info card.
      if (b.level >= 1) {
        obj.onClick(0, () => openBuildingCard(b.id));
      }

      if (underConstruction) {
        underConstructionSet.add(b.id);
        const times = getConstructionTimes(b, cfg);
        const totalSec = times[b.level] || times[0] || 60;
        obj.var.construction_start = b.construction_start_ts;
        obj.var.construction_duration = totalSec;
        obj.setProgressBar(
          () => (Date.now() / 1000 - obj.var.construction_start) / obj.var.construction_duration,
          '#4caf50', '#333333', 0.9
        );
      }

      // Water-powered status badge: green dot = powered, red dot = unpowered.
      if (cfg.water_powered && !underConstruction) {
        const powered = !!(fiefdomData.water_power && fiefdomData.water_power[String(b.id)]);
        if (!poweredDotClass) poweredDotClass = new ItemClass('powered_dot', makeDotDataUrl('#2ecc40'));
        if (!unpoweredDotClass) unpoweredDotClass = new ItemClass('unpowered_dot', makeDotDataUrl('#ff4136'));
        const badgeCls = powered ? poweredDotClass : unpoweredDotClass;
        const center = g2b(b.x, b.y, cfg.width, cfg.height);
        const badge = badgeCls.spawn(center.x - cfg.width * CELL_TO_BOARD_UNITS / 2 + 14, center.y - cfg.height * CELL_TO_BOARD_UNITS / 2 + 14);
        badge.width = 24;
        badge.height = 24;
        badge.zIndex = 120;
        waterBadgeMap.set(b.id, badge);
      }

      // Mill pond label: current type + load/capacity (e.g. "Earthen · 1/2").
      if (b.name === 'mill_pond' && !underConstruction) {
        const pondTypes = cfg.pond_types ?? [];
        const storedType = b.pond_type && pondTypes.some(t => t.id === b.pond_type) ? b.pond_type : (pondTypes[0]?.id ?? 'earthen');
        const typeCfg = pondTypes.find(t => t.id === storedType) ?? pondTypes[0];
        const capacity = typeCfg?.capacity ?? 0;
        const load = (fiefdomData.water_power_detail?.pond_load?.[String(b.id)] ?? 0);
        const labelText = (storedType.charAt(0).toUpperCase() + storedType.slice(1)) + ' · ' + load + '/' + capacity;
        const center = g2b(b.x, b.y, cfg.width, cfg.height);
        const label = createText(labelText, { x: center.x, y: center.y + cfg.height * CELL_TO_BOARD_UNITS / 2 + 34 });
        label.size = 30;
        label.foreground = '#cfe8ff';
        label.setTextAlign('center');
        label.setShadow('#000000', 4, 1, 1);
        pondLabelMap.set(b.id, label);
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
        // The manor house is the way to raise manor_level — make it clickable.
        const homeBase = (fiefdomData.buildings || []).find(b => b.name === 'home_base');
        if (homeBase) {
          manorObj.onClick(0, () => openBuildingCard(homeBase.id));
        }
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
    ghostPreload = null;
    reevePlacementServer = null;

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
    ghostPreload = null;
    reevePlacementServer = null;
    if (ghostBuilding) { ghostBuilding.destroy(); ghostBuilding = null; }
    if (ghostOverlayValid) { ghostOverlayValid.destroy(); ghostOverlayValid = null; }
    if (ghostOverlayInvalid) { ghostOverlayInvalid.destroy(); ghostOverlayInvalid = null; }
    if (ghostTooltip) { ghostTooltip.destroy(); ghostTooltip = null; }
  }

  /**
   * Moves the placement ghost to a grid cell and refreshes its validity
   * overlay/tooltip. Extracted from the per-frame mouse tracking so the Reeve
   * can preload a suggested site.
   *
   * @param gx - Grid x of the ghost centre
   * @param gy - Grid y of the ghost centre
   * @param typeId - Building type being placed
   */
  function applyGhostPosition(gx: number, gy: number, typeId: string): void {
    const cfg = getCfg(typeId);
    if (!cfg) return;
    const pos = g2b(gx, gy, cfg.width, cfg.height);
    if (ghostBuilding) {
      ghostBuilding.x = pos.x;
      ghostBuilding.y = pos.y;
    }
    ghostPos = { gx, gy };
    if (ghostOverlayValid) { ghostOverlayValid.x = pos.x; ghostOverlayValid.y = pos.y; }
    if (ghostOverlayInvalid) { ghostOverlayInvalid.x = pos.x; ghostOverlayInvalid.y = pos.y; }

    const result = checkVal(gx, gy, typeId);
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

  /**
   * Starts placement with the ghost preloaded at the Reeve's suggested site.
   * The ghost stays there until the player moves the mouse, then behaves like
   * normal manual placement (drag/click to place, Esc to cancel).
   *
   * @param typeId - Building type to place
   * @param gx - Suggested grid x
   * @param gy - Suggested grid y
   */
  function startPlacementAt(typeId: string, gx: number, gy: number): void {
    const cfg = getCfg(typeId);
    if (!cfg) return;
    const center_gx = gx + Math.floor((cfg.width - 1) / 2);
    const center_gy = gy + Math.floor((cfg.height - 1) / 2);
    enterPlacement(typeId);
    if (!placementMode) return;
    applyGhostPosition(center_gx, center_gy, typeId);
    const mouse = getMousePosition();
    ghostPreload = { mouse_x: mouse.x, mouse_y: mouse.y, gx: center_gx, gy: center_gy };
    // The server treats (x, y) as the footprint's top-left, while the client
    // ghost is centred on a cell. Remember the server site so the preloaded
    // ghost builds exactly where the Reeve intended (3x3 producers are the
    // common case; 1x1 roads are identical in both systems).
    reevePlacementServer = { typeId, x: gx, y: gy, gx: center_gx, gy: center_gy };
  }

  /**
   * Shows the Reeve's road-link path as green overlays. The player confirms or
   * cancels from the Reeve panel; the path itself is one logical action.
   *
   * @param cells - Ordered road cells suggested by the Reeve
   */
  function startRoadLinkPreview(cells: Array<{ x: number; y: number }>): void {
    exitPlacement();
    cancelRoadLinkPreview();
    const overlayClass = validOverlayClass
      ?? createOverlayClass('rgba(0, 200, 0, 0.3)', 'v_overlay');
    validOverlayClass = overlayClass;
    for (const cell of cells) {
      const pos = g2b(cell.x, cell.y, 1, 1);
      const obj = overlayClass.spawn(pos.x, pos.y);
      obj.width = CELL_TO_BOARD_UNITS;
      obj.height = CELL_TO_BOARD_UNITS;
      obj.opacity = 0.5;
      obj.zIndex = 50;
      roadLinkOverlays.push(obj);
    }
    roadLinkPreview = cells.map((cell) => ({ x: cell.x, y: cell.y }));
  }

  /** Removes the Reeve's road-link preview overlays. */
  function cancelRoadLinkPreview(): void {
    for (const obj of roadLinkOverlays) obj.destroy();
    roadLinkOverlays = [];
    roadLinkPreview = null;
  }

  async function placeBuilding(gx: number, gy: number, typeId: string) {
    if (!fiefdomData) return;
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !$currentCharacter) return;
    let server_x = gx;
    let server_y = gy;
    if (reevePlacementServer &&
        reevePlacementServer.typeId === typeId &&
        reevePlacementServer.gx === gx &&
        reevePlacementServer.gy === gy) {
      server_x = reevePlacementServer.x;
      server_y = reevePlacementServer.y;
    }
    try {
      await buildRequest({
        fiefdom_id: fiefdomData.id,
        building_type: typeId,
        x: server_x, y: server_y,
        character_id: $currentCharacter.id
      }, { username: creds.username, token });
      await loadFiefdomData();
      clearBuildings();
      renderBuildings();
      exitPlacement();
      if (showReeve) await loadReeveReport();
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
      syncBuildColumn();
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
   * Opens the building info card for a completed building.
   *
   * @param id - The fiefdom_buildings row id
   */
  function openBuildingCard(id: number): void {
    if (showReeve) {
      showReeve = false;
      cancelRoadLinkPreview();
      updateToggleButtonStates();
    }
    selectedBuildingId = id;
    buildingCardError = '';
  }

  function closeBuildingCard(): void {
    selectedBuildingId = null;
    buildingCardError = '';
  }

  /**
   * The building currently shown on the info card (looked up from fiefdom data).
   */
  function selectedBuilding(): FiefdomBuilding | null {
    if (selectedBuildingId == null) return null;
    return (fiefdomData?.buildings ?? []).find(b => b.id === selectedBuildingId) ?? null;
  }

  /**
   * Whether the selected building can upgrade to its next level (completed and
   * below its max level).
   *
   * @param b - The building instance
   * @returns True if the Upgrade button should be enabled
   */
  function canUpgradeSelected(b: FiefdomBuilding): boolean {
    if (b.level <= 0) return false;
    const maxLevel = (getCfg(b.name)?.max_level as number | undefined) ?? 1;
    return b.level < maxLevel;
  }

  /**
   * Refreshes the board after a building action.
   */
  async function refreshAfterAction(): Promise<void> {
    await loadFiefdomData();
    clearBuildings();
    renderBuildings();
  }

  async function upgradeSelected(): Promise<void> {
    const b = selectedBuilding();
    if (!b || !fiefdomData) return;
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !$currentCharacter) return;
    buildingCardBusy = true;
    buildingCardError = '';
    try {
      await upgradeBuildingRequest({
        fiefdom_id: fiefdomData.id,
        building_id: b.id,
        character_id: $currentCharacter.id
      }, { username: creds.username, token });
      await refreshAfterAction();
    } catch (e) {
      buildingCardError = e instanceof Error ? e.message : 'Failed to upgrade';
    } finally {
      buildingCardBusy = false;
    }
  }

  async function convertSelected(): Promise<void> {
    const b = selectedBuilding();
    if (!b || !fiefdomData) return;
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !$currentCharacter) return;
    buildingCardBusy = true;
    buildingCardError = '';
    try {
      await convertBuildingRequest({
        fiefdom_id: fiefdomData.id,
        building_id: b.id,
        character_id: $currentCharacter.id
      }, { username: creds.username, token });
      await refreshAfterAction();
    } catch (e) {
      buildingCardError = e instanceof Error ? e.message : 'Failed to convert';
    } finally {
      buildingCardBusy = false;
    }
  }

  async function demolishSelected(): Promise<void> {
    const b = selectedBuilding();
    if (!b || !fiefdomData) return;
    const confirmText = manorTexts['ui_manor_demolish_confirm']
      .replace('{building_name}', getCfg(b.name)?.display_name ?? b.name);
    if (!window.confirm(confirmText)) return;
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !$currentCharacter) return;
    buildingCardBusy = true;
    buildingCardError = '';
    try {
      await demolishBuildingRequest({
        fiefdom_id: fiefdomData.id,
        building_id: b.id,
        character_id: $currentCharacter.id
      }, { username: creds.username, token });
      closeBuildingCard();
      await refreshAfterAction();
    } catch (e) {
      buildingCardError = e instanceof Error ? e.message : 'Failed to demolish';
    } finally {
      buildingCardBusy = false;
    }
  }

  async function upgradePondSelected(): Promise<void> {
    const b = selectedBuilding();
    if (!b || !fiefdomData) return;
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !$currentCharacter) return;
    buildingCardBusy = true;
    buildingCardError = '';
    try {
      await upgradePondTypeRequest({
        fiefdom_id: fiefdomData.id,
        building_id: b.id,
        character_id: $currentCharacter.id
      }, { username: creds.username, token });
      await refreshAfterAction();
    } catch (e) {
      buildingCardError = e instanceof Error ? e.message : 'Failed to upgrade pond type';
    } finally {
      buildingCardBusy = false;
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

  /**
   * Formats a road-morale points total as a production percentage.
   *
   * Uses the server-supplied economy multiplier so fractional bonuses (e.g.
   * 2.1%) display accurately instead of being rounded to a whole percent.
   *
   * @param points - Morale points received by a building
   * @returns String like "2" or "2.1" (the caller appends "%")
   */
  function formatMoralePercent(points: number): string {
    const multiplier = fiefdomData?.morale_production_multiplier ?? 0.02;
    const percent = Math.round(points * multiplier * 100 * 10) / 10;
    return Number.isInteger(percent) ? percent.toFixed(0) : percent.toFixed(1);
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
    if (retinueBtn) retinueBtn.setBackgroundColor(showRetinue ? MANOR_BTN_ACTIVE_COLOR : MANOR_BTN_COLOR);
    if (reeveBtn) reeveBtn.setBackgroundColor(showReeve ? MANOR_BTN_ACTIVE_COLOR : MANOR_BTN_COLOR);
  }

  async function toggleEconomy() {
    showEconomy = !showEconomy;
    if (showEconomy) {
      showProduction = false;
      showRetinue = false;
      showReeve = false;
      cancelRoadLinkPreview();
      await loadFiefdomData();
    }
    updateToggleButtonStates();
  }

  async function toggleProduction() {
    showProduction = !showProduction;
    if (showProduction) {
      showEconomy = false;
      showRetinue = false;
      showReeve = false;
      cancelRoadLinkPreview();
      await loadFiefdomData();
    }
    updateToggleButtonStates();
  }

  async function toggleRetinue() {
    showRetinue = !showRetinue;
    if (showRetinue) {
      showEconomy = false;
      showProduction = false;
      showReeve = false;
      cancelRoadLinkPreview();
    }
    updateToggleButtonStates();
  }

  /**
   * Loads the Reeve's structured report. Report/action failures are shown as
   * text ids, never as raw server English.
   */
  async function loadReeveReport(): Promise<void> {
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds || !$currentCharacter) return;
    reeveLoading = true;
    reeveError = '';
    try {
      const report = await getReeveReportRequest({
        fiefdom_id: fiefdomData?.id ?? 0,
        character_id: $currentCharacter.id
      }, { username: creds.username, token });
      reeveReport = report;
    } catch (e) {
      reeveError = 'reeve_error_load_failed';
      console.log('[ManorMenu] Reeve report failed:', e);
    } finally {
      reeveLoading = false;
    }
  }

  /** Toggles the Reeve panel and closes the other manor panels. */
  async function toggleReeve(): Promise<void> {
    showReeve = !showReeve;
    if (showReeve) {
      exitPlacement();
      showEconomy = false;
      showProduction = false;
      showRetinue = false;
      closeBuildingCard();
      reeveActionError = '';
      await loadReeveReport();
    } else {
      cancelRoadLinkPreview();
    }
    updateToggleButtonStates();
  }

  /**
   * Follows a report detail/section target: opens the relevant card or panel.
   *
   * @param target - Structured Reeve target
   */
  async function handleReeveJump(target: ReeveTarget): Promise<void> {
    cancelRoadLinkPreview();
    if (target.kind === 'building_card') {
      showReeve = false;
      updateToggleButtonStates();
      openBuildingCard(target.building_id);
      return;
    }
    if (target.kind === 'production_panel') {
      showReeve = false;
      showProduction = true;
      showEconomy = false;
      showRetinue = false;
      await loadFiefdomData();
      updateToggleButtonStates();
      return;
    }
    if (target.kind === 'economy_panel' || target.kind === 'resource') {
      showReeve = false;
      showEconomy = true;
      showProduction = false;
      showRetinue = false;
      await loadFiefdomData();
      updateToggleButtonStates();
      return;
    }
    if (target.kind === 'build_palette') {
      showReeve = false;
      if (buildCol) buildCol.visible = true;
      updateToggleButtonStates();
    }
  }

  /**
   * Takes the Reeve's recommended action through the standard endpoints, then
   * refreshes the manor and the report.
   *
   * @param action - Structured action to execute
   */
  async function handleReeveAccept(action: ReeveAction): Promise<void> {
    if (!fiefdomData || !$currentCharacter) return;
    const token = getSessionToken();
    const creds = getInMemoryCredentials();
    if (!token || !creds) return;
    reeveActionBusy = true;
    reeveActionError = '';
    try {
      const result = await execute_reeve_action(
        action, fiefdomData.id, $currentCharacter.id,
        { username: creds.username, token });
      if (!result.ok) {
        reeveActionError = reeve_error_text_id(result.error_code);
        return;
      }
      await loadFiefdomData();
      clearBuildings();
      renderBuildings();
      if (showReeve) await loadReeveReport();
    } catch (e) {
      reeveActionError = 'reeve_error_unknown';
      console.log('[ManorMenu] Reeve action failed:', e);
    } finally {
      reeveActionBusy = false;
    }
  }

  /**
   * Preloads the Reeve's suggested building ghost or road-link path.
   *
   * @param action - Build or build_road_link action
   */
  function handleReeveManualPlace(action: ReeveAction): void {
    reeveActionError = '';
    if (action.kind === 'build') {
      startPlacementAt(action.building_type, action.x, action.y);
      return;
    }
    if (action.kind === 'build_road_link') {
      startRoadLinkPreview(action.cells);
    }
  }

  /** Confirms the currently previewed road link. */
  async function confirmRoadLink(): Promise<void> {
    if (!roadLinkPreview) return;
    const cells = roadLinkPreview.map((cell) => ({ x: cell.x, y: cell.y }));
    await handleReeveAccept({ kind: 'build_road_link', cells });
    if (!reeveActionError) cancelRoadLinkPreview();
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

    for (const typeId of candidateIds) {
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

    syncBuildColumn();

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

    const rb = new ButtonClass('manor_retinue');
    retinueBtn = rb.spawn(0, 0, manorTexts['ui_manor_retinue'], null, toggleStyle);
    retinueBtn.hud = true;
    retinueBtn.visible = true;
    retinueBtn.zIndex = 100;
    retinueBtn.onClick(0, () => toggleRetinue());
    actionCol.addChild(retinueBtn);

    const rvb = new ButtonClass('manor_reeve');
    reeveBtn = rvb.spawn(0, 0, manorTexts['ui_manor_reeve'], null, toggleStyle);
    reeveBtn.hud = true;
    reeveBtn.visible = true;
    reeveBtn.zIndex = 100;
    reeveBtn.onClick(0, () => toggleReeve());
    actionCol.addChild(reeveBtn);

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

    // Roads and races render procedurally (canvas data URLs) until real tile
    // PNGs land, so point the ghost/palette art at a generated tile instead of
    // the PNG path.
    if (buildingConfigs['road']) {
      buildingConfigs['road'] = {
        ...buildingConfigs['road'],
        image: makeRoadTileDataUrl(['n', 'e', 's', 'w']),
        construction_image: makeRoadTileDataUrl(['n', 'e', 's', 'w'])
      };
    }
    for (const raceType of ['head_race', 'tail_race'] as const) {
      if (buildingConfigs[raceType]) {
        buildingConfigs[raceType] = {
          ...buildingConfigs[raceType],
          image: makeRaceTileDataUrl(['n', 'e', 's', 'w'], raceType),
          construction_image: makeRaceTileDataUrl(['n', 'e', 's', 'w'], raceType)
        };
      }
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
    // house is a leftover generic entry with no display_name/image, so the
    // display_name && image guard keeps it (and any placeholder) out of the palette.
    // `candidateIds` is every display-able type (level-locked ones included, so
    // their buttons/texts exist up front); the *visible* set is computed per
    // load by `computeBuildableIds()` (manor level + exclusive group), which is
    // reconciled into the column by `syncBuildColumn()`. Order comes from
    // manor_ui.json build_order; ids not listed sort last (stable).
    candidateIds = Object.entries(buildingConfigs)
      .filter(([id, cfg]) => id !== 'home_base' && cfg.display_name && cfg.image)
      .map(([id]) => id)
      .sort((a, b) => {
        const ia = buildingOrder.indexOf(a);
        const ib = buildingOrder.indexOf(b);
        return (ia === -1 ? Number.MAX_SAFE_INTEGER : ia) - (ib === -1 ? Number.MAX_SAFE_INTEGER : ib);
      });

    const textIds = [
      'manor_main_btn', 'ui_manor_build_btn',
      'ui_manor_economy', 'ui_manor_production', 'ui_manor_retinue',
      'ui_manor_reeve',
      'ui_manor_stockpiles', 'ui_manor_treasury', 'ui_manor_arable',
      'ui_manor_forest',
      'ui_manor_stage', 'ui_manor_upgrade', 'ui_manor_convert',
      'ui_manor_pond_type', 'ui_manor_demolish', 'ui_manor_demolish_confirm',
      'ui_manor_level', 'ui_manor_close', 'ui_manor_free',
      'ui_manor_outputs', 'ui_manor_inputs', 'ui_manor_input_satisfaction',
      'ui_manor_no_outputs', 'ui_manor_water_unpowered', 'ui_manor_rate',
      'ui_manor_net_gold', 'ui_manor_net_silver', 'ui_manor_produced',
      'ui_manor_consumed', 'ui_manor_imported', 'ui_manor_exported',
      'ui_manor_no_economy', 'ui_manor_auto_import', 'ui_manor_auto_import_hint',
      'ui_manor_reserves', 'ui_manor_reserves_hint', 'ui_manor_reserves_hint2',
      'ui_manor_set', 'ui_manor_production_title', 'ui_manor_road_morale',
      'ui_manor_no_outputs_adjustable', 'reeve_open_hint',
      'ui_manor_no_fiefdom', 'ui_manor_unknown_building',
      'ui_manor_need_manor_level', 'ui_manor_max_reached',
      'ui_manor_need_building', 'ui_manor_overlap', 'ui_manor_on_river',
      'ui_manor_not_enough_resource', 'ui_manor_not_enough_arable',
      'ui_manor_not_enough_forest',
      'ui_resource_gold', 'ui_resource_silver_pence', 'ui_resource_grain',
      'ui_resource_wood', 'ui_resource_steel', 'ui_resource_bronze',
      'ui_resource_leather', 'ui_resource_mana', 'ui_resource_charcoal',
      'ui_resource_iron', 'ui_resource_ironwork', 'ui_resource_fancy_ironwork',
      'ui_resource_beams', 'ui_resource_boards', 'ui_resource_stone',
      'ui_building_home_base',
      ...candidateIds.map(id => 'ui_building_' + id)
    ];
    manorTexts = await loadTexts(textIds);

    debugDiv = document.createElement('div');
    initEngine(canvasEl, debugDiv, false, setupGame);
    setBoardSize(BW, BH);
    setBackground([asset_url('/images/manor/ground/grass.jpg')]);
    setBackgroundMode('tile');
    setBackgroundTileSize(GRASS_TILE, GRASS_TILE);
    setCameraFollowsPlayer(false);
    setBoardPanEnabled(true);

    apply_viewport();
    setCameraPosition(CX, CY);
    window.addEventListener('resize', apply_viewport);
    onKeyDown('Escape', () => {
      exitPlacement();
      cancelRoadLinkPreview();
    });

    everyTick(() => {
      if (ghostBuilding && placementType) {
        const mouse = getMousePosition();
        if (ghostPreload) {
          const dx = mouse.x - ghostPreload.mouse_x;
          const dy = mouse.y - ghostPreload.mouse_y;
          if (Math.hypot(dx, dy) < 2) {
            applyGhostPosition(ghostPreload.gx, ghostPreload.gy, placementType);
          } else {
            ghostPreload = null;
          }
        }
        if (!ghostPreload) {
          const snapped = b2g(mouse.x, mouse.y);
          applyGhostPosition(snapped.gx, snapped.gy, placementType);
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
    cancelRoadLinkPreview();
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
    {#if showRetinue}
      <div class="card position-absolute end-0 m-3" style="width: 420px; max-height: 80vh; overflow-y: auto; top: {panelTop}px;">
        <div class="card-body">
          <h6 class="card-title">{manorTexts['ui_manor_retinue']}</h6>
          {#if $currentCharacter}
            <RetinuePanel characterId={$currentCharacter.id} />
          {/if}
        </div>
      </div>
    {/if}
    {#if showReeve}
      <div class="card position-absolute end-0 m-3" style="width: 460px; max-height: 80vh; overflow-y: auto; top: {panelTop}px; z-index: 160;">
        <div class="card-body">
          <ReevePanel
            report={reeveReport}
            loading={reeveLoading}
            error={reeveError}
            action_error={reeveActionError}
            action_busy={reeveActionBusy}
            road_link_preview={roadLinkPreview !== null}
            on_close={() => {
              showReeve = false;
              cancelRoadLinkPreview();
              updateToggleButtonStates();
            }}
            on_refresh={loadReeveReport}
            on_jump={handleReeveJump}
            on_accept={handleReeveAccept}
            on_manual_place={handleReeveManualPlace}
            on_confirm_road_link={confirmRoadLink}
            on_cancel_road_link={cancelRoadLinkPreview}
          />
        </div>
      </div>
    {/if}
    {#if showEconomy}
      <div class="card position-absolute end-0 m-3" style="width: 420px; max-height: 80vh; overflow-y: auto; top: {panelTop}px;">
          <div class="card-body">
            <h6 class="card-title">{manorTexts['ui_manor_economy']}</h6>

          <div class="mb-3">
            <div class="fw-semibold">{manorTexts['ui_manor_stockpiles']}</div>
            <div class="small">{manorTexts['ui_manor_treasury']}: {formatGold(fiefdomData?.gold ?? 0)}, {fiefdomData?.silver_pence ?? 0} {resourceLabel('silver_pence')}</div>
            <div class="small">{manorTexts['ui_manor_arable']}: {Math.floor(fiefdomData?.arable_land?.used ?? 0)} / {Math.floor(fiefdomData?.arable_land?.total ?? 0)}</div>
            <div class="small">{manorTexts['ui_manor_forest']}: {Math.floor(fiefdomData?.forest_land?.used ?? 0)} / {Math.floor(fiefdomData?.forest_land?.total ?? 0)}</div>
            <div class="d-flex flex-wrap gap-1">
              {#each IMPORT_RESOURCES as res}
                {@const amount = ((fiefdomData ?? {}) as unknown as Record<string, number>)[res] ?? 0}
                <span class="badge text-bg-secondary">{resourceLabel(res)}: {amount}</span>
              {/each}
            </div>
          </div>

          {#if economyReport}
            <div class="mb-3">
              <div class="fw-semibold">{manorTexts['ui_manor_net_gold']}: {economyReport.net_gold.toFixed(2)}</div>
              {#if economyReport.net_silver != null}
                <div class="fw-semibold">{manorTexts['ui_manor_net_silver']}: {economyReport.net_silver}d</div>
              {/if}
              {#if Object.keys(economyReport.produced).length > 0}
                <div class="small text-muted">
                  {manorTexts['ui_manor_produced']}: {Object.entries(economyReport.produced).map(([r, a]) => `${resourceLabel(r)} ${a.toFixed(1)}`).join(', ')}
                </div>
              {/if}
              {#if Object.keys(economyReport.consumed).length > 0}
                <div class="small text-muted">
                  {manorTexts['ui_manor_consumed']}: {Object.entries(economyReport.consumed).map(([r, a]) => `${resourceLabel(r)} ${a.toFixed(1)}`).join(', ')}
                </div>
              {/if}
              {#if Object.keys(economyReport.imported).length > 0}
                <div class="small text-muted">
                  {manorTexts['ui_manor_imported']}: {Object.entries(economyReport.imported).map(([r, a]) => `${resourceLabel(r)} ${a.toFixed(1)}`).join(', ')}
                </div>
              {/if}
              {#if Object.keys(economyReport.exported).length > 0}
                <div class="small text-muted">
                  {manorTexts['ui_manor_exported']}: {Object.entries(economyReport.exported).map(([r, v]) => v.pence != null ? `${resourceLabel(r)} ${v.amount.toFixed(1)} (${v.pence}d)` : `${resourceLabel(r)} ${v.amount.toFixed(1)} (${v.gold.toFixed(1)}g)`).join(', ')}
                </div>
              {/if}
            </div>

            <div class="mb-3">
              <button class="btn btn-sm btn-outline-light" onclick={toggleReeve}>
                {manorTexts['reeve_open_hint']}
              </button>
            </div>
          {:else}
            <p class="small text-muted">{manorTexts['ui_manor_no_economy']}</p>
          {/if}

          <hr />
          <div class="fw-semibold mb-2">{manorTexts['ui_manor_auto_import']}</div>
          <div class="d-flex flex-wrap gap-2">
            {#each IMPORT_RESOURCES as resource}
              <button
                class="btn btn-sm {fiefdomData?.import_settings?.[resource] ? 'btn-primary' : 'btn-outline-secondary'}"
                onclick={() => toggleImport(resource)}
              >
                {resourceLabel(resource)}
              </button>
            {/each}
          </div>
          <div class="small text-muted mt-2">
            {manorTexts['ui_manor_auto_import_hint']}
          </div>

          <hr />
          <div class="fw-semibold mb-2">{manorTexts['ui_manor_reserves']}</div>
          <div class="small text-muted mb-2">{manorTexts['ui_manor_reserves_hint']}</div>
          <div class="d-flex flex-wrap gap-2 align-items-center">
            {#each IMPORT_RESOURCES as resource}
              <div class="d-flex align-items-center gap-1">
                <span class="small">{resourceLabel(resource)}:</span>
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
                  {manorTexts['ui_manor_set']}
                </button>
              </div>
            {/each}
          </div>
          <div class="small text-muted mt-2">
            {manorTexts['ui_manor_reserves_hint2']}
          </div>
        </div>
      </div>
    {/if}

    {#if showProduction}
      <div class="card position-absolute end-0 m-3" style="width: 420px; max-height: 80vh; overflow-y: auto; top: {panelTop}px;">
        <div class="card-body">
          <h6 class="card-title">{manorTexts['ui_manor_production_title']}</h6>
          {#each fiefdomData?.buildings ?? [] as building}
            {@const cfg = buildingConfigs[building.name]}
            {@const outputs = (cfg?.outputs ?? []).filter(o => building.level >= (o.min_level ?? 1))}
            {#if outputs.length > 0}
              <div class="mb-3">
                <div class="fw-semibold small">{buildingLabel(building.name, cfg?.display_name ?? building.name)} (L{building.level})</div>
                {#if (fiefdomData?.road_morale?.[building.id] ?? 0) > 0}
                  <div class="small text-success">
                    {(manorTexts['ui_manor_road_morale'] ?? '').replace('{percent}', formatMoralePercent(fiefdomData?.road_morale?.[building.id] ?? 0))}
                  </div>
                {/if}
                {#each outputs as output}
                  <div class="d-flex align-items-center gap-2 mb-1">
                    <span class="small flex-shrink-0" style="width: 110px;">{resourceLabel(output.resource)}</span>
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
            <p class="small text-muted">{manorTexts['ui_manor_no_outputs_adjustable']}</p>
          {/if}
        </div>
      </div>
    {/if}

    {#if selectedBuildingId != null}
      {@const sb = selectedBuilding()}
      {@const sCfg = sb ? getCfg(sb.name) : undefined}
      {@const sSucc = sb ? stageSuccessor(sb.name) : undefined}
      {@const sSuccCfg = sSucc ? getCfg(sSucc) : undefined}
      {@const sMax = (sCfg?.max_level as number | undefined) ?? 5}
      {@const sStatus = sb
        ? fiefdomData?.economy_report?.building_status?.find(s => s.building_id === sb.id)
        : undefined}
      {#if sb && sCfg}
        <div class="card position-absolute end-0 m-3" style="width: 380px; top: {panelTop}px; z-index: 150;">
          <div class="card-body">
            <div class="d-flex justify-content-between align-items-start gap-2">
              <div>
                <h6 class="card-title mb-1">{buildingLabel(sb.name, sCfg.display_name)}</h6>
                <div class="small text-muted">
                  {manorTexts['ui_manor_stage']}: {stageChain(sb.name).length}/{fullChainLength(sb.name)}
                  &middot; {manorTexts['ui_manor_level']} {sb.level}/{sMax}
                </div>
              </div>
              <button class="btn-close" onclick={closeBuildingCard} aria-label={manorTexts['ui_manor_close']}></button>
            </div>

            {#if sStatus}
              <div class="mt-2">
                <div class="fw-semibold small">{manorTexts['ui_manor_outputs']}</div>
                {#if sStatus.outputs.length > 0}
                  {#each sStatus.outputs as out}
                    <div class="small">
                      <span class="badge text-bg-secondary">{resourceLabel(out.resource)}</span>
                      {out.amount.toFixed(1)}/day
                      {#if out.rate < 0.999}
                        ({Math.round(out.rate * 100)}%)
                      {/if}
                      {#if out.input_satisfaction < 0.999}
                        <span class="text-warning">{Math.round(out.input_satisfaction * 100)}%</span>
                      {/if}
                    </div>
                    {#if out.inputs.length > 0}
                      <div class="small text-muted">
                        {manorTexts['ui_manor_inputs']}:
                        {out.inputs.map(i => `${resourceLabel(i.resource)} ${i.supplied.toFixed(1)}/${i.required.toFixed(1)}`).join(', ')}
                      </div>
                    {/if}
                  {/each}
                {:else}
                  <div class="small text-muted">{manorTexts['ui_manor_no_outputs']}</div>
                {/if}
                {#if !sStatus.water_powered}
                  <div class="small text-warning">{manorTexts['ui_manor_water_unpowered']}</div>
                {/if}
              </div>
            {/if}

            {#if buildingCardError}
              <div class="alert alert-danger py-1 px-2 small mt-2 mb-2">{buildingCardError}</div>
            {/if}

            <div class="d-grid gap-2 mt-3">
              {#if canUpgradeSelected(sb)}
                <button class="btn btn-primary btn-sm" disabled={buildingCardBusy} onclick={upgradeSelected}>
                  {manorTexts['ui_manor_upgrade']}
                </button>
              {/if}

              {#if sSucc && sSuccCfg}
                <button class="btn btn-warning btn-sm" disabled={buildingCardBusy} onclick={convertSelected}>
                  {manorTexts['ui_manor_convert']}: {buildingLabel(sSucc, sSuccCfg.display_name)} ({formatCost(convertCost(sb)) || manorTexts['ui_manor_free']})
                </button>
              {/if}

              {#if sb.name === 'mill_pond'}
                <button class="btn btn-info btn-sm" disabled={buildingCardBusy} onclick={upgradePondSelected}>
                  {manorTexts['ui_manor_pond_type']}
                </button>
              {/if}

              <button class="btn btn-outline-danger btn-sm" disabled={buildingCardBusy} onclick={demolishSelected}>
                {manorTexts['ui_manor_demolish']}
              </button>
            </div>
          </div>
        </div>
      {/if}
    {/if}
  {/if}
</div>
