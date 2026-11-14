import type { Writable } from 'svelte/store';

/**
 * Context exposed by MiniGameViewport to the active mini-game component.
 *
 * The shell is the full-screen (and potentially rotated) container element.
 * `rotated` is true only while the shell is physically rotated by CSS; when the
 * device itself is landscape the shell is not CSS-rotated and native input
 * mapping works normally.
 */
export interface mini_game_viewport_context {
  shell: Writable<HTMLElement | null>;
  rotated: Writable<boolean>;
  forced: Writable<boolean>;
}

export const MINI_GAME_VIEWPORT_CONTEXT = 'mini_game_viewport';
