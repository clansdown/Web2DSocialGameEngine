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
  archetype: string | null;
  sex: string | null;
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

  try {
    return await response.json() as ApiResponse<T>;
  } catch {
    // Server returned a non-JSON response (e.g. 500 with empty body).
    // Return a structured error instead of throwing an unhandled SyntaxError.
    return { status: 'ok', data: {} as T, error: `Server returned ${response.status} with invalid response` };
  }
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
  const res = await apiPost<AuthResponse>('login', {}, { username, password });
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
  digitalCredential?: { protocol: string; data: unknown },
  overrideCode?: string,
  sex?: string
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
  if (overrideCode) {
    body.override_code = overrideCode;
  }
  if (sex) {
    body.sex = sex;
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

export type GamePhase = 'initial_mission' | 'land_patent' | 'duke_track' | 'duke_right' | 'sandbox';

export interface PlayerGameState {
  character_id: number;
  game_phase: GamePhase;
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

export interface NewUnlocks {
  new_units: string[];
  new_towers: string[];
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
  land_patent_earned?: boolean;
  duke_right_earned?: boolean;
  new_unlocks?: NewUnlocks;
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

export interface SpawnScheduleEntry {
  enemy_id: string;
  count: number;
  interval_ms: number;
  initial_delay_ms: number;
}

export interface TDRoundKickoffResponse {
  session_id: number;
  character_id: number;
  mini_game: string;
  level_id: number;
  difficulty: number;
  round_number: number;
  total_rounds: number;
  lives: number;
  gold: number;
  spawn_schedule: SpawnScheduleEntry[];
  map_metadata?: unknown;
  mobs?: unknown;
  towers?: unknown;
  units?: unknown;
  [key: string]: unknown;
}

export interface TDRoundCompleteResponse {
  session_id: number;
  game_over: boolean;
  won: boolean;
  lives: number;
  gold: number;
  score: number;
  rewards: Record<string, number>;
  completed: boolean;
  new_best_score: number;
  times_played: number;
  all_levels_done: boolean;
  base_unlocked?: boolean;
  game_phase?: string;
  land_patent_earned?: boolean;
  duke_right_earned?: boolean;
  [key: string]: unknown;
}

export type TDRoundResponse = TDRoundKickoffResponse | TDRoundCompleteResponse;

/**
 * Starts a new TD round session or completes an existing one.
 * Without session_id: kicks off a new game session with round 0 data.
 * With session_id + results: reports round completion and gets game over status.
 *
 * @param characterId - Character ID
 * @param options - Either { mini_game, level_id } for kickoff or { session_id, lives_lost, gold_earned } for completion
 * @param auth - Authentication object
 * @returns Promise<TDRoundResponse> - Round data or game over results
 *
 * Usage: Called from SimpleGame component to manage round lifecycle
 */
export async function tdRoundRequest(
  characterId: number,
  options: { mini_game?: string; level_id?: number; session_id?: number; lives_lost?: number; gold_earned?: number },
  auth: { username: string; token: string }
): Promise<TDRoundResponse> {
  const body: Record<string, unknown> = { character_id: characterId };

  if (options.session_id !== undefined) {
    body.session_id = options.session_id;
    body.lives_lost = options.lives_lost ?? 0;
    body.gold_earned = options.gold_earned ?? 0;
  } else {
    body.mini_game = options.mini_game;
    body.level_id = options.level_id;
  }

  const res = await apiPost<TDRoundResponse>('tdRound', body, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  if (!res.data) {
    throw new Error('Empty response from server');
  }
  return res.data as TDRoundResponse;
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
/**
 * Fetches translated text from the server for the specified language and text IDs.
 * This is a public endpoint — no authentication required.
 * Falls back to English if a translation doesn't exist.
 *
 * @param language - Language code ('en', 'es', 'de', etc.)
 * @param textIds - Array of text IDs to fetch
 * @returns Promise<Record<string, string>> - Map of text ID to translated content
 *
 * Usage: Called by components that need translated UI text
 */
export async function getTextsRequest(
  language: string,
  textIds: string[],
  sex?: string
): Promise<Record<string, string>> {
  const body: Record<string, unknown> = {
    language,
    text_ids: textIds
  };
  if (sex) {
    body.sex = sex;
  }

  const res = await apiPost<{ texts: Record<string, string> }>('getTexts', body);

  if (res.error) {
    throw new Error(res.error);
  }
  return (res.data as { texts: Record<string, string> }).texts;
}

export interface UITexture {
  url: string;
  width: number;
  height: number;
}

export interface UITexturesResponse {
  textures: UITexture[];
  padding_vertical_px: number;
  padding_horizontal_px: number;
}

/**
 * Fetches available UI background textures and component-specific
 * settings (like padding) from the server. The server looks up
 * config/ui_textures.json by component_id to return per-component
 * styling values.
 * Public endpoint — no authentication required.
 *
 * @param componentId - Component key in ui_textures.json (e.g. 'story_text')
 * @returns Promise<UITexturesResponse | null> - Texture data or null on failure
 *
 * Usage: Called from StoryText on mount to discover available backgrounds
 */
export async function getUITexturesRequest(componentId: string): Promise<UITexturesResponse | null> {
  const res = await apiPost<UITexturesResponse>('getUITextures', {
    component_id: componentId
  });
  if (res.error || !res.data) {
    return null;
  }
  return res.data;
}

/**
 * Sets a character's starting path archetype on the server.
 * This determines which mini-game the character plays first.
 * Requires authentication.
 *
 * @param characterId - ID of the character to update
 * @param archetype - The archetype to set ('wolf_warden' or 'assarter')
 * @param auth - Authentication object with username and token
 * @returns Promise<Character> - Updated character data with archetype set
 *
 * Usage: Called when player confirms their starting path in PathSelect
 */
export async function setCharacterArchetypeRequest(
  characterId: number,
  archetype: string,
  auth: { username: string; token: string }
): Promise<Character> {
  const res = await apiPost<Character>('setCharacterArchetype', {
    character_id: characterId,
    archetype
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as Character;
}

/**
 * Sets the character's biological sex (male/female) for gender-substituted text.
 * This is a one-time setting per character, stored in the characters table.
 * Requires authentication.
 *
 * @param characterId - ID of the character to update
 * @param sex - 'male' or 'female'
 * @param auth - Authentication object with username and token
 * @returns Promise<Character> - Updated character data with sex set
 *
 * Usage: Called from SexSelect component when player chooses their sex
 */
export async function setCharacterSexRequest(
  characterId: number,
  sex: string,
  auth: { username: string; token: string }
): Promise<Character> {
  const res = await apiPost<Character>('setCharacterSex', {
    character_id: characterId,
    sex
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as Character;
}

/**
 * Validates an age verification override code with the server.
 * Public endpoint — no authentication required.
 * Used for tech support / testing when Digital Credentials API is unavailable.
 * 
 * @param code - The override code string
 * @returns Promise<boolean> - true if code is valid
 * 
 * Usage: Called from CreateAccountPage when user enters override code
 */
export async function verifyAgeOverrideRequest(code: string): Promise<boolean> {
  const res = await apiPost<{ verified: boolean }>('verifyAgeOverride', {
    code
  });

  if (res.error) {
    return false;
  }
  return (res.data as { verified: boolean }).verified === true;
}

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

// ── Dukedom API types ─────────────────────────────────────────────

export interface DukedomInfo {
  id: number;
  name: string;
  description: string;
  owner_character_id: number;
  owner_name: string;
  member_count: number;
  created_at: number;
}

export interface JoinDukedomResponse {
  dukedom_id: number;
  fiefdom_id: number;
  game_phase: string;
  base_unlocked: boolean;
}

export interface CreateDukedomResponse {
  dukedom_id: number;
  fiefdom_id: number;
  game_phase: string;
  base_unlocked: boolean;
}

export interface StartDukeTrackResponse {
  game_phase: string;
}

/**
 * Fetches all available dukedoms from the server.
 * Requires authentication.
 *
 * @param auth - Authentication object with username and token
 * @returns Promise<DukedomInfo[]> - List of dukedoms
 *
 * Usage: Called to populate the dukedom selection screen
 */
export async function getDukedomsRequest(
  auth: { username: string; token: string }
): Promise<DukedomInfo[]> {
  const res = await apiPost<{ dukedoms: DukedomInfo[] }>('getDukedoms', {}, {
    username: auth.username,
    token: auth.token
  });

  if (res.error) {
    throw new Error(res.error);
  }
  return (res.data as { dukedoms: DukedomInfo[] }).dukedoms;
}

/**
 * Joins an existing dukedom, creating a fiefdom and transitioning to sandbox phase.
 *
 * @param characterId - Character to join with
 * @param dukedomId - Dukedom to join
 * @param auth - Authentication object with username and token
 * @returns Promise<JoinDukedomResponse> - New fiefdom and phase info
 *
 * Usage: Called from DukedomJoin screen
 */
export async function joinDukedomRequest(
  characterId: number,
  dukedomId: number,
  auth: { username: string; token: string }
): Promise<JoinDukedomResponse> {
  const res = await apiPost<JoinDukedomResponse>('joinDukedom', {
    character_id: characterId,
    dukedom_id: dukedomId
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as JoinDukedomResponse;
}

/**
 * Creates a new dukedom and transitions to sandbox phase.
 * Requires the character to have completed the duke track (all 25 levels).
 *
 * @param characterId - Character to create the dukedom for
 * @param name - Dukedom name (must be unique)
 * @param description - Optional description
 * @param auth - Authentication object with username and token
 * @returns Promise<CreateDukedomResponse> - New dukedom and fiefdom info
 *
 * Usage: Called from the dukedom creation form after duke track completion
 */
export async function createDukedomRequest(
  characterId: number,
  name: string,
  description: string,
  auth: { username: string; token: string }
): Promise<CreateDukedomResponse> {
  const res = await apiPost<CreateDukedomResponse>('createDukedom', {
    character_id: characterId,
    name,
    description
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as CreateDukedomResponse;
}

/**
 * Opts into the duke track (4x4 grid, 16 harder levels) to earn the right
 * to start a dukedom instead of joining one.
 *
 * @param characterId - Character to start the duke track for
 * @param auth - Authentication object with username and token
 * @returns Promise<StartDukeTrackResponse> - Updated game phase
 *
 * Usage: Called from PatentScreen when player chooses to start their own dukedom
 */
export async function startDukeTrackRequest(
  characterId: number,
  auth: { username: string; token: string }
): Promise<StartDukeTrackResponse> {
  const res = await apiPost<StartDukeTrackResponse>('startDukeTrack', {
    character_id: characterId
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as StartDukeTrackResponse;
}
