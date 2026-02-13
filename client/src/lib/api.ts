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
