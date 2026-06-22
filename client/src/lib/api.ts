const API_BASE = '';

export interface ApiResponse<T = unknown> {
  status: 'ok';
  data: T;
  token?: string;
  needs_auth?: boolean;
  auth_failed?: boolean;
  error?: string;
}

export interface Character {
  id: number;
  display_name: string;
  safe_display_name: string;
  level: number;
}

export interface AuthResponse {
  user_id: number;
  username: string;
  adult: boolean;
  characters: Character[];
  token?: string;
}

export interface ProfileResponse {
  adult: boolean;
  token?: string;
}

export interface CharacterProfileResponse extends Character {
  token?: string;
}

interface RequestOptions {
  token?: string;
  username?: string;
  password?: string;
}

/**
 * Makes a POST request to the API endpoint with optional authentication.
 * Constructs request body with auth credentials if provided.
 * Returns parsed JSON response or throws on error.
 * 
 * @param endpoint - API endpoint path (e.g., 'login', 'getCharacter')
 * @param body - Request body object to send as JSON
 * @param options - Optional authentication options (username/password or token)
 * @returns Promise<ApiResponse<T>> - Parsed API response
 * 
 * Usage: Low-level API function used by all specific request functions
 */
export async function apiPost<T = unknown>(
  endpoint: string,
  body: Record<string, unknown>,
  options?: RequestOptions
): Promise<ApiResponse<T>> {
  const requestBody: Record<string, unknown> = { ...body };

  if (options?.username) {
    if (!requestBody.auth) {
      requestBody.auth = {};
    }
    (requestBody.auth as Record<string, unknown>).username = options.username;

    if (options.password) {
      (requestBody.auth as Record<string, unknown>).password = options.password;
    } else if (options.token) {
      (requestBody.auth as Record<string, unknown>).token = options.token;
    }
  }

  const response = await fetch(`${API_BASE}/api/${endpoint}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(requestBody)
  });

  return await response.json() as ApiResponse<T>;
}

/**
 * Authenticates a user with username and password.
 * Returns user data, character list, and session token on success.
 * 
 * @param username - User's account username
 * @param password - User's account password
 * @returns Promise<AuthResponse> - User data, characters, and token
 * 
 * Usage: Called from LoginPage when user submits login form
 */
export async function loginRequest(
  username: string,
  password: string
): Promise<AuthResponse> {
  const res = await apiPost<AuthResponse>('login', { username, password });
  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as AuthResponse;
}

/**
 * Creates a new user account with character.
 * Validates credentials, optional adult verification, and character naming words.
 * Returns user data, character list, and session token on success.
 * 
 * @param username - Desired username for the new account
 * @param password - Password for the new account (min 8 characters)
 * @param adult - Boolean flag indicating if user verified as 18+
 * @param word1 - First word for safe character display name
 * @param word2 - Second word for safe character display name
 * @param displayName - Optional custom display name (requires adult verification)
 * @param digitalCredential - Optional digital credential proof for age verification
 * @returns Promise<AuthResponse> - New user data, character, and token
 * 
 * Usage: Called from CreateAccountPage when creating new account
 */
export async function createAccountRequest(
  username: string,
  password: string,
  adult: boolean,
  word1: string,
  word2: string,
  displayName?: string,
  digitalCredential?: { protocol: string; data: unknown }
): Promise<AuthResponse> {
  const body: Record<string, unknown> = {
    username,
    password,
    adult,
    word1,
    word2
  };
  if (displayName) {
    body.displayName = displayName;
  }
  if (digitalCredential) {
    body.digitalCredential = digitalCredential;
  }

  const res = await apiPost<AuthResponse>('createAccount', body);
  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as AuthResponse;
}

/**
 * Fetches character data from the server by character ID.
 * Requires authentication via username and session token.
 * 
 * @param characterId - Unique identifier of the character to fetch
 * @param auth - Authentication object with username and token
 * @returns Promise<Character> - Character data from server
 * 
 * Usage: Used when loading character details for game play
 */
export async function getCharacterRequest(
  characterId: number,
  auth: { username: string; token: string }
): Promise<Character> {
  const res = await apiPost<Character>('getCharacter', {
    character_id: characterId
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as Character;
}

/**
 * Updates user profile settings on the server.
 * Currently supports updating the adult verification flag.
 * 
 * @param adult - New adult flag value
 * @param auth - Authentication object with username and token
 * @returns Promise<ProfileResponse> - Updated profile data and new token
 * 
 * Usage: Called when user updates their profile settings
 */
export async function updateUserProfileRequest(
  adult: boolean,
  auth: { username: string; token: string }
): Promise<ProfileResponse> {
  const res = await apiPost<ProfileResponse>('updateUserProfile', {
    adult
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as ProfileResponse;
}

/**
 * Updates character profile including display name and safe name words.
 * Requires authentication and only works for characters owned by the user.
 * 
 * @param characterId - Unique identifier of the character to update
 * @param displayName - New custom display name (may require adult verification)
 * @param word1 - New first word for safe display name
 * @param word2 - New second word for safe display name
 * @param auth - Authentication object with username and token
 * @returns Promise<CharacterProfileResponse> - Updated character data and new token
 * 
 * Usage: Called when user wants to change character name
 */
export async function updateCharacterProfileRequest(
  characterId: number,
  displayName: string,
  word1: string,
  word2: string,
  auth: { username: string; token: string }
): Promise<CharacterProfileResponse> {
  const res = await apiPost<CharacterProfileResponse>('updateCharacterProfile', {
    character_id: characterId,
    display_name: displayName,
    word1,
    word2
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as CharacterProfileResponse;
}

/**
 * Refreshes an expired session token using username and password.
 * Used to restore session without requiring full re-authentication.
 * 
 * @param username - User's account username
 * @param password - User's account password
 * @returns Promise<{ token: string }> - New session token
 * 
 * Usage: Called when session expires and needs refresh
 */
export async function refreshToken(
  username: string,
  password: string
): Promise<{ token: string }> {
  const res = await apiPost<{ token: string }>('login', {
    username,
    password
  });

  if (res.error || !res.token) {
    throw new Error(res.error || 'Failed to refresh token');
  }

  return { token: res.token };
}

// ── Mini-game API types ──────────────────────────────────────────────

export interface MiniGameProgress {
  id: number;
  character_id: number;
  mini_game: string;
  level_id: number;
  completed: boolean;
  best_score: number;
  times_played: number;
  last_played: number;
}

export interface PlayerGameState {
  character_id: number;
  game_phase: 'initial_mission' | 'sandbox';
  current_mini_game: string | null;
  current_level_id: number | null;
  base_unlocked: boolean;
  entered_at: number;
  last_updated: number;
  progress: MiniGameProgress[];
}

export interface MiniGameLevelConfig {
  id: number;
  row: number;
  col: number;
  difficulty: number;
  reward?: Record<string, number>;
  map?: string;
  mini_game?: string;
  level_id?: number;
  num_waves?: number;
  lane_count?: number;
  enemy_types?: string[];
  [key: string]: unknown;
}

export interface StartMiniGameResponse {
  character_id: number;
  mini_game: string;
  level_id: number;
  level_config: MiniGameLevelConfig;
  [key: string]: unknown;
}

export interface EndMiniGameResponse {
  completed: boolean;
  score: number;
  new_best_score: number;
  times_played: number;
  all_levels_done: boolean;
  base_unlocked: boolean;
  game_phase: string;
  next_level_id: number | null;
  rewards: Record<string, number>;
  completion_bonus?: Record<string, number>;
}

export interface MiniGameConfig {
  name: string;
  display_name: string;
  description: string;
  grid_size: number;
  sequential: boolean;
  levels: MiniGameLevelConfig[];
  completion_bonus: {
    base_unlock: boolean;
    resources: Record<string, number>;
  };
  replay_config: {
    random_generation: boolean;
    difficulty_scaling: Record<string, number>;
    reward_scaling: Record<string, number>;
  };
}

/**
 * Fetches the current player's game state including phase, active mini-game,
 * and progress across all mini-game campaigns.
 *
 * @param characterId - The character ID whose state to fetch
 * @param auth - Authentication object with username and token
 * @returns Promise<PlayerGameState> - Current game state
 *
 * Usage: Called on character selection and after mini-game transitions
 */
export async function getPlayerStateRequest(
  characterId: number,
  auth: { username: string; token: string }
): Promise<PlayerGameState> {
  const res = await apiPost<PlayerGameState>('getPlayerState', {
    character_id: characterId
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as PlayerGameState;
}

/**
 * Starts a mini-game session for the character.
 * Validates prerequisites and returns level configuration.
 *
 * @param characterId - The character playing the mini-game
 * @param miniGame - The mini-game name ('tower_defense' or 'weeding')
 * @param auth - Authentication object with username and token
 * @returns Promise<StartMiniGameResponse> - Level config for the client to render
 *
 * Usage: Called when player selects a level from the mini-game grid
 */
export async function startMiniGameRequest(
  characterId: number,
  miniGame: string,
  auth: { username: string; token: string }
): Promise<StartMiniGameResponse> {
  const res = await apiPost<StartMiniGameResponse>('startMiniGame', {
    character_id: characterId,
    mini_game: miniGame
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as StartMiniGameResponse;
}

/**
 * Ends a mini-game session and reports the outcome to the server.
 * Processes rewards, updates progress, and potentially unlocks the base.
 *
 * @param characterId - The character who played
 * @param miniGame - The mini-game name
 * @param levelId - The level that was played
 * @param won - Whether the player won
 * @param score - The player's score
 * @param auth - Authentication object with username and token
 * @returns Promise<EndMiniGameResponse> - Results including rewards and next level
 *
 * Usage: Called when the mini-game finishes (win, lose, or quit)
 */
export async function endMiniGameRequest(
  characterId: number,
  miniGame: string,
  levelId: number,
  won: boolean,
  score: number,
  auth: { username: string; token: string }
): Promise<EndMiniGameResponse> {
  const res = await apiPost<EndMiniGameResponse>('endMiniGame', {
    character_id: characterId,
    mini_game: miniGame,
    level_id: levelId,
    won,
    score
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as EndMiniGameResponse;
}

/**
 * Retrieves mini-game configuration data.
 *
 * @param miniGame - Optional mini-game name to filter (returns all if omitted)
 * @param auth - Authentication object with username and token
 * @returns Promise<Record<string, MiniGameConfig>> - Mini-game configurations keyed by name
 *
 * Usage: Called to populate the mini-game selection screen with descriptions
 */
export async function getMiniGameConfigRequest(
  miniGame: string | null,
  auth: { username: string; token: string }
): Promise<Record<string, MiniGameConfig>> {
  const body: Record<string, unknown> = {};
  if (miniGame) {
    body.mini_game = miniGame;
  }

  const res = await apiPost<Record<string, MiniGameConfig>>('getMiniGameConfig', body, {
    username: auth.username,
    token: auth.token
  });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as Record<string, MiniGameConfig>;
}
