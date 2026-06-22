/**
 * Tower Defense type definitions.
 * 
 * All types are placeholders ready for the real level specification format
 * and game mechanics (mobs, towers, projectiles, upgrades, etc.)
 */

/**
 * Represents a single lane in the tower defense map.
 * Lanes are horizontal paths that enemies traverse from left to right.
 */
export interface Lane {
  row: number;
  length: number;
  /** Reserved: waypoints for path curvature */
  waypoints?: Array<{ x: number; y: number }>;
}

/**
 * Configuration for a wave of enemies.
 * 
 * TODO: Populate with mob types, count, spawn interval, etc.
 * once the level specification format is designed.
 */
export interface WaveConfig {
  wave_number: number;
  /** Reserved: enemy type identifiers */
  enemy_types: string[];
  /** Reserved: number of enemies per type */
  counts: number[];
  /** Reserved: spawn delay between enemies in ms */
  spawn_interval_ms: number;
}

/**
 * Tower placement slot on the map.
 * 
 * TODO: Populate with tower type, cost, range, damage, etc.
 * once the tower configuration is defined.
 */
export interface TowerSlot {
  x: number;
  y: number;
  /** Reserved: which tower types can be placed here */
  allowed_types: string[];
}

/**
 * Line-based tower defense level configuration returned by the server.
 * 
 * TODO: Extend with actual mob/tower data once the format is designed.
 */
export interface TowerDefenseLevelConfig {
  mini_game: string;
  level_id: number;
  map: string;
  difficulty: number;
  num_waves: number;
  lane_count: number;
  enemy_types: string[];
  rewards?: Record<string, number>;
  lanes?: Lane[];
  tower_slots?: TowerSlot[];
  waves?: WaveConfig[];
  [key: string]: unknown;
}
