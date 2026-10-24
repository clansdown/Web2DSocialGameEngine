/**
 * unit_sprites.ts — hand-authored combat art (rts_design.md D24/D71).
 *
 * Combat renders procedurally by default: class-colored circles plus the
 * mandatory team ring / HP bar / facing chevron overlays. Real art drops in
 * behind the same API.
 *
 * Directional art (StarCraft-style): each class may declare animation clips in
 * its combatant config (`animations`), e.g.
 *
 *     "animations": {
 *       "walk": { "fps": 10, "views": { "e": "e", "se": "se", ... } },
 *       "idle": { "fps": 1,  "views": { "e": "e", "se": "se", ... } }
 *     }
 *
 * Frames live at `game/images/combatants/<class>/<clip>/<set>/<n>.png`, numbered
 * from 1. The frame *count* is discovered from the directory contents by the
 * server (never declared in config), so an animation may have any number of
 * frames. `idle` is the standing pose: one image per set directory
 * (`.../idle/<set>/1.png`) drawn whenever the unit is stationary — no idle
 * animation required.
 *
 * `views` maps a compass direction to a frame set: either its own name (the
 * identity default — author one set per direction, no mirroring) or
 * `{ set, mirror }` to reuse another direction's set with a horizontal flip
 * (mirrored across the vertical axis). Mirroring is therefore optional and opted
 * into per direction. A direction with no authored set falls back to the nearest
 * loaded set, so partial art still renders.
 *
 * The server only sends clips/sets that exist on disk (`handleCombatGetConfigs`
 * discovers the frame counts and narrows `views`), so the client never requests
 * frames that are not there. A class with no directional art keeps the
 * procedural draw. Rest priority while stationary: `idle` → walk frame 1.
 *
 * Legacy: a flat, rotatable `combatants/<class>/idle/1.png` (no set
 * subdirectories) is still supported when a class has no directional animations.
 * `unit_sprite_url` points at that frame for icon use (e.g. the retinue
 * class-icon fallback).
 */

import type { combatant_animations_dto, combatant_clip_dto } from '../lib/api';
import { asset_url } from '../lib/public_urls';

/** Root URL for combatant sprites (public assets, /images/combatants/*). */
const COMBATANT_SPRITE_ROOT = asset_url('/images/combatants');

/**
 * Compass direction order indexed by `direction_index`, matching the server's
 * `k_combat_directions`: 0=e, 1=se, 2=s, 3=sw, 4=w, 5=nw, 6=n, 7=ne. Canvas y
 * grows downward, so a positive facing angle rotates toward +y (south).
 */
export const COMBAT_DIRECTIONS: readonly string[] = ['e', 'se', 's', 'sw', 'w', 'nw', 'n', 'ne'];

/** Interpolated-distance threshold below which a unit counts as stationary. */
const MOVE_EPSILON = 0.0005;

/** A resolved direction view plus its currently-loaded frames. */
interface view_resolution {
  set: string;
  mirror: boolean;
  frames: HTMLImageElement[];
}

/** A frame ready to draw, with the horizontal flip the view asks for. */
export interface resolved_frame {
  image: HTMLImageElement;
  mirror: boolean;
}

/** Per-class animation clips, set by `set_combatant_art`. */
let art: Record<string, combatant_animations_dto> = {};

/** True once the server configs have been applied (even if art is empty). */
let art_loaded = false;

/** Legacy flat `idle/1.png` per class (rotatable fallback). */
const legacy_idle_cache = new Map<string, HTMLImageElement>();

/** Directional frames per `class|clip|set`; entries load asynchronously. */
const frames_cache = new Map<string, HTMLImageElement[]>();

/** Shared empty result (avoids allocating on every miss). */
const EMPTY_FRAMES: HTMLImageElement[] = [];

/** Classes requested for preload, re-run when art metadata arrives. */
const known_classes = new Set<string>();

/** Builds the cache key for one directional frame set. */
function frame_key(unit_class: string, clip: string, set: string): string {
  return `${unit_class}|${clip}|${set}`;
}

/**
 * Maps a facing angle (radians) to a compass sector index.
 *
 * @param facing - Facing angle in radians (0 = east, +y = south)
 * @returns Sector index 0..7 into COMBAT_DIRECTIONS
 */
export function direction_index(facing: number): number {
  const step = Math.PI / 4;
  let index = Math.round(facing / step) % 8;
  if (index < 0) index += 8;
  return index;
}

