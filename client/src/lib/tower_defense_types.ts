/**
 * Tower Defense type definitions for runtime data stored on SimpleGame objects.
 *
 * SimpleGame provides `GameObject.var: any` as the official user-data bag.
 * These interfaces give typed access to that data without `as any` casts.
 */

import type { Enemy, GameObject } from '../../SimpleGame/ui/src/lib/gameclasses';

// ================================================================
// Runtime data stored in GameObject.var
// ================================================================

export interface EnemyVar {
  hp: number;
  maxHp: number;
  enemyId: string;
  slowFactor: number;
  baseSpeed: number;
  /** Engine velocity from moveTo, used to re-apply slow each frame */
  baseVelocity: number;
  waypointIndex: number;
  pathId: string;
}

export interface ProjectileVar {
  targetX: number;
  targetY: number;
  aeCfg?: AreaEffectConfig;
  dmg: number;
  aoe: number;
  isOrbital: boolean;
  hitEnemies: Set<Enemy>;
}

export interface PoolObjVar {
  poolData: {
    id: string;
    slowFactor: number;
    damagePerSecond: number;
  };
  damageMap: Map<GameObject, number>;
}

export interface DragObjVar {
  _dragOrigX: number;
  _dragOrigY: number;
}

// ================================================================
// Config types (replaces (cls as any).pieceConfig / .mobConfig / .config)
// ================================================================

export interface MobConfig {
  speed?: number;
  hp?: number;
  width?: number;
  height?: number;
  forward_vector?: [number, number];
  sprite_up_vector?: [number, number];
  image_url?: string;
  reward_gold?: number;
  spawn_on_death?: string[];
  [key: string]: unknown;
}

export interface ProjectileConfig {
  name?: string;
  width?: number;
  height?: number;
  speed?: number;
  image_file?: string;
  forward_vector?: [number, number];
  sprite_up_vector?: [number, number];
  orbit_radius?: number;
  arc_degrees?: number;
  fade_in_ms?: number;
  fade_out_ms?: number;
  multi_hit?: boolean;
  area_effect?: AreaEffectConfig;
  [key: string]: unknown;
}

export interface AreaEffectConfig {
  id: string;
  image_file: string;
  opacity: number;
  duration_ms: number;
  slow_factor: number;
  damage_per_second: number;
  damage_type: string | null;
  radius: number;
  z_index?: number;
  [key: string]: unknown;
}

export interface PieceConfig {
  name?: string;
  cost?: { gold: number }[];
  damage?: number[];
  attack_rate?: number;
  range?: number;
  exclusion_radius?: number;
  width?: number;
  height?: number;
  image_url?: string;
  becomes?: string | null;
  upgrade_cost?: { gold: number }[];
  projectile_ids?: string[];
  projectile_id?: string;
  default_targeting?: string;
  area_of_effect?: { radius: number };
  abilities?: string[];
  [key: string]: unknown;
}

// ================================================================
// Server response shapes (fully typed — one cast per entry point)
// ================================================================

export interface KickoffMobs {
  mobs: Record<string, MobConfig>;
}

export interface KickoffProjectiles {
  projectiles: Record<string, ProjectileConfig>;
}

export interface KickoffTowers {
  towers: Record<string, PieceConfig>;
}

export interface KickoffUnits {
  units: Record<string, PieceConfig>;
}

export interface MapMetadata {
  image_filename?: string;
  paths?: {
    id: string;
    waypoints: { x: number; y: number }[];
  }[];
  intersections?: {
    id: string;
    branches: { path_id: string; weight: number }[];
  }[];
  exclusion_zones?: unknown[];
  spawn_points?: {
    id: string;
    x: number;
    y: number;
    target_path_id: string;
  }[];
  [key: string]: unknown;
}

// ================================================================
// HUD theme (server-authored, Tower Defense-scoped)
// ================================================================

/** Semantic HUD text surfaces referenced by the Tower Defense theme. */
export type td_ui_surface =
  | 'copper_readout'
  | 'cost_badge'
  | 'lives_readout'
  | 'picker_label'
  | 'context_button'
  | 'wave_button'
  | 'forfeit_button'
  | 'help_button'
  | 'menu_label';

/** Outline paint for one HUD ink role. All dimensions are canvas units. */
export interface td_outline_style {
  color: string;
  width: number;
  opacity?: number;
  line_join?: CanvasLineJoin;
}

/** Drop-shadow paint for one HUD ink role. All dimensions are canvas units. */
export interface td_shadow_style {
  color: string;
  offset_x: number;
  offset_y: number;
  blur: number;
  opacity?: number;
}

/** One semantic text-ink treatment. */
export interface td_ink_style {
  color: string;
  outline: td_outline_style;
  shadow?: td_shadow_style;
}

/** Tower Defense HUD theme. Scoped per mini-game; there is no global theme. */
export interface td_ui_theme {
  ink: Record<string, td_ink_style>;
  surfaces: Record<td_ui_surface, string>;
}

/** Fully-typed view of the server kickoff response, discarding the `[key: string]: unknown` indexer. */
export interface KickoffResponse {
  session_id: number;
  character_id: number;
  mini_game: string;
  level_id: number;
  difficulty: number;
  round_number: number;
  total_rounds: number;
  lives: number;
  gold: number;
  cost_suffix?: string;
  spawn_schedule: {
    enemy_id: string;
    count: number;
    interval_ms: number;
    initial_delay_ms: number;
    spawn_point_id?: string;
  }[];
  map_metadata?: MapMetadata;
  mobs?: KickoffMobs;
  projectiles?: KickoffProjectiles;
  towers?: KickoffTowers;
  units?: KickoffUnits;
  ui_theme?: td_ui_theme;
  resumed?: boolean;
}

export interface AdvanceResponse {
  next_round: boolean;
  round_number: number;
  total_rounds: number;
  spawn_schedule: {
    enemy_id: string;
    count: number;
    interval_ms: number;
    initial_delay_ms: number;
    spawn_point_id?: string;
  }[];
  lives: number;
  gold: number;
  won?: boolean;
  completed?: boolean;
  score?: number;
  new_best_score?: number;
  times_played?: number;
  all_levels_done?: boolean;
  base_unlocked?: boolean;
  game_phase?: string;
  next_level_id?: number | null;
  rewards?: Record<string, number>;
  completion_bonus?: Record<string, number>;
  land_patent_earned?: boolean;
  baron_right_earned?: boolean;
  new_unlocks?: unknown;
  silver_formatted?: string;
  [key: string]: unknown;
}
