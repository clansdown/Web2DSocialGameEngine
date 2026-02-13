import * as storage from './storage';
import type { Character } from './api';

const CONFIG_USERNAME = 'username';
const CONFIG_PASSWORD = 'password';
const CONFIG_CURRENT_CHARACTER_ID = 'current_character_id';

let sessionToken: string | null = null;
let inMemoryUsername: string | null = null;
let inMemoryPassword: string | null = null;

export function getSessionToken(): string | null {
  return sessionToken;
}

export function setSessionToken(token: string | null): void {
  sessionToken = token;
}

export function getInMemoryCredentials(): { username: string; password: string } | null {
  if (inMemoryUsername && inMemoryPassword) {
    return { username: inMemoryUsername, password: inMemoryPassword };
  }
  return null;
}

export function setInMemoryCredentials(username: string, password: string): void {
  inMemoryUsername = username;
  inMemoryPassword = password;
}

export function clearInMemoryCredentials(): void {
  inMemoryUsername = null;
  inMemoryPassword = null;
}

export async function loadStoredCredentials(): Promise<{ username: string; password: string } | null> {
  const username = await storage.getConfigString(CONFIG_USERNAME, '');
  const password = await storage.getConfigString(CONFIG_PASSWORD, '');
  if (username && password) {
    return { username, password };
  }
  return null;
}

export async function saveCredentialsLocally(username: string, password: string): Promise<void> {
  await storage.setConfig(CONFIG_USERNAME, username);
  await storage.setConfig(CONFIG_PASSWORD, password);
}

export async function clearStoredCredentials(): Promise<void> {
  await storage.deleteConfig(CONFIG_USERNAME);
  await storage.deleteConfig(CONFIG_PASSWORD);
}

export async function loadCurrentCharacterId(): Promise<number | null> {
  const id = await storage.getConfigNumber(CONFIG_CURRENT_CHARACTER_ID, 0);
  return id || null;
}

export async function saveCurrentCharacterId(characterId: number): Promise<void> {
  await storage.setConfig(CONFIG_CURRENT_CHARACTER_ID, characterId);
}

export function hasSession(): boolean {
  return sessionToken !== null;
}

export async function logout(): Promise<void> {
  sessionToken = null;
  inMemoryUsername = null;
  inMemoryPassword = null;
  await clearStoredCredentials();
}

export function getStoredUsername(): string | null {
  return inMemoryUsername;
}
