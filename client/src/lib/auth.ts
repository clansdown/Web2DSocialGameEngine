import * as storage from './storage';
import type { Character } from './api';

const CONFIG_USERNAME = 'username';
const CONFIG_PASSWORD = 'password';
const CONFIG_CURRENT_CHARACTER_ID = 'current_character_id';

let sessionToken: string | null = null;
let inMemoryUsername: string | null = null;
let inMemoryPassword: string | null = null;

/**
 * Gets the current session token from memory.
 * Returns null if no active session.
 * 
 * @param none
 * @returns string | null - Current session token or null
 * 
 * Usage: Used to get token for API requests
 */
export function getSessionToken(): string | null {
  return sessionToken;
}

/**
 * Sets the session token in memory.
 * Called after successful login or token refresh.
 * 
 * @param token - New session token or null to clear
 * @returns void
 * 
 * Usage: Called after login/refresh to establish session
 */
export function setSessionToken(token: string | null): void {
  sessionToken = token;
}

/**
 * Gets username and password stored in memory.
 * Used for token refresh when session expires.
 * 
 * @param none
 * @returns { username: string; password: string } | null - Credentials or null
 * 
 * Usage: Used internally for token refresh
 */
export function getInMemoryCredentials(): { username: string; password: string } | null {
  if (inMemoryUsername && inMemoryPassword) {
    return { username: inMemoryUsername, password: inMemoryPassword };
  }
  return null;
}

/**
 * Stores username and password in memory for session restoration.
 * Called after successful login. Credentials not persisted to disk.
 * 
 * @param username - User's account username
 * @param password - User's account password
 * @returns void
 * 
 * Usage: Called after login to enable token refresh
 */
export function setInMemoryCredentials(username: string, password: string): void {
  inMemoryUsername = username;
  inMemoryPassword = password;
}

/**
 * Clears in-memory username and password.
 * Called on logout or when session refresh fails.
 * 
 * @param none
 * @returns void
 * 
 * Usage: Called during logout or auth failure
 */
export function clearInMemoryCredentials(): void {
  inMemoryUsername = null;
  inMemoryPassword = null;
}

/**
 * Loads stored credentials from OPFS for auto-login.
 * Reads username and password from config storage.
 * 
 * @param none
 * @returns Promise<{ username: string; password: string } | null> - Stored credentials or null
 * 
 * Usage: Called on app mount for automatic login
 */
export async function loadStoredCredentials(): Promise<{ username: string; password: string } | null> {
  const username = await storage.getConfigString(CONFIG_USERNAME, '');
  const password = await storage.getConfigString(CONFIG_PASSWORD, '');
  if (username && password) {
    return { username, password };
  }
  return null;
}

/**
 * Saves username and password to OPFS for future auto-login.
 * Warning: Stores credentials in plaintext - security risk.
 * 
 * @param username - Username to store
 * @param password - Password to store
 * @returns Promise<void>
 * 
 * Usage: Called when user checks "remember me" on login
 */
export async function saveCredentialsLocally(username: string, password: string): Promise<void> {
  await storage.setConfig(CONFIG_USERNAME, username);
  await storage.setConfig(CONFIG_PASSWORD, password);
}

/**
 * Deletes stored credentials from OPFS.
 * 
 * @param none
 * @returns Promise<void>
 * 
 * Usage: Called on logout or when auto-login fails
 */
export async function clearStoredCredentials(): Promise<void> {
  await storage.deleteConfig(CONFIG_USERNAME);
  await storage.deleteConfig(CONFIG_PASSWORD);
}

/**
 * Loads the last selected character ID from OPFS storage.
 * Used to restore the previously selected character on app load.
 * 
 * @param none
 * @returns Promise<number | null> - Character ID or null if none saved
 * 
 * Usage: Called on login to restore character selection
 */
export async function loadCurrentCharacterId(): Promise<number | null> {
  const id = await storage.getConfigNumber(CONFIG_CURRENT_CHARACTER_ID, 0);
  return id || null;
}

/**
 * Saves the selected character ID to OPFS storage.
 * Persists selection for restoration on next app load.
 * 
 * @param characterId - ID of the character to save as current
 * @returns Promise<void>
 * 
 * Usage: Called when user selects a character
 */
export async function saveCurrentCharacterId(characterId: number): Promise<void> {
  await storage.setConfig(CONFIG_CURRENT_CHARACTER_ID, characterId);
}

/**
 * Checks if there is an active session in memory.
 * 
 * @param none
 * @returns boolean - true if session token exists, false otherwise
 * 
 * Usage: Used throughout app to check auth state
 */
export function hasSession(): boolean {
  return sessionToken !== null;
}

/**
 * Performs complete logout by clearing all session data.
 * Clears session token, in-memory credentials, and stored credentials.
 * 
 * @param none
 * @returns Promise<void>
 * 
 * Usage: Called when user clicks logout button
 */
export async function logout(): Promise<void> {
  sessionToken = null;
  inMemoryUsername = null;
  inMemoryPassword = null;
  await clearStoredCredentials();
}

/**
 * Gets the username stored in memory (not from OPFS).
 * 
 * @param none
 * @returns string | null - Username or null if not set
 * 
 * Usage: Used to display current user in UI
 */
export function getStoredUsername(): string | null {
  return inMemoryUsername;
}
