/**
 * portrait_frames.ts — cached access to the decorative portrait-frame catalog
 * (game/config/portrait_frames.json, served by /api/getPortraitFrames).
 *
 * The catalog describes, per frame variant, the overlay image plus the rectangle
 * (in source-image pixels) the portrait image is mapped into. The frame is an
 * ordinary transparent PNG drawn ON TOP of the portrait, so the rectangle can be
 * any shape the overlay art needs — the opaque frame pixels occlude whatever
 * spills past the opening.
 *
 * The catalog is static, so it is fetched at most once per page load and shared
 * by every RetinuePortrait instance.
 */
import { getPortraitFramesRequest, type portrait_frame_dto, type portrait_frames_dto } from './api';

/** Last successfully loaded catalog (variant id -> frame), or null. */
let cached_frames: Record<string, portrait_frame_dto> | null = null;

/** In-flight request, so concurrent callers share one network round-trip. */
let inflight: Promise<Record<string, portrait_frame_dto>> | null = null;

/**
 * Loads the portrait-frame catalog, fetching it only once. Concurrent and
 * repeated calls share the same cached promise/result.
 *
 * @returns Promise resolving to the variant-id -> frame map (empty on failure)
 */
export function load_portrait_frames(): Promise<Record<string, portrait_frame_dto>> {
  if (cached_frames) return Promise.resolve(cached_frames);
  if (inflight) return inflight;

  inflight = getPortraitFramesRequest()
    .then((response: portrait_frames_dto) => {
      cached_frames = response.frames ?? {};
      return cached_frames;
    })
    .catch((error: unknown) => {
      // Allow a later attempt to retry rather than caching the failure.
      inflight = null;
      throw error;
    });

  return inflight;
}

/**
 * Synchronous accessor for an already-loaded frame. Returns null until the
 * catalog has loaded (or when the variant is unknown).
 *
 * @param variant - Frame variant id (e.g. "recruit", "management")
 * @returns The frame descriptor, or null
 */
export function get_cached_portrait_frame(variant: string): portrait_frame_dto | null {
  if (!cached_frames) return null;
  return cached_frames[variant] ?? null;
}
