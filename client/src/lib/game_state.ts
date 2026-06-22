/**
 * Game state management helpers for mini-game lifecycle operations.
 * Provides high-level functions that wrap API calls with store updates.
 */

import { playerGameState } from './stores';
import { getPlayerStateRequest, startMiniGameRequest, endMiniGameRequest, getMiniGameConfigRequest } from './api';
import type { PlayerGameState, StartMiniGameResponse, EndMiniGameResponse, MiniGameConfig } from './api';
import * as auth from './auth';

/**
 * Fetches the current player state from the server and updates the store.
 * 
 * @param characterId - Character ID to fetch state for
 * @returns Promise<void> - Updates the playerGameState store on success
 * 
 * Usage: Called after character selection and after state transitions
 */
export async function fetchPlayerState(characterId: number): Promise<void> {
  const token = auth.getSessionToken();
  const username = auth.getInMemoryCredentials()?.username;
  if (!token || !username) {
    throw new Error('Not authenticated');
  }

  const state = await getPlayerStateRequest(characterId, { username, token });
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
  const token = auth.getSessionToken();
  const username = auth.getInMemoryCredentials()?.username;
  if (!token || !username) {
    throw new Error('Not authenticated');
  }

  const result = await startMiniGameRequest(characterId, miniGame, { username, token });

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
  const token = auth.getSessionToken();
  const username = auth.getInMemoryCredentials()?.username;
  if (!token || !username) {
    throw new Error('Not authenticated');
  }

  const result = await endMiniGameRequest(characterId, miniGame, levelId, won, score, { username, token });

  await fetchPlayerState(characterId);

  return result;
}

/**
 * Fetches mini-game configuration from the server.
 * 
 * @param miniGame - Optional specific mini-game name (null returns all)
 * @returns Promise<Record<string, MiniGameConfig>> - Config data
 * 
 * Usage: Called to populate the mission select screen
 */
export async function getMiniGameConfigs(miniGame: string | null = null): Promise<Record<string, MiniGameConfig>> {
  const token = auth.getSessionToken();
  const username = auth.getInMemoryCredentials()?.username;
  if (!token || !username) {
    throw new Error('Not authenticated');
  }

  return await getMiniGameConfigRequest(miniGame, { username, token });
}
