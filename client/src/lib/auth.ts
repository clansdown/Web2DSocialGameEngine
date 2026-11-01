import * as storage from './storage';
import { isAuthenticated } from './stores';
import { api_url } from './public_urls';

const CONFIG_USERNAME = 'username';
const CONFIG_PASSWORD = 'password';
const CONFIG_CURRENT_CHARACTER_ID = 'current_character_id';
const CONFIG_LANGUAGE = 'language';

let sessionToken: string | null = null;
let inMemoryUsername: string | null = null;

/**
 * Gets the current access token from memory. The token is never persisted.
 *
 * @returns string | null - Current access token or null
 */
export function getSessionToken(): string | null {
  return sessionToken;
}

/**
 * Sets the access token in memory. Called after login/refresh.
 *
 * @param token - New access token or null to clear
 */
export function setSessionToken(token: string | null): void {
  sessionToken = token;
  isAuthenticated.set(token !== null);
}

/**
 * Gets the signed-in username. Retained for callers that need the account name;
 * passwords are never stored in the browser.
 *
 * @returns { username: string; password: string } | null
 */
export function getInMemoryCredentials(): { username: string; password: string } | null {
  if (inMemoryUsername) {
    return { username: inMemoryUsername, password: '' };
  }
  return null;
}

/**
 * Stores the signed-in username in memory. The password argument is retained
 * only for source compatibility with older call sites and is ignored.
 *
 * @param username - Account username
 * @param _password - Ignored (never stored)
 */
export function setInMemoryCredentials(username: string, _password: string): void {
  inMemoryUsername = username;
}

/**
 * Stores only the username. Password persistence was removed with the session
 * rewrite; refresh is handled by the HttpOnly `ravenest_refresh` cookie.
 *
 * @param username - Account username
 */
export function setStoredUsername(username: string): void {
  inMemoryUsername = username;
}

/**
 * Clears the in-memory username.
 */
export function clearInMemoryCredentials(): void {
  inMemoryUsername = null;
}

/**
 * Legacy credential loader. Passwords are no longer persisted, so this always
 * returns null. Startup session restoration uses the refresh cookie instead.
 *
 * @returns Promise<null>
 */
export async function loadStoredCredentials(): Promise<{ username: string; password: string } | null> {
  return null;
}

/**
 * Legacy credential writer. Kept as a no-op so old call sites compile; no
 * credentials are written to OPFS.
 *
 * @param _username - Ignored
 * @param _password - Ignored
 */
export async function saveCredentialsLocally(_username: string, _password: string): Promise<void> {
  // Deliberately empty: the access token lives in memory and the refresh token
  // lives in an HttpOnly cookie.
}

/**
 * Deletes legacy credential keys from OPFS.
 */
export async function clearStoredCredentials(): Promise<void> {
  await storage.deleteConfig(CONFIG_USERNAME);
  await storage.deleteConfig(CONFIG_PASSWORD);
}

/**
 * Loads the last selected character ID from OPFS storage.
 *
 * @returns Promise<number | null> - Character ID or null
 */
export async function loadCurrentCharacterId(): Promise<number | null> {
  const id = await storage.getConfigNumber(CONFIG_CURRENT_CHARACTER_ID, 0);
  return id || null;
}

/**
 * Saves the selected character ID to OPFS storage.
 *
 * @param characterId - Character ID
 */
export async function saveCurrentCharacterId(characterId: number): Promise<void> {
  await storage.setConfig(CONFIG_CURRENT_CHARACTER_ID, characterId);
}

/**
 * Loads the stored language preference from OPFS.
 *
 * @returns Promise<string | null> - Language code or null
 */
export async function loadStoredLanguage(): Promise<string | null> {
  const lang = await storage.getConfigString(CONFIG_LANGUAGE, '');
  return lang || null;
}

/**
 * Saves the language preference to OPFS.
 *
 * @param lang - Language code
 */
export async function saveStoredLanguage(lang: string): Promise<void> {
  await storage.setConfig(CONFIG_LANGUAGE, lang);
}

/**
 * Clears the stored language preference.
 */
export async function clearStoredLanguage(): Promise<void> {
  await storage.deleteConfig(CONFIG_LANGUAGE);
}

/**
 * True when an access token is present in memory.
 *
 * @returns boolean
 */
export function hasSession(): boolean {
  return sessionToken !== null;
}

/**
 * Performs a complete logout: revokes the refresh session on the server,
 * clears the in-memory access token/username, and removes legacy OPFS
 * credentials.
 */
export async function logout(): Promise<void> {
  try {
    await fetch(api_url('/api/auth/logout'), {
      method: 'POST',
      credentials: 'include',
      headers: { 'Content-Type': 'application/json' },
      body: '{}'
    });
  } catch {
    // Local logout still proceeds if the server is unreachable.
  }
  sessionToken = null;
  inMemoryUsername = null;
  isAuthenticated.set(false);
  await clearStoredCredentials();
}

/**
 * Gets the username stored in memory.
 *
 * @returns string | null
 */
export function getStoredUsername(): string | null {
  return inMemoryUsername;
}
