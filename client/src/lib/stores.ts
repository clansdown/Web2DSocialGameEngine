import { writable, derived } from 'svelte/store';
import type { Character } from './api';

export interface User {
  id: number;
  username: string;
  adult: boolean;
}

export const user = writable<User | null>(null);
export const characters = writable<Character[]>([]);
export const currentCharacter = writable<Character | null>(null);
export const authLoading = writable(false);
export const authError = writable<string | null>(null);

export const isLoggedIn = derived(
  [user, currentCharacter],
  ([$user]) => $user !== null
);