/**
 * Returns true while a unit is still closing the gap to its interpolated
 * target — used to decide when to play the walk cycle.
 *
 * @param from_x - Current interpolated x (normalized 0..1)
 * @param from_y - Current interpolated y
 * @param to_x - Latest server x
 * @param to_y - Latest server y
 * @returns True when the distance exceeds the movement epsilon
 */
export function is_moving(from_x: number, from_y: number, to_x: number, to_y: number): boolean {
  return Math.hypot(to_x - from_x, to_y - from_y) > MOVE_EPSILON;
}

/** Creates an async-decoding image for a URL. */
function load_image(source: string): HTMLImageElement {
  const image = new Image();
  image.decoding = 'async';
  image.src = source;
  return image;
}

/** Starts loading a class's legacy flat idle frame (once). */
function preload_legacy_idle(unit_class: string): void {
  if (legacy_idle_cache.has(unit_class)) return;
  legacy_idle_cache.set(
    unit_class,
    load_image(`${COMBATANT_SPRITE_ROOT}/${unit_class}/idle/1.png`)
  );
}

/** Starts loading one directional set's frames (once). */
function preload_frames(unit_class: string, clip: string, set: string, count: number): void {
  const key = frame_key(unit_class, clip, set);
  if (frames_cache.has(key)) return;
  const frames: HTMLImageElement[] = [];
  for (let frame = 1; frame <= count; frame++) {
    frames.push(load_image(`${COMBATANT_SPRITE_ROOT}/${unit_class}/${clip}/${set}/${frame}.png`));
  }
  frames_cache.set(key, frames);
}

/** Every authored set a clip references (explicit views, else all directions). */
function clip_sets(clip: combatant_clip_dto): string[] {
  if (!clip.views) return [...COMBAT_DIRECTIONS];
  const sets = new Set<string>();
  for (const direction of COMBAT_DIRECTIONS) {
    const entry = clip.views[direction];
    if (typeof entry === 'string') sets.add(entry);
    else if (entry && typeof entry === 'object' && entry.set) sets.add(entry.set);
  }
  return [...sets];
}

/** Returns only the frames that have decoded successfully (in authored order). */
function ready_frames(unit_class: string, clip: string, set: string): HTMLImageElement[] {
  const frames = frames_cache.get(frame_key(unit_class, clip, set));
  if (!frames) return EMPTY_FRAMES;
  // Fast path (the steady state): every frame decoded — return the cached array.
  let all_ready = true;
  for (const image of frames) {
    if (!image.complete || image.naturalWidth === 0) {
      all_ready = false;
      break;
    }
  }
  if (all_ready) return frames;
  return frames.filter((image) => image.complete && image.naturalWidth > 0);
}

/**
 * Stores the per-class animation metadata from the server configs and loads any
 * frames for classes that have already been requested.
 *
 * @param next - class id → animation clips (from combatGetConfigs.combatant_art)
 * @returns void
 */
export function set_combatant_art(next: Record<string, combatant_animations_dto> | null | undefined): void {
  art = next ?? {};
  art_loaded = true;
  preload_unit_sprites(known_classes);
}

/**
 * Starts loading sprites for the given unit classes. Safe to call often (every
 * state update / frame): already-requested classes are skipped.
 *
 * @param unit_classes - Unit class ids to ensure are loading
 * @returns void
 */
export function preload_unit_sprites(unit_classes: Iterable<string>): void {
  for (const unit_class of unit_classes) {
    if (!unit_class) continue;
    known_classes.add(unit_class);

    const animations = art_loaded ? art[unit_class] : undefined;
    if (!animations) {
      preload_legacy_idle(unit_class);
      continue;
    }

    let queued = false;
    for (const [clip, definition] of Object.entries(animations)) {
      if (!definition) continue;
      // The frame count is discovered server-side from the set directory; fall
      // back to a single frame if a clip arrives without it.
      const frame_count =
        typeof definition.frames === 'number' && definition.frames > 0 ? definition.frames : 1;
      for (const set of clip_sets(definition)) {
        preload_frames(unit_class, clip, set, frame_count);
        queued = true;
      }
    }
    if (!queued) preload_legacy_idle(unit_class);
  }
}

