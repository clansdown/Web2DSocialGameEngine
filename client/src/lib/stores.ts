import { writable, derived } from 'svelte/store';
import type { Character } from './api';

/**
 * User data structure containing account information.
 */
export interface User {
  id: number;
  username: string;
  adult: boolean;
}

/**
 * Svelte store containing the current user session data.
 * Set to null when logged out, contains User object when authenticated.
 * 
 * @type {writable<User | null>}
 * 
 * Usage: Use in components with $user to access current user
 */
export const user = writable<User | null>(null);

/**
 * Svelte store containing array of characters owned by the current user.
 * Populated after successful login.
 * 
 * @type {writable<Character[]>}
 * 
 * Usage: Use in components with $characters to access character list
 */
export const characters = writable<Character[]>([]);

/**
 * Svelte store containing the currently selected character.
 * Used for game actions and character-specific data.
 * 
 * @type {writable<Character | null>}
 * 
 * Usage: Use in components with $currentCharacter to access selected character
 */
export const currentCharacter = writable<Character | null>(null);

/**
 * Svelte store for auth-related loading state.
 * Set to true during login, account creation, or token refresh.
 * 
 * @type {writable<boolean>}
 * 
 * Usage: Use in components with $authLoading to show loading UI
 */
export const authLoading = writable(false);

/**
 * DEPRECATED: Use global error handler from './errors.ts' instead.
 * This store is kept for backwards compatibility with existing components.
 * 
 * @type {writable<string | null>}
 */
export const authError = writable<string | null>(null);

/**
 * Derived store that computes login state from user and currentCharacter.
 * True when both user is set and a character is selected.
 * 
 * @type {derived<[typeof user, typeof currentCharacter], boolean>}
 * 
 * Usage: Use in components with $isLoggedIn to check auth status
 */
export const isLoggedIn = derived(
  [user, currentCharacter],
  ([$user]) => $user !== null
);
