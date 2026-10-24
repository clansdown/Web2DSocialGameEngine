import * as auth from './auth';
import { resolve_server_url } from './public_urls';

/**
 * True when a URL points at the age-gated custom-avatar route, whether the URL
 * is relative or has already been rebased onto the API origin.
 *
 * @param url - Absolute or relative avatar URL
 * @returns Whether it is the custom-avatar endpoint
 */
function is_custom_avatar_url(url: string): boolean {
  try {
    return new URL(url, window.location.origin).pathname.startsWith('/api/avatar/');
  } catch {
    return false;
  }
}

/**
 * Appends the session credentials to an age-gated custom-avatar URL so it can
 * be used directly as an <img> src. Safe-pool and static URLs are rebased onto
 * the asset origin and pass through unchanged (they are public).
 *
 * The server resolves custom avatars to `/api/avatar/<character_id>` and
 * requires `?username=&token=` because a browser image request cannot carry a
 * POST auth body.
 *
 * @param url - Server-resolved avatar URL (possibly a custom-avatar route)
 * @returns The URL with credentials appended when required
 */
export function with_avatar_auth(url: string): string {
  if (!url) return url;
  const resolved = resolve_server_url(url);
  if (!is_custom_avatar_url(resolved)) return resolved;
  const creds = auth.getInMemoryCredentials();
  const token = auth.getSessionToken();
  if (!creds || !token) return resolved;
  const separator = resolved.includes('?') ? '&' : '?';
  return `${resolved}${separator}username=${encodeURIComponent(creds.username)}&token=${encodeURIComponent(token)}`;
}

/** Largest source file we will attempt to decode client-side (5 MB). */
const AVATAR_MAX_SOURCE_BYTES = 5 * 1024 * 1024;

/** Output edge length in pixels (the server caps dimensions at 256x256). */
const AVATAR_OUTPUT_SIZE = 256;

/** Quality for the lossy re-encode. */
const AVATAR_OUTPUT_QUALITY = 0.9;

/** The per-character avatar change cooldown, in seconds (24 hours). */
export const AVATAR_COOLDOWN_SECONDS = 24 * 60 * 60;

/** A decoded image plus its intrinsic size, usable as a canvas draw source. */
interface decoded_image {
  source: CanvasImageSource;
  width: number;
  height: number;
}

/**
 * True when the browser's canvas can encode WebP. Detected by asking the
 * canvas for a WebP data URI and checking the result's MIME prefix (browsers
 * without WebP encoding silently return a PNG data URI instead).
 *
 * @returns boolean - Whether `canvas.toDataURL('image/webp')` produced WebP
 */
export function canvas_supports_webp(): boolean {
  try {
    const canvas = document.createElement('canvas');
    canvas.width = 1;
    canvas.height = 1;
    return canvas.toDataURL('image/webp').startsWith('data:image/webp');
  } catch {
    return false;
  }
}

/**
 * Decodes a File into a draw source and its intrinsic size. Uses
 * `createImageBitmap` when available (fast path) and falls back to an
 * `<img>` + object URL.
 *
 * @param file - The image file to decode
 * @returns Promise<decoded_image> - Draw source + dimensions
 * @throws Error('image_decode_failed') when the file cannot be decoded
 */
async function decode_image_file(file: File): Promise<decoded_image> {
  if (typeof createImageBitmap === 'function') {
    const bitmap = await createImageBitmap(file);
    return { source: bitmap, width: bitmap.width, height: bitmap.height };
  }
  const url = URL.createObjectURL(file);
  try {
    const image = await new Promise<HTMLImageElement>((resolve, reject) => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error('image_decode_failed'));
      img.src = url;
    });
    return { source: image, width: image.naturalWidth, height: image.naturalHeight };
  } finally {
    URL.revokeObjectURL(url);
  }
}

/**
 * Converts a user-selected image into the payload the avatar upload endpoint
 * expects: a base64 data URI plus the matching format. The image is
 * center-cropped to a square and scaled to 256x256, then encoded as WebP when
 * the browser supports it, falling back to JPEG.
 *
 * @param file - The file chosen from an `<input type="file">`
 * @returns Promise<{ base64: string; format: string }> - data URI + format
 * @throws Error('not_an_image') when the file is not an image
 * @throws Error('image_too_large_source') when the source exceeds 5 MB
 * @throws Error('canvas_unavailable') when a 2D context cannot be created
 * @throws Error('image_decode_failed') when the file cannot be decoded
 *
 * Usage: ProfilePanel upload flow, before uploadCharacterAvatarRequest
 */
export async function file_to_avatar_payload(
  file: File
): Promise<{ base64: string; format: string }> {
  if (!file.type.startsWith('image/')) {
    throw new Error('not_an_image');
  }
  if (file.size > AVATAR_MAX_SOURCE_BYTES) {
    throw new Error('image_too_large_source');
  }

  const decoded = await decode_image_file(file);
  if (decoded.width <= 0 || decoded.height <= 0) {
    throw new Error('image_decode_failed');
  }
  const crop_size = Math.min(decoded.width, decoded.height);
  const crop_x = (decoded.width - crop_size) / 2;
  const crop_y = (decoded.height - crop_size) / 2;

  const canvas = document.createElement('canvas');
  canvas.width = AVATAR_OUTPUT_SIZE;
  canvas.height = AVATAR_OUTPUT_SIZE;
  const ctx = canvas.getContext('2d');
  if (!ctx) {
    throw new Error('canvas_unavailable');
  }
  ctx.drawImage(
    decoded.source,
    crop_x, crop_y, crop_size, crop_size,
    0, 0, AVATAR_OUTPUT_SIZE, AVATAR_OUTPUT_SIZE
  );

  const use_webp = canvas_supports_webp();
  const format = use_webp ? 'webp' : 'jpg';
  const mime = use_webp ? 'image/webp' : 'image/jpeg';
  return { base64: canvas.toDataURL(mime, AVATAR_OUTPUT_QUALITY), format };
}

/**
 * Seconds remaining before the character may change its portrait again.
 *
 * @param changed_at - characters.avatar_changed_at (unix seconds; 0 = never)
 * @param now_seconds - Current unix time in seconds
 * @returns number - Seconds remaining (0 when no cooldown is active)
 *
 * Usage: ProfilePanel disables mutation controls while > 0
 */
export function avatar_cooldown_remaining(
  changed_at: number | null | undefined,
  now_seconds: number
): number {
  if (!changed_at || changed_at <= 0) return 0;
  const elapsed = now_seconds - changed_at;
  if (elapsed >= AVATAR_COOLDOWN_SECONDS) return 0;
  return AVATAR_COOLDOWN_SECONDS - elapsed;
}
