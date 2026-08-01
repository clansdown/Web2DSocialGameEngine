/**
 * Unified text-system access for the client.
 *
 * The single entry point is loadTexts()/loadText(). It automatically:
 * - reads the current language from the language store,
 * - routes to the authenticated getCharacterTexts endpoint when a character is
 *   selected (server applies gender + {character_name} substitution), otherwise
 *   falls back to the public getTexts endpoint,
 * - caches results per (language, characterId, text-ids) to avoid re-fetching.
 *
 * All UI components should fetch display text through this module — never
 * hardcode strings in markup, and never call the raw API functions directly.
 */

import { get } from 'svelte/store';
import { language, currentCharacter } from './stores';
import { getTextsRequest, getCharacterTextsRequest } from './api';

const cache = new Map<string, Record<string, string>>();

/**
 * Builds the cache key for a text request given language and character context.
 * Includes sex and display name so cached text is invalidated when the
 * character's gender/name changes (e.g. after setCharacterSex).
 *
 * @param lang - Language code
 * @param character - Current character, or null for the public (no-character) context
 * @param textIds - Sorted array of text IDs (caller must sort for cache stability)
 * @returns Stable cache key string
 */
function cacheKey(lang: string, character: { id: number; sex: string | null; display_name: string } | null, textIds: string[]): string {
  const context = character === null
    ? 'public'
    : `char:${character.id}:${character.sex ?? 'none'}:${character.display_name}`;
  return `${lang}|${context}|${textIds.join(',')}`;
}

/**
 * Fetches multiple translated texts, routing to the authenticated character
 * endpoint when a character is selected. Results are cached per language,
 * character, and text-ID set.
 *
 * @param textIds - Text IDs to fetch (empty arrays return an empty map)
 * @returns Promise<Record<string, string>> - Map of text ID to content
 *
 * Usage: const texts = await loadTexts(['ui_patent_title', 'ui_patent_body']);
 */
export async function loadTexts(textIds: string[]): Promise<Record<string, string>> {
  if (textIds.length === 0) return {};

  const lang = get(language);
  const character = get(currentCharacter);
  const sorted = [...textIds].sort();
  const key = cacheKey(lang, character, sorted);

  const cached = cache.get(key);
  if (cached) return cached;

  let texts: Record<string, string>;
  if (character !== null) {
    texts = await getCharacterTextsRequest(character.id, sorted);
  } else {
    texts = await getTextsRequest(lang, sorted);
  }

  cache.set(key, texts);
  return texts;
}

/**
 * Fetches a single translated text via loadTexts().
 *
 * @param textId - Text ID to fetch
 * @returns Promise<string> - The text content (empty string if not found)
 *
 * Usage: const intro = await loadText('ui_path_select_intro');
 */
export async function loadText(textId: string): Promise<string> {
  const texts = await loadTexts([textId]);
  return texts[textId] || '';
}
