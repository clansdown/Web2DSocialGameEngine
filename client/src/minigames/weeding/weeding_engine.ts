/**
 * Weeding mini-game engine — stub.
 * 
 * TODO: Implement actual weeding/ground-clearing game logic.
 */

import type { WeedingLevelConfig } from './weeding_types';

export interface WeedingEngineState {
  config: WeedingLevelConfig;
  width: number;
  height: number;
  finished: boolean;
  won: boolean;
  score: number;
}

export function createWeedingEngine(
  config: WeedingLevelConfig,
  width: number,
  height: number
): WeedingEngineState {
  return {
    config,
    width,
    height,
    finished: false,
    won: false,
    score: 0
  };
}

export function renderWeedingFrame(
  ctx: CanvasRenderingContext2D,
  state: WeedingEngineState
): void {
  ctx.clearRect(0, 0, state.width, state.height);

  ctx.fillStyle = '#1a3a1a';
  ctx.fillRect(0, 0, state.width, state.height);

  ctx.fillStyle = '#888';
  ctx.font = '24px sans-serif';
  ctx.textAlign = 'center';
  ctx.fillText('Weeding — Coming Soon', state.width / 2, state.height / 2);
}

export function updateWeedingEngine(
  state: WeedingEngineState,
  _deltaMs: number
): WeedingEngineState {
  return state;
}
