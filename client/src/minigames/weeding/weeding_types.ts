export interface WeedingSessionState {
  session_id: number;
  character_id: number;
  level_id: number;
  state: 'active' | 'won' | 'forfeited';
  board: GridSquare[][];
  grid_size: number;
  round: number;
  actions_remaining: number;
  pending_switch: boolean;
  equipped_tool: string | null;
  available_tools: RawToolConfig[];
  available_plants: RawPlantConfig[];
  available_specials: RawSpecialConfig[];
  map_metadata: RawWeedingMapConfig;
  par: number;
  won: boolean;
  score: number;
  message: string;
}

export interface GridSquare {
  plant_type: string | null;
  progress: number;
  actions_needed: number;
  is_smother_crop: boolean;
  is_blocked: boolean;
  is_accessible: boolean;
}

export interface WeedingActionItem {
  action_type: 'use_tool' | 'switch_tool' | 'plant';
  tool_id?: string;
  target_x?: number;
  target_y?: number;
  crop_id?: string;
}

export interface WeedingTurnRequest {
  action_type?: 'forfeit';
  character_id?: number;
  session_id?: number;
  actions?: WeedingActionItem[];
  tool_id?: string;
  target_x?: number;
  target_y?: number;
}

export interface WeedingTurnResponse {
  round: number;
  actions_remaining: number;
  pending_switch: boolean;
  equipped_tool: string | null;
  board_changes: BoardChange[];
  board: GridSquare[][];
  won: boolean;
  score: number;
  message: string;
  par: number;
  // Forfeit/completion fields
  game_over?: boolean;
  completed?: boolean;
  new_best_score?: number;
  times_played?: number;
  all_levels_done?: boolean;
  game_phase?: string;
  rewards?: Record<string, number>;
  completion_bonus?: Record<string, number>;
  land_patent_earned?: boolean;
  duke_right_earned?: boolean;
  next_level_id?: number | null;
}

export interface BoardChange {
  x: number;
  y: number;
  plant_type: string | null;
  progress: number;
  actions_needed: number;
  is_smother_crop: boolean;
  is_accessible: boolean;
  is_blocked?: boolean;
}

export interface RawPlantConfig {
  id: string;
  display_name_key: string;
  is_smother_crop: boolean;
  spread_probability: number;
  hp?: number;
  damage_sprites?: number[];
  sprite: PlantSpriteConfig;
  tools: Record<string, ToolEffectConfig>;
}

export interface RawToolConfig {
  id: string;
  display_name_key: string;
  sprite: { image_file: string };
  animation: ToolAnimConfig;
  can_plant: boolean;
  plant_crop_id: string | null;
}

export interface RawSpecialConfig {
  id: string;
  display_name_key: string;
  description_key: string;
  sprite: Record<string, never>;
  effect: string;
  available_condition: string;
}

export interface RawWeedingMapConfig {
  id: string;
  image_file: string;
  grid_bounds: GridBounds;
  grid_size: number;
  out_of_bounds: OobSquare[];
  par: number;
}

export interface PlantSpriteConfig {
  image_file: string;
  render_width: number;
  render_height: number;
  center_x: number;
  center_y: number;
  overlap_x: number;
  overlap_y: number;
  z_index: number;
}

export interface ToolAnimConfig {
  type: 'arc' | 'straight';
  arc_angle: number;
  distance: number;
  repetitions: number;
  duration_ms: number;
}

export interface ToolEffectConfig {
  damage: number;
  affects_adjacent: boolean;
  adjacent_mode?: 'row_or_column';
}

export interface GridBounds {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface OobSquare {
  x: number;
  y: number;
}
