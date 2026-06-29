/**
 * Tower Defense type definitions matching the map metadata format (v1.0).
 * Normalized coordinates range 0-1 where (0,0) is top-left of the map image.
 */

export interface Waypoint {
  x: number;
  y: number;
}

export interface SpawnPoint {
  id: string;
  label: string;
  x: number;
  y: number;
  interval_ms: number;
  initial_delay_ms: number;
  target_path_id: string;
}

export interface Path {
  id: string;
  label: string;
  waypoints: Waypoint[];
  end_at_intersection_id?: string | null;
  end_at_end_point_id?: string | null;
}

export interface IntersectionBranch {
  path_id: string;
  weight: number;
}

export interface Intersection {
  id: string;
  label: string;
  x: number;
  y: number;
  branches: IntersectionBranch[];
}

export interface EndPoint {
  id: string;
  label: string;
  x: number;
  y: number;
}

export interface PolygonExclusion {
  id: string;
  label: string;
  type: 'polygon';
  vertices: Waypoint[];
}

export interface CircleExclusion {
  id: string;
  label: string;
  type: 'circle';
  center_x: number;
  center_y: number;
  radius: number;
}

export type ExclusionZone = PolygonExclusion | CircleExclusion;

export interface MapMetadata {
  format_version: string;
  name: string;
  image_filename: string;
  spawn_points: SpawnPoint[];
  paths: Path[];
  intersections: Intersection[];
  exclusion_zones: ExclusionZone[];
  end_points: EndPoint[];
}

export interface MobConfig {
  name: string;
  description: string;
  hp: number;
  speed: number;
  reward_gold: number;
  image_file: string;
  spawn_on_death: string[];
}

export interface TowerDefenseAbility {
  name: string;
  damage: number;
  area_of_effect: boolean | { radius: number };
  cost_to_equip: Record<string, number>;
  spawns: { id: string; duration_ms: number } | null;
  description: string;
}

export interface TowerDefensePiece {
  name: string;
  description: string;
  cost: Record<string, number>[];
  damage: number[];
  area_of_effect: boolean | { radius: number };
  attack_rate: number;
  range: number;
  attack_spawns: { id: string; duration_ms: number } | null;
  upgrade_cost: Record<string, number>[];
  abilities: TowerDefenseAbility[];
  becomes: string | null;
}

export interface TowerDefenseLevelConfig {
  mini_game: string;
  level_id: number;
  map: string;
  difficulty: number;
  reward?: Record<string, number>;
  num_waves?: number;
  map_metadata?: MapMetadata;
  mobs?: {
    format_version: string;
    mobs: Record<string, MobConfig>;
  };
  towers?: {
    format_version: string;
    towers: Record<string, TowerDefensePiece>;
  };
  units?: {
    format_version: string;
    units: Record<string, TowerDefensePiece>;
  };
  [key: string]: unknown;
}

export interface EnemyState {
  id: string;
  path_id: string;
  waypoint_index: number;
  x: number;
  y: number;
  speed: number;
  progress: number;
}
