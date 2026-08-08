/**
 * Minimal hash-based router for in-app navigation.
 *
 * Routes are encoded in the URL fragment so that:
 * - the browser Back/Forward buttons navigate between screens,
 * - reloading restores the current screen from the URL,
 * - mobile app-switching never disturbs the open screen (no hashchange fires).
 *
 * Route shapes:
 *   #/                      → hub grid
 *   #/activity/<id>[/…]     → hub activity (manor, tasks, chat, …); trailing
 *                             segments are opaque nested sub-routes (e.g. a
 *                             chat thread: #/activity/chat/thread/42)
 *   #/game/<game>/<level>[/…] → mini-game level (rest reserved for future use)
 *
 * Components read `route_store` (a Svelte store) and navigate with
 * `navigate()`, `replace_route()`, or `go_back()`.
 *
 * Back-stack invariant: when navigating into an activity/game from an
 * empty or foreign URL, a `#/` hub entry is pushed first, so the browser
 * Back button always returns to the hub grid before leaving the app.
 */

import { writable } from 'svelte/store';

export type route =
  | { type: 'hub'; rest: [] }
  | { type: 'activity'; id: string; rest: string[] }
  | { type: 'game'; game_id: string; level_id: number; rest: string[] };

/**
 * Parses a URL fragment into a route.
 * Unknown or malformed fragments resolve to the hub route.
 *
 * @param hash - The URL fragment including the leading '#'
 * @returns route - Parsed route
 *
 * Usage: Internal; also used to initialize the store from the current URL
 */
function parse_hash(hash: string): route {
  const segments = hash.replace(/^#\/?/, '').split('/').filter((s) => s.length > 0);
  const [head, second, third, ...rest] = segments;

  if (head === 'activity' && second) {
    return { type: 'activity', id: second, rest };
  }

  if (head === 'game' && second && third) {
    const level_id = Number(third);
    if (Number.isInteger(level_id) && level_id > 0) {
      return { type: 'game', game_id: second, level_id, rest };
    }
  }

  return { type: 'hub', rest: [] };
}

/**
 * Serializes a route back into a URL fragment.
 *
 * @param route - Route to serialize
 * @returns string - Fragment including the leading '#'
 *
 * Usage: Internal; used by navigate()/replace_route()
 */
function to_hash(route: route): string {
  switch (route.type) {
    case 'hub':
      return '#/';
    case 'activity':
      return route.rest.length > 0
        ? `#/activity/${route.id}/${route.rest.join('/')}`
        : `#/activity/${route.id}`;
    case 'game':
      return route.rest.length > 0
        ? `#/game/${route.game_id}/${route.level_id}/${route.rest.join('/')}`
        : `#/game/${route.game_id}/${route.level_id}`;
  }
}

/**
 * True when the given fragment is a known in-app route.
 *
 * @param hash - URL fragment including the leading '#'
 * @returns boolean - True for hub, activity, and game fragments
 *
 * Usage: Internal; decides whether a hub entry must be pushed first
 */
function is_in_app_hash(hash: string): boolean {
  return hash === '#/' || hash.startsWith('#/activity') || hash.startsWith('#/game');
}

/**
 * The current route as a Svelte store. Updated on hashchange and by the
 * navigation functions below.
 *
 * Usage: `$route_store.type === 'activity'` in components
 */
export const route_store = writable<route>(parse_hash(window.location.hash));

/**
 * Navigates to a route by pushing a new history entry.
 * The browser Back button then returns to the previous screen.
 *
 * @param route - Destination route
 * @returns void
 *
 * Usage: selectActivity → navigate({ type: 'activity', id: 'manor', rest: [] })
 */
export function navigate(route: route): void {
  const target = to_hash(route);
  const from = window.location.hash;

  if (from === target) {
    route_store.set(route);
    return;
  }

  if (route.type === 'hub') {
    // Hub is the root screen: replace rather than push so Back never
    // returns to a duplicate hub entry.
    if (from === '' || from === '#') {
      history.replaceState({ from: null }, '', target);
    } else {
      history.pushState({ from }, '', target);
    }
    route_store.set(route);
    return;
  }

  if (!is_in_app_hash(from)) {
    history.pushState({ from: from || null }, '', '#/');
  }
  history.pushState({ from }, '', target);
  route_store.set(route);
}

/**
 * Navigates to a route by replacing the current history entry.
 * Used for redirects (e.g. game completion) so the browser Back button
 * does not re-enter the screen that was just finished.
 *
 * @param route - Destination route
 * @returns void
 *
 * Usage: onMiniGameComplete → replace_route({ type: 'activity', id: 'tasks', rest: [] })
 */
export function replace_route(route: route): void {
  const target = to_hash(route);
  if (window.location.hash === target) {
    route_store.set(route);
    return;
  }
  history.replaceState(
    { from: history.state?.from ?? window.location.hash },
    '',
    target
  );
  route_store.set(route);
}

/**
 * Navigates back one history entry when the previous entry is an in-app
 * route; otherwise returns to the hub grid directly. Used by in-app Back
 * buttons so they always escape to an app screen.
 *
 * @param none
 * @returns void
 *
 * Usage: goBackToHub() in HubScreen and activity panels
 */
export function go_back(): void {
  const from = history.state?.from;
  if (typeof from === 'string' && is_in_app_hash(from)) {
    history.back();
  } else {
    navigate({ type: 'hub', rest: [] });
  }
}

window.addEventListener('hashchange', () => {
  route_store.set(parse_hash(window.location.hash));
});
