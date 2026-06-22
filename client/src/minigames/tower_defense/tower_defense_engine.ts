/**
 * Tower Defense engine stub.
 * 
 * This is a minimal engine that renders a lane-based layout.
 * The actual game logic (waves, mobs, towers, projectiles, upgrades)
 * will be implemented later once the level specification format and
 * game assets (mobs, towers, players, maps) are designed.
 */

import type { TowerDefenseLevelConfig } from './tower_defense_types';

export interface TowerDefenseEngineState {
  config: TowerDefenseLevelConfig;
  width: number;
  height: number;
  finished: boolean;
  won: boolean;
  score: number;
}

/**
 * Creates a new tower defense engine state.
 * Initialized with the server-provided level config.
 * 
 * @param config - Level configuration from the server
 * @param width - Canvas width
 * @param height - Canvas height
 * @returns TowerDefenseEngineState - Initial engine state
 */
export function createEngine(
  config: TowerDefenseLevelConfig,
  width: number,
  height: number
): TowerDefenseEngineState {
  return {
    config,
    width,
    height,
    finished: false,
    won: false,
    score: 0
  };
}

/**
 * Renders a single frame of the tower defense game.
 * Currently draws a simple lane placeholder.
 * TODO: Replace with actual game rendering (mobs, towers, projectiles, UI).
 * 
 * @param ctx - Canvas 2D rendering context
 * @param state - Current engine state
 */
export function renderFrame(
  ctx: CanvasRenderingContext2D,
  state: TowerDefenseEngineState
): void {
  ctx.clearRect(0, 0, state.width, state.height);

  const laneCount = state.config.lane_count || 1;
  const laneHeight = state.height / (laneCount + 1);
  const laneWidth = state.width * 0.8;

  ctx.strokeStyle = '#666';
  ctx.lineWidth = 2;
  ctx.fillStyle = '#2a2a2a';

  for (let i = 0; i < laneCount; i++) {
    const y = laneHeight * (i + 1) - 30;
    const x = state.width * 0.1;

    ctx.fillRect(x, y, laneWidth, 60);
    ctx.strokeRect(x, y, laneWidth, 60);

    ctx.fillStyle = '#888';
    ctx.font = '14px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(`Lane ${i + 1}`, state.width / 2, y + 38);
    ctx.fillStyle = '#2a2a2a';
  }

  ctx.fillStyle = '#555';
  ctx.font = '12px sans-serif';
  ctx.textAlign = 'center';
  ctx.fillText(
    `Difficulty ${state.config.difficulty} | Waves: ${state.config.num_waves}`,
    state.width / 2,
    20
  );

  ctx.fillStyle = '#777';
  ctx.fillText(
    'Tower Defense — content coming soon',
    state.width / 2,
    state.height - 10
  );
}

/**
 * Updates the engine state each tick.
 * Stub: currently does nothing since no game logic is implemented.
 * TODO: Process wave timers, mob movement, tower targeting, etc.
 * 
 * @param state - Current engine state
 * @param _deltaMs - Time elapsed since last tick in milliseconds
 * @returns TowerDefenseEngineState - Updated state
 */
export function updateEngine(
  state: TowerDefenseEngineState,
  _deltaMs: number
): TowerDefenseEngineState {
  // Stub: no gameplay logic yet
  return state;
}
