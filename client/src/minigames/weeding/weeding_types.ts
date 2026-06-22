/**
 * Weeding mini-game type definitions — stub.
 * 
 * TODO: Define types once the weeding game mechanics are designed.
 */

export interface WeedingLevelConfig {
  mini_game: string;
  level_id: number;
  map: string;
  difficulty: number;
  grid_size: number;
  weed_density: number;
  rewards?: Record<string, number>;
  [key: string]: unknown;
}
