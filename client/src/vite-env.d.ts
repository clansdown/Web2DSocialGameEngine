/// <reference types="svelte" />
/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** API origin, e.g. https://api.example.com (empty = same origin). */
  readonly VITE_API_BASE_URL?: string;
  /** Public asset origin, e.g. https://assets.example.com (empty = same origin). */
  readonly VITE_ASSETS_BASE_URL?: string;
  /** WebSocket origin, e.g. wss://api.example.com (empty = derive from API). */
  readonly VITE_WS_BASE_URL?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
