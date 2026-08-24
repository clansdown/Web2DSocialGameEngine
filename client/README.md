# Ravenest Client

The game client built with Vite + Svelte 5 + TypeScript.

## Overview

This is the browser-based game client that:
- Uses Vite for development and production builds
- Serves as the UI layer for the Ravenest game engine
- Integrates Bootstrap 5.3.8 for UI components with dark mode support
- Uses OPFS (Origin Private File System) for local storage

## Project Structure

```
client/
├── SimpleGame/ui/src/lib/     # Game engine library (imported via file: protocol)
├── src/
│   ├── App.svelte            # Main Svelte component
│   ├── main.ts               # Application entry point
│   ├── combat/               # Realtime combat game (NOT a mini-game)
│   │   ├── CombatScreen.svelte   # Top-level container: lobby → battle → results
│   │   ├── CombatNetClient.ts    # WebSocket client (auth, reconnect, typed sends)
│   │   ├── protocol.ts           # Wire protocol types + combat_codec interface
│   │   ├── CombatGame.svelte     # SimpleGame canvas: entity store + interpolation
│   │   ├── CombatHud.svelte      # Bootstrap battle bar (timer, counts, actions)
│   │   ├── CombatChat.svelte     # Team text chat over the WS
│   │   ├── CombatVoice.svelte    # WebRTC mesh voice (WS-relayed signaling)
│   │   └── MatchLobby.svelte     # PvE create/join-by-code
│   ├── components/           # Shared UI + manor board
│   │   ├── ManorMenu.svelte      # Manor board (roads, water power, construction)
│   │   ├── LandPatentPanel.svelte # Baron-track patent/create UI
│   │   ├── DialogOverlay.svelte  # Modal overlay shell
│   │   ├── GameText.svelte       # Text-system markdown (vellum)
│   │   └── StoryText.svelte      # Narrative markdown (silk)
│   ├── minigames/            # Mini-game screens
│   │   ├── tower_defense/
│   │   └── weeding/
│   └── lib/
│       ├── router.ts         # Hash-based in-app router (hub/activity/game routes)
│       └── storage.ts        # OPFS storage utilities
├── index.html                # Bootstrap 5.3.8 CDN (dark mode enabled)
├── package.json              # References simplegame via "file:./SimpleGame/ui"
└── vite.config.ts            # Vite configuration (proxies /api, /images, /ws)
```

## Realtime Combat

The combat game is a server-authoritative RTS (see `docs/combat_protocol.md`).
It is **not** a mini-game: it mounts from `App.svelte` as its own activity
(there is no hub card — it will be reached through game flow later) and
communicates over WebSocket `/ws/combat` instead of REST turns.

- **Networking**: `CombatNetClient` opens the socket, authenticates with the
  REST session token in its first message, and auto-reconnects with backoff.
  The vite dev server proxies `/ws` → `localhost:2290` (`ws: true`); nginx
  needs the upgrade headers in production.
- **State**: 10 Hz `match_state`/`match_update` messages; `CombatGame`
  maintains a local entity store (entity-level overwrite — no merge) and
  interpolates positions at rAF speed. Tick gaps trigger `request_state`.
- **Rendering**: the battle canvas uses SimpleGame's engine loop and
  `afterDraw` for custom unit rendering (no engine classes needed — units are
  server-simulated). Do not modify files under `SimpleGame/`.
- **Voice**: WebRTC mesh (browser standard) — the server only relays
  signaling; media flows peer-to-peer. STUN is a placeholder; configure TURN
  in `CombatVoice.svelte` for production.

## Manor (ManorMenu)

The manor is a SimpleGame canvas board reached as a hub activity
(`#/activity/manor`). All logic lives in `src/components/ManorMenu.svelte`
(the engine under `SimpleGame/` is never modified).

- **Entry & construction**: on first entry the client auto-places `home_base`
  at (0,0) and starts its construction timer. Building progress bars
  re-evaluate live every frame (`setProgressBar` getter); mill-pond bars
  resolve per-pond-type `construction_times` (`getConstructionTimes` —
  timber/stone ponds have their own build times).
- **HUD**: Main, Build, Economy, and Production are screen-space HUD objects
  fixed to the viewport (top-right column) with text-system labels.
