/**
 * Game state management helpers for mini-game lifecycle operations.
 * Provides high-level functions that wrap API calls with store updates.
 */

import { playerGameState } from './stores';
import { authenticatedPost } from './api';
import type { PlayerGameState, StartMiniGameResponse, EndMiniGameResponse, MiniGameConfig, TDRoundResponse } from './api';

/**
 * Fetches the current player state from the server and updates the store.
 * 
 * @param characterId - Character ID to fetch state for
 * @returns Promise<void> - Updates the playerGameState store on success
 * 
 * Usage: Called after character selection and after state transitions
 */
export async function fetchPlayerState(characterId: number): Promise<void> {
  const state = await authenticatedPost<PlayerGameState>('getPlayerState', {
    character_id: characterId
  });
  playerGameState.set(state);
}

/**
 * Starts a mini-game session for the given character and mini-game type.
 * Updates the store with the new state after successful start.
 * 
 * @param characterId - Character playing the mini-game
 * @param miniGame - Mini-game name ('tower_defense' or 'weeding')
 * @returns Promise<StartMiniGameResponse> - Level config data
 * 
 * Usage: Called when player selects a level from the mini-game select screen
 */
export async function startMiniGame(
  characterId: number,
  miniGame: string
): Promise<StartMiniGameResponse> {
  const result = await authenticatedPost<StartMiniGameResponse>('startMiniGame', {
    character_id: characterId,
    mini_game: miniGame
  });

  await fetchPlayerState(characterId);

  return result;
}

/**
 * Ends the current mini-game session and reports results to the server.
 * Updates the store with new game state after processing.
 * 
 * @param characterId - Character who played
 * @param miniGame - Mini-game name
 * @param levelId - Level that was played
 * @param won - Whether the player won
 * @param score - Player's score
 * @returns Promise<EndMiniGameResponse> - Results including rewards and progression
 * 
 * Usage: Called from MiniGameContainer when game finishes
 */
export async function endMiniGame(
  characterId: number,
  miniGame: string,
  levelId: number,
  won: boolean,
  score: number
): Promise<EndMiniGameResponse> {
  const result = await authenticatedPost<EndMiniGameResponse>('endMiniGame', {
    character_id: characterId,
    mini_game: miniGame,
    level_id: levelId,
    won,
    score
  });

  await fetchPlayerState(characterId);

  return result;
}

export async function tdRound(
  characterId: number,
  options: { mini_game?: string; level_id?: number; session_id?: number; lives_lost?: number; leaked_enemies?: Record<string, number> }
): Promise<TDRoundResponse> {
  const body: Record<string, unknown> = { character_id: characterId };

  if (options.session_id !== undefined) {
    body.session_id = options.session_id;
    body.lives_lost = options.lives_lost ?? 0;
    body.leaked_enemies = options.leaked_enemies ?? {};
  } else {
    body.mini_game = options.mini_game;
    body.level_id = options.level_id;
  }

  return await authenticatedPost<TDRoundResponse>('tdRound', body);
}

export async function getMiniGameConfigs(miniGame?: string | null): Promise<Record<string, MiniGameConfig>> {
  const body: Record<string, unknown> = {};
  if (miniGame) body.mini_game = miniGame;
  return await authenticatedPost<Record<string, MiniGameConfig>>('getMiniGameConfig', body);
}
