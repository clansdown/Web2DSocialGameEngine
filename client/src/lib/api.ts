const API_BASE = '';

import * as auth from './auth';
import { get } from 'svelte/store';
import { language } from './stores';

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
 * Makes an authenticated API call with automatic token refresh.
 * If the server returns needs_auth, refreshes via stored password
 * and retries the request once. Transparent to callers.
 *
 * @param endpoint - API endpoint (e.g. 'tdRound', 'getPlayerState')
 * @param body - Request body as JSON-compatible object
 * @returns Promise<T> - The API response data
 *
 * Usage: General-purpose authenticated API entry point for all callers
 */
export async function authenticatedPost<T>(
  endpoint: string,
  body: Record<string, unknown>
): Promise<T> {
  if (endpoint === 'login') {
    throw new Error('Use apiPost directly for login');
  }

  const creds = auth.getInMemoryCredentials();
  const token = auth.getSessionToken();
  if (!token || !creds) {
    throw new Error('Not authenticated');
  }

  const doFetch = async (t: string) =>
    await apiPost<T>(endpoint, body, { username: creds.username, token: t });

  let response = await doFetch(token);

  if (response.needs_auth) {
    // Token rejected — refresh via stored password
    const loginRes = await apiPost<any>('login', {}, {
      username: creds.username,
      password: creds.password
    });
    const newToken = loginRes.data?.token;
    if (!newToken) throw new Error('Session expired');

    auth.setSessionToken(newToken);

    // Retry original request with fresh token
    response = await doFetch(newToken);
    if (response.needs_auth) throw new Error('Session expired');
  }

  if (response.error) throw new Error(response.error);
  return response.data as T;
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

export type GamePhase = 'initial_mission' | 'land_patent' | 'baron_track' | 'baron_right' | 'sandbox';

export interface PlayerGameState {
  character_id: number;
  game_phase: GamePhase;
  current_mini_game: string | null;
  current_level_id: number | null;
  base_unlocked: boolean;
  entered_at: number;
  last_updated: number;
  progress: MiniGameProgress[];
  available_activities: string[];
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

export interface UnlockItem {
  id: string;
  text_key: string;
}

export interface NewUnlocks {
  new_units: UnlockItem[];
  new_towers: UnlockItem[];
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
  baron_right_earned?: boolean;
  new_unlocks?: NewUnlocks;
  silver_formatted?: string;
}

export interface MiniGameConfig {
  name: string;
  display_name: string;
  description: string;
  image?: string;
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
  baron_grid_size?: number;
  baron_levels?: MiniGameLevelConfig[];
  ongoing?: OngoingGameConfig;
}

/**
 * Ongoing-mode configuration served by the server for a mini-game.
 * Defines which difficulty/size options are available and the silver reward
 * tables. The server is authoritative for rewards; the client only renders
 * these options and queries estimateOngoingRewards for expected payouts.
 */
export interface OngoingGameConfig {
  game: string;
  difficulty_options: number[];
  default_difficulty: number;
  difficulty_coeff_pence: number;
  size_options: { value: number; reward_pence: number }[];
  default_size: number;
  [key: string]: unknown;
}

/** Response from estimateOngoingRewards: expected silver reward at the given
 * settings, adjusted for the character's current reward pool (diminishing
 * returns). */
export interface OngoingRewardEstimate {
  silver_pence: number;
  silver_formatted: string;
  base_silver_pence: number;
  reward_multiplier: number;
  pool: { full: number; half: number };
}



export interface SpawnScheduleEntry {
  enemy_id: string;
  count: number;
  interval_ms: number;
  initial_delay_ms: number;
  spawn_point_id?: string;
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
  baron_right_earned?: boolean;
  new_unlocks?: NewUnlocks;
  silver_formatted?: string;
  [key: string]: unknown;
}

export type TDRoundResponse = TDRoundKickoffResponse | TDRoundCompleteResponse;

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
 * This is a public endpoint — no authentication required. No gender or
 * character-name substitution is applied (use getCharacterTextsRequest for
 * character-context text). Falls back to English if a translation doesn't exist.
 *
 * @param language - Language code ('en', 'es', 'de', etc.)
 * @param textIds - Array of text IDs to fetch
 * @returns Promise<Record<string, string>> - Map of text ID to translated content
 *
 * Usage: Called by pre-auth screens (language select, login) that have no character
 */
export async function getTextsRequest(
  language: string,
  textIds: string[]
): Promise<Record<string, string>> {
  const body: Record<string, unknown> = {
    language,
    text_ids: textIds
  };

  const res = await apiPost<{ texts: Record<string, string> }>('getTexts', body);

  if (res.error) {
    throw new Error(res.error);
  }
  return (res.data as { texts: Record<string, string> }).texts;
}

/**
 * Fetches translated text for a character via the authenticated endpoint.
 * The server applies gender substitution ({male|female} tokens) and replaces
 * {character_name} with the character's display name, so the client never
 * sends sex or the character name. Falls back to English if a translation
 * doesn't exist.
 *
 * @param characterId - ID of the character whose context applies
 * @param textIds - Array of text IDs to fetch
 * @returns Promise<Record<string, string>> - Map of text ID to substituted content
 *
 * Usage: In-game screens with a selected character; prefer loadTexts() in lib/text.ts
 */
export async function getCharacterTextsRequest(
  characterId: number,
  textIds: string[]
): Promise<Record<string, string>> {
  const lang = get(language);
  return await authenticatedPost<{ texts: Record<string, string> }>('getCharacterTexts', {
    character_id: characterId,
    language: lang,
    text_ids: textIds
  }).then(data => data.texts);
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

// ── Barony API types ──────────────────────────────────────────────

export interface BaronyInfo {
  id: number;
  name: string;
  description: string;
  owner_character_id: number;
  owner_name: string;
  member_count: number;
  created_at: number;
  baron_character_id?: number;
  baron_name?: string;
}

export interface JoinBaronyResponse {
  barony_id: number;
  fiefdom_id: number;
  game_phase: string;
  base_unlocked: boolean;
}

export interface CreateBaronyResponse {
  barony_id: number;
  fiefdom_id: number;
  game_phase: string;
  base_unlocked: boolean;
}

export interface FiefdomBuilding {
  id: number;
  name: string;
  level: number;
  x: number;
  y: number;
  construction_start_ts: number;
  last_updated: number;
  action_start_ts: number;
  action_tag: string;
}

export interface FiefdomResponse {
  id: number;
  owner_id: number;
  name: string;
  x: number;
  y: number;
  peasants: number;
  gold: number;
  grain: number;
  wood: number;
  steel: number;
  bronze: number;
  stone: number;
  leather: number;
  mana: number;
  wall_count: number;
  morale: number;
  manor_level: number;
  buildings?: FiefdomBuilding[];
  officials?: unknown[];
  heroes?: unknown[];
  stationed_combatants?: unknown[];
}

export interface BuildResponse {
  building_id: number;
  fiefdom_id: number;
}

export interface SetFiefdomImportResponse {
  import_settings: Record<string, boolean>;
}

export interface StartBaronTrackResponse {
  game_phase: string;
}

/**
 * Fetches all available baronies from the server.
 * Requires authentication.
 *
 * @param auth - Authentication object with username and token
 * @returns Promise<BaronyInfo[]> - List of baronies
 *
 * Usage: Called to populate the barony selection screen
 */
export async function getBaroniesRequest(
  auth: { username: string; token: string }
): Promise<BaronyInfo[]> {
  const res = await apiPost<{ baronies: BaronyInfo[] }>('getBaronies', {}, {
    username: auth.username,
    token: auth.token
  });

  if (res.error) {
    throw new Error(res.error);
  }
  return (res.data as { baronies: BaronyInfo[] }).baronies;
}

/**
 * Joins an existing barony, creating a fiefdom and transitioning to sandbox phase.
 *
 * @param characterId - Character to join with
 * @param baronyId - Barony to join
 * @param auth - Authentication object with username and token
 * @returns Promise<JoinBaronyResponse> - New fiefdom and phase info
 *
 * Usage: Called from BaronyJoin screen
 */
export async function joinBaronyRequest(
  characterId: number,
  baronyId: number,
  auth: { username: string; token: string }
): Promise<JoinBaronyResponse> {
  const res = await apiPost<JoinBaronyResponse>('joinBarony', {
    character_id: characterId,
    barony_id: baronyId
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as JoinBaronyResponse;
}

/**
 * Creates a new barony and transitions to sandbox phase.
 * Requires the character to have completed the baron track (all 25 levels).
 *
 * @param characterId - Character to create the barony for
 * @param name - Barony name (must be unique)
 * @param description - Optional description
 * @param auth - Authentication object with username and token
 * @returns Promise<CreateBaronyResponse> - New barony and fiefdom info
 *
 * Usage: Called from the barony creation form after baron track completion
 */
export async function createBaronyRequest(
  characterId: number,
  name: string,
  description: string,
  auth: { username: string; token: string }
): Promise<CreateBaronyResponse> {
  const res = await apiPost<CreateBaronyResponse>('createBarony', {
    character_id: characterId,
    name,
    description
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as CreateBaronyResponse;
}

/**
 * Fetches fiefdom data, optionally including buildings and other entities.
 * Accepts fiefdom_id or character_id to look up the fiefdom.
 *
 * @param params - fiefdom_id (direct) or character_id (lookup by owner)
 * @param auth - Authentication object with username and token
 * @returns Promise<FiefdomResponse> - Fiefdom data
 *
 * Usage: Called when opening the manor view
 */
export async function getFiefdomRequest(
  params: { fiefdom_id?: number; character_id?: number },
  auth: { username: string; token: string }
): Promise<FiefdomResponse> {
  const res = await apiPost<FiefdomResponse>('getFiefdom', {
    fiefdom_id: params.fiefdom_id || 0,
    character_id: params.character_id || 0,
    include_buildings: true,
    include_officials: true,
    include_heroes: true,
    include_combatants: true
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as FiefdomResponse;
}

/**
 * Builds a building in a fiefdom via the /api/Build endpoint.
 *
 * @param params - Build parameters (fiefdom_id, building_type, x, y)
 * @param auth - Authentication object with username and token
 * @returns Promise<BuildResponse> - Building creation response
 *
 * Usage: Called when placing a building on the manor
 */
export async function buildRequest(
  params: { fiefdom_id: number; building_type: string; x: number; y: number },
  auth: { username: string; token: string }
): Promise<BuildResponse> {
  const res = await apiPost<BuildResponse>('Build', {
    action: 'create',
    fiefdom_id: params.fiefdom_id,
    building_type: params.building_type,
    x: params.x,
    y: params.y
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as BuildResponse;
}

/**
 * Sets auto-import preference for a resource in a fiefdom.
 *
 * @param fiefdomId - Fiefdom ID
 * @param resource - Resource name (e.g., "steel", "wood")
 * @param autoImport - Whether to auto-import this resource
 * @param auth - Authentication object with username and token
 * @returns Promise<SetFiefdomImportResponse> - Updated import settings
 *
 * Usage: Called from the manor economy panel
 */
export async function setFiefdomImportRequest(
  fiefdomId: number,
  resource: string,
  autoImport: boolean,
  auth: { username: string; token: string }
): Promise<SetFiefdomImportResponse> {
  const res = await apiPost<SetFiefdomImportResponse>('setFiefdomImport', {
    fiefdom_id: fiefdomId,
    resource,
    auto_import: autoImport
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as SetFiefdomImportResponse;
}

/**
 * Opts into the baron track (4x4 grid, 16 harder levels) to earn the right
 * to start a barony instead of joining one.
 *
 * @param characterId - Character to start the baron track for
 * @param auth - Authentication object with username and token
 * @returns Promise<StartBaronTrackResponse> - Updated game phase
 *
 * Usage: Called from PatentScreen when player chooses to start their own barony
 */
export async function startBaronTrackRequest(
  characterId: number,
  auth: { username: string; token: string }
): Promise<StartBaronTrackResponse> {
  const res = await apiPost<StartBaronTrackResponse>('startBaronTrack', {
    character_id: characterId
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as StartBaronTrackResponse;
}

/**
 * Queries the server for the expected silver reward of an ongoing game at the
 * given difficulty/size, adjusted for the character's current reward pool.
 * The server is authoritative; this is a read-only hint for display only.
 *
 * @param characterId - Character playing
 * @param miniGame - Mini-game name ('tower_defense' or 'weeding')
 * @param difficulty - Chosen difficulty
 * @param size - Chosen size (rounds for TD, grid size for weeding)
 * @param auth - Authentication object with username and token
 * @returns Promise<OngoingRewardEstimate> - Pool-adjusted silver reward estimate
 */
export async function estimateOngoingRewards(
  characterId: number,
  miniGame: string,
  difficulty: number,
  size: number,
  auth: { username: string; token: string }
): Promise<OngoingRewardEstimate> {
  const res = await apiPost<OngoingRewardEstimate>('estimateOngoingRewards', {
    character_id: characterId,
    mini_game: miniGame,
    difficulty,
    size
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as OngoingRewardEstimate;
}

// ── Building config types ──────────────────────────────────────────

export interface BuildingTypeConfig {
  display_name: string;
  image: string;
  construction_image: string;
  width: number;
  height: number;
  construction_times: number[];
  costs: Record<string, number>;
  min_manor_level: number;
  [key: string]: unknown;
}

/**
 * Acknowledges the land patent notification, silencing the flag.
 *
 * @param characterId - Character to acknowledge for
 * @param auth - Authentication object with username and token
 */
export async function acknowledgeLandPatentRequest(
  characterId: number,
  auth: { username: string; token: string }
): Promise<void> {
  await apiPost('acknowledgeLandPatent', {
    character_id: characterId
  }, { username: auth.username, token: auth.token });
}

/**
 * Fetches building type configurations from the server.
 * Public endpoint — no authentication required.
 *
 * @returns Promise<Record<string, BuildingTypeConfig>> - Building configs keyed by type ID
 *
 * Usage: Called when opening the manor view to get building metadata
 */
export async function getBuildingConfigsRequest(): Promise<Record<string, BuildingTypeConfig>> {
  const res = await apiPost<Record<string, BuildingTypeConfig>>('getBuildingConfigs', {});

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as Record<string, BuildingTypeConfig>;
}
