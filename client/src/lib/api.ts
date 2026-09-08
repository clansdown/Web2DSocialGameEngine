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
  honor_name: string | null;
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
  pond_type?: string;
  output_rates: Record<string, number>;
}

export interface FiefdomResponse {
  id: number;
  owner_id: number;
  name: string;
  x: number;
  y: number;
  gold: number;
  silver_pence: number;
  grain: number;
  wood: number;
  steel: number;
  bronze: number;
  /** Retired: stone costs were removed from the game; the server still returns it (always 0). */
  stone: number;
  leather: number;
  mana: number;
  charcoal: number;
  iron: number;
  ironwork: number;
  fancy_ironwork: number;
  beams: number;
  boards: number;
  wall_count: number;
  morale: number;
  manor_level: number;
  buildings?: FiefdomBuilding[];
  officials?: unknown[];
  heroes?: unknown[];
  stationed_combatants?: unknown[];
  import_settings?: Record<string, boolean>;
  reserves?: Record<string, number>;
  economy_report?: EconomyReport;
  /** Per-building road-morale points (building_id → points), only when include_buildings. */
  road_morale?: Record<string, number>;
  /** River cells (grid [x,y] pairs) for this fiefdom. */
  river_cells?: Array<[number, number]>;
  /** Per-building water-powered state (building_id → powered), only when include_buildings. */
  water_power?: Record<string, boolean>;
  /** Water-power detail: powered_by (building_id → pond id) + pond_load (pond id → count). */
  water_power_detail?: {
    powered_by?: Record<string, number>;
    pond_load?: Record<string, number>;
  };
  /** Arable land (abstract resource limiting manor growth). */
  arable_land?: {
    total: number;
    used: number;
    available: number;
  };
  /** Forest land (off-map resource limiting the wood producers). */
  forest_land?: {
    total: number;
    used: number;
    available: number;
  };
}

export interface EconomyExport {
  amount: number;
  gold: number;
  pence?: number;
}

/**
 * Per-fiefdom economy ledger returned by getFiefdom after a time update.
 * Produced/consumed/imported map resource names to amounts; exported maps
 * resource names to {amount, gold} sold above reserve.
 * net_gold is gold produced + export gold − import spend − gold consumed.
 */
