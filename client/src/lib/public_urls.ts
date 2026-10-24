/**
 * Public origin resolution for the client.
 *
 * The Pages site, API server, and asset bucket may live on three different
 * origins. Vite bakes these bases into the build from `VITE_API_BASE_URL`,
 * `VITE_ASSETS_BASE_URL`, and `VITE_WS_BASE_URL`. Empty values mean "same
 * origin", which keeps the Vite dev proxy working and preserves same-origin
 * deployments behind a reverse proxy.
 *
 * Nothing in this module reads secrets. Production values come from `.deploy`
 * through install.sh; local values come from client/.env.development.local.
 */

/** Trimmed, slash-free API origin (empty = same origin). */
export const api_base: string = strip_trailing_slash(
  (import.meta.env.VITE_API_BASE_URL ?? '').trim()
);

/** Trimmed, slash-free asset origin (empty = same origin). */
export const assets_base: string = strip_trailing_slash(
  (import.meta.env.VITE_ASSETS_BASE_URL ?? '').trim()
);

/** Trimmed, slash-free WebSocket origin (empty = derive it). */
const websocket_base: string = strip_trailing_slash(
  (import.meta.env.VITE_WS_BASE_URL ?? '').trim()
);

/**
 * Removes trailing slashes from an origin or path prefix.
 *
 * @param value - Raw origin string
 * @returns The value without trailing slashes
 */
function strip_trailing_slash(value: string): string {
  return value.replace(/\/+$/, '');
}

/**
 * True for URLs that already carry a scheme (https:, data:, blob:) or a
 * protocol-relative `//host` prefix. Those are returned unchanged.
 *
 * @param value - Candidate URL
 * @returns Whether it is already absolute
 */
function is_absolute_url(value: string): boolean {
  return /^(?:[a-z][a-z0-9+.-]*:|\/\/)/i.test(value);
}

/**
 * Ensures a path starts with one slash.
 *
 * @param path - Path or URL fragment
 * @returns Path with a leading slash
 */
function with_leading_slash(path: string): string {
  return path.startsWith('/') ? path : `/${path}`;
}

/**
 * Joins a public origin with an absolute app path.
 *
 * @param base - Origin, or empty for same-origin
 * @param path - Path beginning with `/`
 * @returns Absolute URL, or the path when the base is empty
 */
function join_origin(base: string, path: string): string {
  if (!path) return base;
  if (is_absolute_url(path)) return path;
  const normalized = with_leading_slash(path);
  return base ? `${base}${normalized}` : normalized;
}

/**
 * Builds an API URL, e.g. `/api/login` -> `https://api.example.com/api/login`.
 *
 * @param path - API path beginning with `/api/`
 * @returns Absolute or same-origin API URL
 */
export function api_url(path: string): string {
  return join_origin(api_base, path);
}

/**
 * Builds a public-asset URL, e.g. `/images/ui/book.png`.
 *
 * @param path - Asset path beginning with `/`
 * @returns Absolute or same-origin asset URL
 */
export function asset_url(path: string): string {
  return join_origin(assets_base, path);
}

/**
 * Rebases a URL returned by the server. The server deliberately returns
 * relative paths (`/images/...`, `/api/avatar/...`); the client knows which
 * public origin each prefix belongs to.
 *
 * @param url - Server-provided URL
 * @returns URL on the correct public origin (or unchanged)
 */
export function resolve_server_url(url: string): string {
  if (!url || is_absolute_url(url)) return url;

  if (url === '/api/avatar' || url.startsWith('/api/avatar/')) {
    return api_url(url);
  }

  for (const prefix of ['/images', '/sounds', '/music']) {
    if (url === prefix || url.startsWith(`${prefix}/`)) {
      return asset_url(url);
    }
  }

  return url;
}

/**
 * Resolves the combat WebSocket URL. An explicit VITE_WS_BASE_URL wins;
 * otherwise the API origin is converted to ws/wss; otherwise the current page
 * origin is used (the Vite dev proxy path).
 *
 * @param path - WebSocket path beginning with `/ws/`
 * @returns Absolute ws:// or wss:// URL
 */
export function websocket_url(path: string): string {
  let base = websocket_base;

  if (!base && api_base) {
    base = api_base.replace(/^https:/i, 'wss:').replace(/^http:/i, 'ws:');
  }

  if (!base) {
    const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
    base = `${scheme}://${window.location.host}`;
  }

  // Accept https/http values in VITE_WS_BASE_URL by normalising the scheme.
  base = base.replace(/^https:/i, 'wss:').replace(/^http:/i, 'ws:');
  return join_origin(base, path);
}

/**
 * Rewrites root-relative asset URLs inside rendered Markdown/HTML. Text files
 * under game/text/ contain `<img src="/images/...">`, and tokens may inject the
 * same form. Applying this after markdown rendering keeps those references on
 * the asset origin without editing every text file.
 *
 * @param html - Rendered HTML
 * @returns HTML with `/images`, `/sounds`, and `/music` rebased
 */
export function rewrite_asset_urls(html: string): string {
  if (!assets_base) return html;

  return html
    .replace(
      /(\s(?:src|href)=["'])\/(images|sounds|music)\//g,
      (_match: string, attribute: string, prefix: string): string =>
        `${attribute}${assets_base}/${prefix}/`
    )
    .replace(
      /url\((["']?)\/(images|sounds|music)\//g,
      (_match: string, quote: string, prefix: string): string =>
        `url(${quote}${assets_base}/${prefix}/`
    );
}
