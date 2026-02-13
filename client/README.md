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
│   └── lib/
│       └── storage.ts        # OPFS storage utilities
├── index.html                # Bootstrap 5.3.8 CDN (dark mode enabled)
├── package.json              # References simplegame via "file:./SimpleGame/ui"
└── vite.config.ts            # Vite configuration
```

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

## IDE Setup

**Recommended:** VS Code + Svelte extension

TypeScript configuration uses strict mode with Svelte 5 support. See `tsconfig.json` and `tsconfig.app.json` for full configuration.

## Build Output

Production builds output to `dist/` with:
- Minified and bundled JavaScript
- Optimized assets
- Source maps for debugging