- **Build palette**: buttons are ordered by `manor_ui.json` `build_order`
  (config-driven; the client's display/level/affordability filters are never
  overridden). Buttons grey out (`setDisabled`) when level-locked,
  prerequisite-unmet, or unaffordable. Placement uses a ghost with click or
  drag-and-release, and rejects river cells and occupied squares. Prerequisite
  checks are **stage-chain-aware** (`stageChain`/`satisfiedLevel`) — a
  villein/yeoman satisfies a `peasant` prerequisite.
- **Building info card**: clicking a completed building (including the manor
  house) opens a Bootstrap card showing its stage position, level, and actions —
  **Upgrade** (within-stage), **Convert to <successor>** (with the computed
  convert cost, mirroring the server's 80%-refund discount), **Upgrade Pond
  Type** (mill ponds), and **Demolish** (with confirm). Wired to the
  `upgrade`/`convert`/`demolish`/`upgrade_pond_type` Build actions.
- **Roads & morale**: roads auto-tile via `road_tiles_canonical` connectivity
  masks (procedural canvas tiles); buildings with `road_morale` radiate bonus
  points along connected road tiles, shown as "+X% production" in the
  Production panel.
- **Water power**: the river renders as procedural water tiles; head/tail
  races auto-tile like roads (`race_tiles_canonical`); water-powered buildings
  show a green (powered) / red (unpowered) dot; mill ponds show a
  `Type · load/capacity` label (e.g. "Earthen · 1/2").
- **Panels**: Economy (silver balance, imports/exports) and Production
  (per-output rate sliders, reserves) are toggleable HUD panels.

## Navigation (hash router)

In-app navigation uses a small hash router (`src/lib/router.ts`), so the
browser Back/Forward buttons and reload work naturally:

- `#/` → hub grid
- `#/activity/<id>[/…]` → hub activity (manor, tasks, chat, …); trailing
  segments are nested sub-routes (e.g. `#/activity/chat/thread/42`)
- `#/game/<game>/<level>` → mini-game level

Components read `route_store` and navigate with `navigate()` / `replace_route()`
(redirects) / `go_back()` (in-app Back buttons). Entering an activity or game
from an empty/foreign URL pushes a `#/` hub entry first, so the browser Back
button always returns to the hub grid before leaving the app. Reloading
restores the current screen from the URL; mobile app-switching fires no hash
events and never disturbs the open screen.

## Getting Started

### Development

```bash
npm run dev    # Start dev server at http://localhost:5173
npm run build  # Production build to dist/
npm run preview  # Preview production build
```

### Type Checking

```bash
npm run check   # Run svelte-check and TypeScript compiler
```

## Storage (OPFS)

The client uses the Origin Private File System for persistent local storage. All storage functions are in `src/lib/storage.ts`.

### Basic File Operations

```typescript
import * as storage from './lib/storage';

// Write file
await storage.writeFile('data/sample.txt', 'Hello, world!');

// Read file
const content = await storage.readFile('data/sample.txt');  // string | null

// Delete file
await storage.deleteFile('data/sample.txt');

// List directory contents
const files = await storage.listDirectory('');  // string[]
```

### Directory Operations

```typescript
// Ensure directory exists (creates parents recursively)
const dirHandle = await storage.ensureDirectory('data/subdir/nested');

// Delete directory and all contents
await storage.deleteDirectory('data');
```

### Config Storage (Key-Value)

Config values are stored as JSON files in `config/CONFIG_KEY.json`:

```typescript
// Set config value (generic type)
interface UserSettings {
    theme: 'light' | 'dark';
    soundEnabled: boolean;
    volume: number;
}

await storage.setConfig('userSettings', {
    theme: 'dark',
    soundEnabled: true,
    volume: 75
});

// Get config value with optional default (generic type)
// Returns the config value if exists and valid, otherwise returns defaultValue or null
const settings = await storage.getConfig<UserSettings>('userSettings', {
    theme: 'light',
    soundEnabled: false,
    volume: 50
});  
// Result: { theme: 'dark', soundEnabled: true, volume: 75 } if valid
//         { theme: 'light', soundEnabled: false, volume: 50 } if missing/invalid

// Type-safe helpers for primitive types
const volume = await storage.getConfigNumber('volume', 75);      // number
const username = await storage.getConfigString('username', '');  // string
const enabled = await storage.getConfigBoolean('enabled', false); // boolean

// Delete config
await storage.deleteConfig('userSettings');

// List all config keys
const keys = await storage.listConfigs();  // string[]

// Clear all configs
await storage.clearConfigs();
```

**Error Handling:**
- File not found: returns `null` (or default value if provided to `getConfig`)
- Invalid JSON: returns `null` (or default value if provided to `getConfig`)
- Type-safe helpers validate runtime types and return default on mismatch

### OPFS Notes

- OPFS is browser-origin-private: data persists per site
- Not user-visible like the regular file system
- Storage quota varies by browser (typically 5-10% of free disk space)
- Data is cleared when user clears site data
- All paths use POSIX-style forward slashes (`/`)

## Game Engine Integration

The SimpleGame engine is imported as a local dependency:

```typescript
import { simplegame, GameObject, gameClasses } from 'simplegame';
```

Engine files are in `SimpleGame/ui/src/lib/` and are hot-reloaded automatically when modified.

### Available Engine Modules

From `SimpleGame/ui/src/lib/`:
- `simplegame.ts` - Main game loop and state management
- `gameclasses.ts` - GameObject, Player, Enemy, Projectile, Item classes
- `collision.ts` - Collision detection system
- `layout.ts` - Layout and positioning utilities
- `button.ts` - Button component
- `audio.ts` - Audio handling
- `util.ts` - Utility functions (Position2D, box2, matrix2 types)

## UI Framework

Bootstrap 5.3.8 is loaded via CDN with dark mode enabled:

```html
<html lang="en" data-bs-theme="dark">
```

Bootstrap is bundled in `index.html` with:
- CSS: `https://cdn.jsdelivr.net/npm/bootstrap@5.3.8/dist/css/bootstrap.min.css`
- JS: `https://cdn.jsdelivr.net/npm/bootstrap@5.3.8/dist/js/bootstrap.bundle.min.js`

## Display Components

The client provides three reusable display components for showing text and content:

| Component | File | Role |
|-----------|------|------|
| **`DialogOverlay`** | `src/components/DialogOverlay.svelte` | Modal shell — dark backdrop, centered panel, title, Continue button. Use for any full-screen overlay that needs a dismiss action. Content via `children` snippet (preferred) or legacy `body` prop. |
| **`StoryText`** | `src/components/StoryText.svelte` | Silk-texture background. Use for narrative/story text (king's messages, intro lore). Supports markdown via `text` prop or arbitrary content via `children` snippet. |
| **`GameText`** | `src/components/GameText.svelte` | Vellum-texture background. Use for game text (descriptions, tooltips, UI text). Supports markdown via `text` prop or arbitrary content via `children` snippet. |

**Usage guidelines:**
- For narrative overlays (king's messages, story reveals): nest `StoryText` inside `DialogOverlay`
- For inline story text on a page: use `StoryText` alone
- For game descriptions and tooltips: use `GameText`
- Pass markdown content via the `text` prop, or use `children` for custom HTML/Svelte content

## Text System

All user-facing text comes from the text system — never hardcode display strings.

- Text files live in `game/text/<lang>/<id>.txt` (Markdown format; English is the source, other languages fall back to it).
- **Fetching**: use `loadTexts(textIds)` / `loadText(textId)` from `src/lib/text.ts`. They read the current language and character stores, route to the correct endpoint, and cache results.
  - With a selected character → authenticated `getCharacterTexts` (server substitutes `{male|female}` gender tokens and `{character_name}`).
  - No character (pre-auth screens) → public `getTexts`.
- **Auto-fetch rendering**: `GameText` and `StoryText` accept an `id` prop (`<GameText id="td_ongoing_info" />`) that fetches the text reactively and renders its Markdown as HTML. Optional `tokens` prop replaces `{key}`/`<key>` placeholders before rendering.
- **New strings**: create `game/text/en/<id>.txt`, then fetch it with `loadTexts` (or pass the ID to `GameText`/`StoryText`).
- The client never sends `sex` or the character name — the server applies those substitutions itself.

## IDE Setup

**Recommended:** VS Code + Svelte extension

TypeScript configuration uses strict mode with Svelte 5 support. See `tsconfig.json` and `tsconfig.app.json` for full configuration.

## Build Output

Production builds output to `dist/` with:
- Minified and bundled JavaScript
- Optimized assets
- Source maps for debugging