/**
 * Resolves the best direction view for a clip: the declared view when its set is
 * loaded, otherwise the nearest loaded direction.
 *
 * @param unit_class - Unit class id
 * @param clip - Clip id (e.g. "walk")
 * @param direction - Sector index (0..7, see direction_index)
 * @returns The resolved view with ready frames, or null when none are loaded
 */
function resolve_view(unit_class: string, clip: string, direction: number): view_resolution | null {
  const definition = art[unit_class]?.[clip];
  if (!definition) return null;

  const make = (index: number): view_resolution => {
    const name = COMBAT_DIRECTIONS[index];
    const entry = definition.views?.[name];
    let set = name;
    let mirror = false;
    if (typeof entry === 'string') {
      set = entry;
    } else if (entry && typeof entry === 'object' && entry.set) {
      set = entry.set;
      mirror = !!entry.mirror;
    }
    return { set, mirror, frames: ready_frames(unit_class, clip, set) };
  };

  const direct = make(direction);
  if (direct.frames.length > 0) return direct;
  for (let offset = 1; offset <= 4; offset++) {
    for (const candidate of [direction - offset, direction + offset]) {
      const index = ((candidate % 8) + 8) % 8;
      const view = make(index);
      if (view.frames.length > 0) return view;
    }
  }
  return null;
}

/**
 * Resolves the frame to draw for one class/clip/direction at a point in the
 * animation cycle. Returns null when no directional art is loaded (callers fall
 * back to the legacy sprite or the procedural draw).
 *
 * @param unit_class - Unit class id
 * @param clip - Clip id (e.g. "walk")
 * @param direction - Sector index (0..7)
 * @param anim_time - Seconds elapsed within the clip
 * @returns The frame + mirror flag, or null
 */
export function resolve_frame(
  unit_class: string,
  clip: string,
  direction: number,
  anim_time: number
): resolved_frame | null {
  const view = resolve_view(unit_class, clip, direction);
  if (!view || view.frames.length === 0) return null;
  const definition = art[unit_class]?.[clip];
  const fps = definition && typeof definition.fps === 'number' && definition.fps > 0
    ? definition.fps
    : 1;
  const raw_index = Math.floor(anim_time * fps);
  const index = ((raw_index % view.frames.length) + view.frames.length) % view.frames.length;
  return { image: view.frames[index], mirror: view.mirror };
}

/**
 * Resolves the body frame for a unit: its walk cycle while moving, else its
 * idle clip, else the walk rest pose (frame 1). Null → use the legacy sprite or
 * the procedural circle.
 *
 * @param unit_class - Unit class id
 * @param direction - Sector index (0..7)
 * @param moving - Whether the unit is currently walking
 * @param walk_time - Seconds accumulated while walking
 * @param idle_time - Seconds accumulated while stationary (idle clips only)
 * @returns The frame + mirror flag, or null
 */
export function resolve_body_frame(
  unit_class: string,
  direction: number,
  moving: boolean,
  walk_time: number,
  idle_time: number = 0
): resolved_frame | null {
  const animations = art[unit_class];
  if (!animations) return null;

  if (moving && animations.walk) {
    const frame = resolve_frame(unit_class, 'walk', direction, walk_time);
    if (frame) return frame;
  }
  if (animations.idle) {
    const frame = resolve_frame(unit_class, 'idle', direction, idle_time);
    if (frame) return frame;
  }
  if (animations.walk) {
    const frame = resolve_frame(unit_class, 'walk', direction, 0);
    if (frame) return frame;
  }
  return null;
}

/**
 * Returns the sprite URL for a unit class (its legacy flat idle frame).
 *
 * @param unit_class - The unit class id (e.g. "goblin", "knight")
 * @returns Absolute URL to the class's idle frame 1
 */
export function unit_sprite_url(unit_class: string): string {
  return `${COMBATANT_SPRITE_ROOT}/${unit_class}/idle/1.png`;
}

/**
 * Returns a fully loaded legacy sprite for a unit class, or null when there is
 * no art (or it has not loaded yet, or the load failed). Callers MUST fall back
 * to the procedural rendering when this returns null.
 *
 * @param unit_class - The unit class id
 * @returns The decoded image, or null
 */
export function get_unit_sprite(unit_class: string): HTMLImageElement | null {
  const image = legacy_idle_cache.get(unit_class);
  if (image && image.complete && image.naturalWidth > 0) return image;
  return null;
}