export interface EconomyReport {
  elapsed_seconds: number;
  produced: Record<string, number>;
  consumed: Record<string, number>;
  imported: Record<string, number>;
  exported: Record<string, EconomyExport>;
  net_gold: number;
  net_silver?: number;
  recommendations: string[];
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
  honor_name: string;
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
  params: { fiefdom_id: number; building_type: string; x: number; y: number; character_id: number },
  auth: { username: string; token: string }
): Promise<BuildResponse> {
  const res = await apiPost<BuildResponse>('Build', {
    action: 'create',
    fiefdom_id: params.fiefdom_id,
    building_type: params.building_type,
    x: params.x,
    y: params.y,
    character_id: params.character_id
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as BuildResponse;
}

/**
 * Upgrades a building to its next level via /api/Build (action 'upgrade').
 *
 * @param params - fiefdom_id, building_id, character_id
 * @param auth - Authentication object with username and token
 * @returns Promise<BuildResponse> - Upgrade response (cost, upgrade_to_level)
 *
 * Usage: Called from the manor building info card
 */
export async function upgradeBuildingRequest(
  params: { fiefdom_id: number; building_id: number; character_id: number },
  auth: { username: string; token: string }
): Promise<BuildResponse> {
  const res = await apiPost<BuildResponse>('Build', {
    action: 'upgrade',
    fiefdom_id: params.fiefdom_id,
    building_id: params.building_id,
    character_id: params.character_id
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as BuildResponse;
}

/**
 * Converts a building to its next stage via /api/Build (action 'convert').
 * Price = max(0, successor level-1 cost − 80% of the old building's cumulative
 * cost); the old building's row is transformed in place to the successor.
 *
 * @param params - fiefdom_id, building_id, character_id
 * @param auth - Authentication object with username and token
 * @returns Promise<BuildResponse> - Convert response (building_type, level)
 *
 * Usage: Called from the manor building info card's "Convert to <next>" button
 */
export async function convertBuildingRequest(
  params: { fiefdom_id: number; building_id: number; character_id: number },
  auth: { username: string; token: string }
): Promise<BuildResponse> {
  const res = await apiPost<BuildResponse>('Build', {
    action: 'convert',
    fiefdom_id: params.fiefdom_id,
    building_id: params.building_id,
    character_id: params.character_id
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as BuildResponse;
}

/**
 * Demolishes a building via /api/Build (action 'demolish'), refunding 80% of
 * its cumulative cost.
 *
 * @param params - fiefdom_id, building_id, character_id
 * @param auth - Authentication object with username and token
 * @returns Promise<BuildResponse> - Demolish response (refund)
 *
 * Usage: Called from the manor building info card
 */
export async function demolishBuildingRequest(
  params: { fiefdom_id: number; building_id: number; character_id: number },
  auth: { username: string; token: string }
): Promise<BuildResponse> {
  const res = await apiPost<BuildResponse>('Build', {
    action: 'demolish',
    fiefdom_id: params.fiefdom_id,
    building_id: params.building_id,
    character_id: params.character_id
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as BuildResponse;
}

/**
 * Moves a building to a new cell via /api/Build (action 'move'), costing 10%
 * of the current level's cost.
 *
 * @param params - fiefdom_id, building_id, x, y, character_id
 * @param auth - Authentication object with username and token
 * @returns Promise<BuildResponse> - Move response (new_x, new_y, cost)
 *
 * Usage: Called from the manor building info card
 */
export async function moveBuildingRequest(
  params: { fiefdom_id: number; building_id: number; x: number; y: number; character_id: number },
  auth: { username: string; token: string }
): Promise<BuildResponse> {
  const res = await apiPost<BuildResponse>('Build', {
    action: 'move',
    fiefdom_id: params.fiefdom_id,
    building_id: params.building_id,
    x: params.x,
    y: params.y,
    character_id: params.character_id
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as BuildResponse;
}

/**
 * Upgrades a mill pond's type (earthen → timber → stone) via /api/Build.
 *
 * @param params - fiefdom_id, building_id, character_id
 * @param auth - Authentication object with username and token
 * @returns Promise<BuildResponse> - Pond type upgrade response (pond_type)
 *
 * Usage: Called from the manor building info card for a mill_pond
 */
export async function upgradePondTypeRequest(
  params: { fiefdom_id: number; building_id: number; character_id: number },
  auth: { username: string; token: string }
): Promise<BuildResponse> {
  const res = await apiPost<BuildResponse>('Build', {
    action: 'upgrade_pond_type',
    fiefdom_id: params.fiefdom_id,
    building_id: params.building_id,
    character_id: params.character_id
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

export interface SetFiefdomReserveResponse {
  reserves: Record<string, number>;
}

/**
 * Sets the reserve (minimum stock kept) for a resource in a fiefdom.
 * Excess above the reserve is auto-sold for gold each economy tick.
 *
 * @param fiefdomId - Fiefdom ID
 * @param resource - Resource name (e.g., "grain", "ironwork")
 * @param reserve - Minimum amount to keep in stock (0 sells all excess)
 * @param auth - Authentication object with username and token
 * @returns Promise<SetFiefdomReserveResponse> - Updated reserve settings
 *
 * Usage: Called from the manor economy panel
 */
export async function setFiefdomReserveRequest(
  fiefdomId: number,
  resource: string,
  reserve: number,
  auth: { username: string; token: string }
): Promise<SetFiefdomReserveResponse> {
  const res = await apiPost<SetFiefdomReserveResponse>('setFiefdomReserve', {
    fiefdom_id: fiefdomId,
    resource,
    reserve
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as SetFiefdomReserveResponse;
}

export interface SetBuildingOutputRateResponse {
  output_rates: Record<string, number>;
}

/**
 * Sets how much of a building's maximum possible output actually runs (0..1).
 * The output must be produced by the building's config and unlocked at its
 * level. Scaling the rate also scales that output's input requirements.
 *
 * @param buildingId - Building instance ID
 * @param output - Output resource name (e.g., "fancy_ironwork")
 * @param rate - Utilization 0..1 (0 = off, 1 = full)
 * @param auth - Authentication object with username and token
 * @returns Promise<SetBuildingOutputRateResponse> - Updated per-output rates
 *
 * Usage: Called from the manor production panel
 */
export async function setBuildingOutputRateRequest(
  buildingId: number,
  output: string,
  rate: number,
  auth: { username: string; token: string }
): Promise<SetBuildingOutputRateResponse> {
  const res = await apiPost<SetBuildingOutputRateResponse>('setBuildingOutputRate', {
    building_id: buildingId,
    output,
    rate
  }, { username: auth.username, token: auth.token });

  if (res.error) {
    throw new Error(res.error);
  }
  return res.data as SetBuildingOutputRateResponse;
}

/**
 * Opts into the baron track (4x4 grid, 16 harder levels) to earn the right
 * to start a barony instead of joining one. Captures the aspiring barony's
 * honor name, which the server requires to be unique (case-insensitive).
 *
 * @param characterId - Character to start the baron track for
 * @param honorName - The aspiring barony's honor name (required, <= 64 chars)
 * @param auth - Authentication object with username and token
 * @returns Promise<StartBaronTrackResponse> - Updated game phase and honor name
 *
 * Usage: Called from LandPatentPanel when player chooses to start their own barony
 */
export async function startBaronTrackRequest(
  characterId: number,
  honorName: string,
  auth: { username: string; token: string }
): Promise<StartBaronTrackResponse> {
  const res = await apiPost<StartBaronTrackResponse>('startBaronTrack', {
    character_id: characterId,
    honor_name: honorName
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

/**
 * A {gold, shillings, pence} money amount — any subset of keys may be present.
 * Used for money-form production outputs and prices.
 */
export interface MoneyObject {
  gold?: number;
  shillings?: number;
  pence?: number;
}

/**
 * A production/input amount. May be a plain number (gold-denominated for the
 * gold resource), a money object ({gold, shillings, pence}), or a per-level
 * array of either (each level adds 5% of the base, each stage +20%).
 */
export type AmountValue =
  | number
  | MoneyObject
  | Array<number | MoneyObject>;

export interface BuildingOutputConfig {
  resource: string;
  amount: AmountValue;
  inputs?: Record<string, { amount?: AmountValue } | AmountValue>;
  min_level?: number;
}

export interface PondTypeConfig {
  id: string;
  capacity: number;
  max_level: number;
  construction_times: number[];
}

export interface BuildingTypeConfig {
  display_name: string;
  image: string;
  construction_image: string;
  width: number;
  height: number;
  construction_times: number[];
  costs: Record<string, number>;
  min_manor_level: number;
  /** Arable acres this building type claims (0 = none). */
  arable_acres: number;
  /** Forest acres this building type claims (wood producers; 0 = none). */
  forest_acres: number;
  /** Class grouping, definitive and independent of the built_from chain. */
  class: string;
  /** The previous stage this building can be converted from (stage chains). */
  built_from?: string;
  outputs?: BuildingOutputConfig[];
  water_powered?: boolean;
  water_source?: boolean;
  pond_types?: PondTypeConfig[];
  race_tiles?: Record<string, string>;
  race_tiles_canonical?: Record<string, string[]>;
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
 * Requires authentication.
 *
 * @param auth - Authentication object with username and token
 * @returns Promise resolving to the building configs keyed by type ID plus the
 *          config-driven build-palette ordering (see manor_ui.json)
 *
 * Usage: Called when opening the manor view to get building metadata
 */
export async function getBuildingConfigsRequest(
  auth: { username: string; token: string }
): Promise<{ configs: Record<string, BuildingTypeConfig>; build_order: string[] }> {
  const res = await apiPost<Record<string, BuildingTypeConfig | string[]>>('getBuildingConfigs', {}, {
    username: auth.username,
    token: auth.token
  });

  if (res.error || !res.data) {
    throw new Error(res.error || 'Failed to load building configs');
  }

  const data = res.data;
  const buildOrder = Array.isArray(data.build_order) ? (data.build_order as string[]) : [];
  const configs: Record<string, BuildingTypeConfig> = {};
  for (const [key, value] of Object.entries(data)) {
    if (key === 'build_order' || key === 'token') continue;
    configs[key] = value as BuildingTypeConfig;
  }
  return { configs, build_order: buildOrder };
}

// ── Realtime combat (lobby REST surface; battle runs over /ws/combat) ──

export interface combat_ruleset_dto {
  id: string;
  name: string;
  mode: string;
}

export interface combat_map_dto {
  id: string;
  file: string;
}

export interface combat_match_summary_dto {
  match_id: string;
  match_code: string;
  mode: string;
  ruleset_id: string;
  map_id: string;
  player_count: number;
  max_players: number;
  host_name?: string;
}

export interface combat_configs_dto {
  rulesets: combat_ruleset_dto[];
  maps: combat_map_dto[];
}

export interface combat_match_result_dto {
  match_id: string;
  match_code: string;
  mode: string;
}

export interface retinue_member_dto {
  id: number;
  character_id: number;
  display_name: string;
  unit_class: string;
  is_knight: boolean;
  level: number;
  gender: string;
  health: number;
  health_updated: number;
  priority: number;
  weapons: unknown;
  armor: unknown;
  equipment: unknown;
  abilities: unknown[];
  status: string;
  maintained: boolean;
  fieldable: boolean;
  created_at: number;
}

// Retinue capacity (the knight is free; recruited members count only).
export interface retinue_capacity_dto {
  manor_level: number;
  total: number;
  recruited: number;
  available: number;
}

// Continuous-health recovery/fielding parameters for the retinue.
export interface retinue_recovery_dto {
  max_recovery_hours: number;
  min_deploy_hp: number;
  multiplier: number;
}

// A named hire offer on the recruit market.
export interface recruit_candidate_dto {
  unit_class: string;
  display_name: string;
  gender: string;
  level: number;
  hour_bucket: number;
  expires_at: number;
  fee: Record<string, number>;
}

export interface recruit_market_dto {
  candidates: recruit_candidate_dto[];
  current_bucket: number;
  next_refresh_at: number;
  manor_level: number;
  capacity: retinue_capacity_dto;
}

// Equipment item config (game/config/equipment.json).
export interface equipment_item_dto {
  name: string;
  slot: string;
  requires_level: number;
  allowed_classes?: string[];
  upkeep?: Record<string, number>;
  armory_slots: number;
  base_value: number;
  city_price?: number;
  craft?: {
    duration_hours: number;
    materials: Record<string, number>;
    tech_node?: string;
  };
}

// A building carrying a tech tree plus its per-instance progression state.
export interface tech_building_dto {
  id: number;
  name: string;
  level: number;
  tech_trees: string[];
  tech_xp: number;
  tech_nodes: string[];
  forge_order: unknown | null;
  training: unknown | null;
}

export interface tech_trees_dto {
  trees: Record<string, { name: string; nodes: unknown[] }>;
  buildings: tech_building_dto[];
}

export interface armory_item_dto {
  id: number;
  item_id: string;
  member_id: number | null;
  created_at: number;
  item?: equipment_item_dto;
  sell_value?: number;
}

export interface storage_item_dto {
  item_id: string;
  count: number;
  item?: Record<string, unknown>;
}

export interface retinue_gear_dto {
  armory: armory_item_dto[];
  storage: storage_item_dto[];
}

/**
 * Creates a new PvE combat match (host is the creating character).
 *
 * @param characterId - Host character id
 * @param options - Mode ('pve'), ruleset id, and map id
 * @returns Promise<combat_match_result_dto> - Match id/code for the lobby
 *
 * Usage: Called from MatchLobby's create flow
 */
export async function combatCreateRequest(
  characterId: number,
  options: { mode: 'pve'; ruleset_id: string; map_id: string }
): Promise<combat_match_result_dto> {
  return await authenticatedPost<combat_match_result_dto>('combatCreate', {
    character_id: characterId,
    mode: options.mode,
    ruleset_id: options.ruleset_id,
    map_id: options.map_id
  });
}

/**
 * Joins a PvE lobby match by its shareable code.
 *
 * @param characterId - Joining character id
 * @param matchCode - 6-character match code
 * @returns Promise<combat_match_result_dto> - Match id/code
 *
 * Usage: Called from MatchLobby's join flow
 */
export async function combatJoinRequest(
  characterId: number,
  matchCode: string
): Promise<combat_match_result_dto> {
  return await authenticatedPost<combat_match_result_dto>('combatJoin', {
    character_id: characterId,
    match_code: matchCode
  });
}

/**
 * Lists open (lobby-phase) PvE matches.
 *
 * @param none
 * @returns Promise<{ matches: combat_match_summary_dto[] }>
 *
 * Usage: Future barony-invite UI; not used by the current scaffold screens
 */
export async function combatListRequest(): Promise<{ matches: combat_match_summary_dto[] }> {
  return await authenticatedPost<{ matches: combat_match_summary_dto[] }>('combatList', {});
}

/**
 * PvP matchmaking — STUB endpoint; always throws 'not yet implemented'.
 *
 * @param none
 * @returns Promise<never> - Throws; challenges/acceptances arrive later
 *
 * Usage: Placeholder for the future matchmaking queue UI
 */
export async function combatMatchmakingRequest(): Promise<never> {
  return await authenticatedPost<never>('combatMatchmaking', {});
}

/**
 * Fetches the available combat rulesets and maps.
 *
 * @param none
 * @returns Promise<combat_configs_dto> - Ruleset and map lists
 *
 * Usage: Called by MatchLobby when it opens
 */
export async function combatGetConfigsRequest(): Promise<combat_configs_dto> {
  return await authenticatedPost<combat_configs_dto>('combatGetConfigs', {});
}

/**
 * Fetches the character's full retinue (knight + units).
 *
 * @param characterId - Character whose retinue to fetch
 * @returns Promise<{ members: retinue_member_dto[]; capacity: retinue_capacity_dto }>
 *
 * Usage: Retinue management UI (later); combat snapshots come via welcome
 */
export async function getRetinueRequest(
  characterId: number
): Promise<{
  members: retinue_member_dto[];
  capacity: retinue_capacity_dto;
  recovery: retinue_recovery_dto;
}> {
  return await authenticatedPost<{
    members: retinue_member_dto[];
    capacity: retinue_capacity_dto;
    recovery: retinue_recovery_dto;
  }>('getRetinue', { character_id: characterId });
}

/**
 * Lists the current recruit market (named candidate offers for this manor).
 *
 * @param characterId - Character whose market to list
 * @returns Promise<recruit_market_dto> - Candidate offers + capacity
 *
 * Usage: Retinue panel recruit tab shows candidate cards, expiry, and fees
 */
export async function listRecruitCandidatesRequest(
  characterId: number
): Promise<recruit_market_dto> {
  return await authenticatedPost<recruit_market_dto>('listRecruitCandidates', {
    character_id: characterId
  });
}

/**
 * Hires a named candidate from the recruit market into the retinue.
 *
 * @param characterId - Owning character
 * @param candidate - The offer as returned by listRecruitCandidatesRequest
 * @returns Promise<{ member: retinue_member_dto; fee: Record<string, number>; capacity: retinue_capacity_dto }>
 *
 * Usage: Recruit tab hire button for a specific candidate card
 */
export async function hireRecruitRequest(
  characterId: number,
  candidate: recruit_candidate_dto
): Promise<{
  member: retinue_member_dto;
  fee: Record<string, number>;
  capacity: retinue_capacity_dto;
}> {
  return await authenticatedPost<{
    member: retinue_member_dto;
    fee: Record<string, number>;
    capacity: retinue_capacity_dto;
  }>('hireRecruit', {
    character_id: characterId,
    unit_class: candidate.unit_class,
    display_name: candidate.display_name,
    gender: candidate.gender,
    level: candidate.level,
    hour_bucket: candidate.hour_bucket
  });
}

/**
 * Reorders the roster by strict priority (funding + infirmary-bed order).
 * The knight is always first; `memberIds` must be every non-knight member id
 * in the new preferred order (a full permutation of the current roster).
 *
 * @param characterId - Owning character
 * @param memberIds - Ordered non-knight member ids, top priority first
 * @returns Promise<{ members: { id: number; priority: number }[] }> - New ranking
 *
 * Usage: Retinue panel Roster tab up/down reorder controls
 */
export async function setRetinuePriorityRequest(
  characterId: number,
  memberIds: number[]
): Promise<{ members: { id: number; priority: number }[] }> {
  return await authenticatedPost<{ members: { id: number; priority: number }[] }>(
    'setRetinuePriority',
    { character_id: characterId, member_ids: memberIds }
  );
}

/**
 * Fetches the tech trees and every tech-capable building's progression state.
 *
 * @param characterId - Owning character
 * @returns Promise<tech_trees_dto> - Trees + building xp/learned nodes/orders
 *
 * Usage: Tech panel in the manor shows trees and per-building specialization
 */
export async function getTechTreesRequest(characterId: number): Promise<tech_trees_dto> {
  return await authenticatedPost<tech_trees_dto>('getTechTrees', { character_id: characterId });
}

/**
 * Spends a building's tech XP to learn a node in its tree.
 *
 * @param characterId - Owning character
 * @param buildingId - The tech-capable building
 * @param nodeId - Node id from getTechTreesRequest
 * @returns Promise<{ node_id: string; tech_xp: number; tech_nodes: string[] }>
 *
 * Usage: Tech panel learn button on a node card
 */
export async function learnTechNodeRequest(
  characterId: number,
  buildingId: number,
  nodeId: string
): Promise<{ node_id: string; tech_xp: number; tech_nodes: string[] }> {
  return await authenticatedPost<{ node_id: string; tech_xp: number; tech_nodes: string[] }>(
    'learnTechNode',
    { character_id: characterId, building_id: buildingId, node_id: nodeId }
  );
}

/**
 * Starts a forge order at a tech-capable building (pays materials up front).
 *
 * @param characterId - Owning character
 * @param buildingId - The building (must have learned the recipe's tech node)
 * @param itemId - Equipment item with a `craft` block
 * @returns Promise<{ forge_order: unknown }>
 *
 * Usage: Armory "forge" button on a craftable item
 */
export async function startForgeOrderRequest(
  characterId: number,
  buildingId: number,
  itemId: string
): Promise<{ forge_order: unknown }> {
  return await authenticatedPost<{ forge_order: unknown }>('startForgeOrder', {
    character_id: characterId,
    building_id: buildingId,
    item_id: itemId
  });
}

/**
 * Fetches the fiefdom's armory (gear) and general storage (items).
 *
 * @param characterId - Owning character
 * @returns Promise<retinue_gear_dto> - Armory + storage lists (item configs merged)
 *
 * Usage: Retinue gear/storage panel
 */
export async function getRetinueGearRequest(characterId: number): Promise<retinue_gear_dto> {
  return await authenticatedPost<retinue_gear_dto>('getRetinueGear', {
    character_id: characterId
  });
}

/**
 * Equips an armory item to a member (level/slot/class checked server-side).
 *
 * @param characterId - Owning character
 * @param armoryId - Armory item row id
 * @param memberId - Target member
 * @returns Promise<{ member_id: number; slot: string; item_id: string }>
 *
 * Usage: Armory "equip" for a member card
 */
export async function equipGearRequest(
  characterId: number,
  armoryId: number,
  memberId: number
): Promise<{ member_id: number; slot: string; item_id: string }> {
  return await authenticatedPost<{ member_id: number; slot: string; item_id: string }>('equipGear', {
    character_id: characterId,
    armory_id: armoryId,
    member_id: memberId
  });
}

/**
 * De-equips a member's slot back to the armory (basic kit fills the slot).
 *
 * @param characterId - Owning character
 * @param memberId - The member
 * @param slot - Slot id ("weapon" | "armor" | "mount" | "potions")
 * @returns Promise<{ member_id: number; slot: string; item_id: string }>
 */
export async function deassignGearRequest(
  characterId: number,
  memberId: number,
  slot: string
): Promise<{ member_id: number; slot: string; item_id: string }> {
  return await authenticatedPost<{ member_id: number; slot: string; item_id: string }>(
    'deassignGear',
    { character_id: characterId, member_id: memberId, slot }
  );
}

/**
 * Sells an armory item or a storage item at the config sell discount.
 *
 * @param characterId - Owning character
 * @param kind - 'armory' (needs armoryId) or 'storage' (needs itemId)
 * @param armoryId - Armory row id (kind armory)
 * @param itemId - Storage item id (kind storage)
 * @returns Promise<{ gold: number }> - Gold received
 *
 * Usage: Sell button on stashed gear / surplus storage items
 */
export async function sellItemRequest(
  characterId: number,
  kind: 'armory' | 'storage',
  armoryId: number,
  itemId: string
): Promise<{ gold: number }> {
  return await authenticatedPost<{ gold: number }>('sellItem', {
    character_id: characterId,
    kind,
    armory_id: armoryId,
    item_id: itemId
  });
}

/**
 * Purchases a gear item from "the city" at its extreme city_price markup.
 *
 * @param characterId - Owning character
 * @param itemId - Equipment item with a city_price
 * @returns Promise<{ item_id: string; armory_id: number; gold: number }>
 *
 * Usage: Armory "buy from city" button (a gold-sink bypass of the blacksmith)
 */
export async function buyGearCityRequest(
  characterId: number,
  itemId: string
): Promise<{ item_id: string; armory_id: number; gold: number }> {
  return await authenticatedPost<{ item_id: string; armory_id: number; gold: number }>(
    'buyGearCity',
    { character_id: characterId, item_id: itemId }
  );
}

/**
 * Hires a teacher: starts a one-at-a-time training timer on a tech-capable
 * building, granting xp when it completes (retinue.json training block).
 *
 * @param characterId - Owning character
 * @param buildingId - A building with a tech tree and no active training
 * @returns Promise<{ training: unknown }> - The started training timer
 *
 * Usage: Tech panel "hire teacher" (on-demand, no inventory)
 */
export async function hireTeacherRequest(
  characterId: number,
  buildingId: number
): Promise<{ training: unknown }> {
  return await authenticatedPost<{ training: unknown }>('hireTeacher', {
    character_id: characterId,
    building_id: buildingId
  });
}

/**
 * Consumes one training item (a book) from general storage to start a training
 * timer on a tech-capable building.
 *
 * @param characterId - Owning character
 * @param buildingId - A building with a tech tree and no active training
 * @param itemId - items.json item with xp_grant + training_duration_hours
 * @returns Promise<{ training: unknown; item_id: string }>
 *
 * Usage: Storage panel "apply" on a book
 */
export async function applyBookItemRequest(
  characterId: number,
  buildingId: number,
  itemId: string
): Promise<{ training: unknown; item_id: string }> {
  return await authenticatedPost<{ training: unknown; item_id: string }>('applyBookItem', {
    character_id: characterId,
    building_id: buildingId,
    item_id: itemId
  });
}

/**
 * Converts a caught error into a user-displayable message.
 *
 * @param error - The caught error
 * @returns string - Displayable message
 *
 * Usage: Combat screens show this in alerts instead of raw errors
 */
export function handle_combat_error(error: unknown): string {
  if (error instanceof Error) {
    return error.message;
  }
  return String(error);
}
