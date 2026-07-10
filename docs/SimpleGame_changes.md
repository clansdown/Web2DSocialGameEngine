# SimpleGame Changes Needed

Feature requests for the SimpleGame engine project to improve embedding support.

---

## 1. Image Load Error Recovery

**File:** `ui/src/lib/gameclasses.ts` — `GameObjectClass` constructor

**Current:** The `onload` handler sets `loaded = true`, but there is no `onerror` handler. If an image URL fails to load (404, network error), `loaded` stays `false` forever and `allClassesLoaded()` in `simplegame.ts` never returns `true`, blocking the game loop entirely.

**Request:** Add an `onerror` handler that sets `loaded = true` with fallback dimensions:

```typescript
this.image.onerror = () => {
    if (this.defaultWidth == 0) this.defaultWidth = 32;
    if (this.defaultHeight == 0) this.defaultHeight = 32;
    if (this.hitboxWidth == 0) this.hitboxWidth = this.defaultWidth;
    if (this.hitboxHeight == 0) this.hitboxHeight = this.defaultHeight;
    this.loaded = true;
};
```

## 2. External Setup Hook

**File:** `ui/src/game.ts`

**Current:** `setup()` is hardcoded to call `setup_brickbreaker()`. Embedding projects cannot inject their own game setup without modifying this file.

**Request:** Add `setCustomSetup(fn)` / `hasCustomSetup()`:

```typescript
let customSetupFunction: (() => void) | null = null;

export function setCustomSetup(fn: () => void): void {
    customSetupFunction = fn;
}

export function hasCustomSetup(): boolean {
    return customSetupFunction !== null;
}

export function setup() {
    if (customSetupFunction) {
        customSetupFunction();
        return;
    }
    setup_brickbreaker();
}
```

## 3. everyTick Callback Type

**File:** `ui/src/lib/simplegame.ts`

**Current:** `everyTick` accepts `() => void` but internally the callbacks receive `delta_t: number`:

```typescript
export function everyTick(callback : () => void) {
    tickWork.push(callback);
}
```

Where `tickWork: ((delta_t: number) => void)[]`.

**Request:** Change the parameter type to match:

```typescript
export function everyTick(callback: (delta_t: number) => void) {
    tickWork.push(callback);
}
```

This is a backwards-compatible change — existing `() => void` callbacks still type-check because TypeScript allows assigning functions with fewer parameters.

## 4. Public Game Loop Access

**File:** `ui/src/lib/simplegame.ts`

**Current:** `moveObjects(delta_t)`, `draw()`, and `mainGameLoop` are all private. Embedding projects that want to run their own frame loop (using requestAnimationFrame instead of the built-in setTimeout-based loop) cannot call movement or rendering.

**Request:** Export `moveObjects` and `draw` so embedding projects can run them in their own loop:

```typescript
export function moveObjects(delta_t: number) { ... }
export function draw() { ... }
```

Alternatively, export the full `gameLoop` function so callers can pass `delta_t` manually.
