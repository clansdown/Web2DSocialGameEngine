import type { UITexture } from './api';

/** Minimum vertical scale applied to the smallest texture for short content. */
export const TEXTURE_MIN_STRETCH: number = 0.7;

/** How strongly native height breaks ties between near-identical aspect ratios. */
const TEXTURE_HEIGHT_WEIGHT: number = 0.08;

/** Keep the current texture unless another is meaningfully better. */
const TEXTURE_CURRENT_HYSTERESIS: number = 0.05;

/** Resolved 9-slice layout for the current panel size. */
export interface TextureLayout {
  /** Source-image pixels for border-image-slice (top/bottom). */
  slice_vertical_px: number;
  /** Source-image pixels for border-image-slice (left/right). */
  slice_horizontal_px: number;
  /** Rendered border thickness (top/bottom). */
  border_vertical_px: number;
  /** Rendered border thickness (left/right). */
  border_horizontal_px: number;
  /** Text padding inside the border (top/bottom). */
  padding_vertical_px: number;
  /** Text padding inside the border (left/right). */
  padding_horizontal_px: number;
}

/**
 * Returns the minimum panel height that keeps the smallest texture readable.
 *
 * @param textures - Textures returned by getUITextures
 * @returns Smallest native height times the minimum stretch, or 0
 */
export function texture_min_height(textures: UITexture[]): number {
  if (textures.length === 0) return 0;
  return Math.min(...textures.map((texture: UITexture): number => texture.height)) * TEXTURE_MIN_STRETCH;
}

/**
 * Scores a texture against the panel's aspect ratio. Height is a mild tiebreaker
 * so a much larger texture is not chosen when two candidates have similar shapes.
 *
 * @param texture - Candidate texture
 * @param panel_width - Rendered panel width
 * @param panel_height - Candidate's resulting panel height
 * @returns Lower is better
 */
function texture_score(texture: UITexture, panel_width: number, panel_height: number): number {
  const panel_aspect: number = panel_width / panel_height;
  const texture_aspect: number = texture.width / texture.height;
  const aspect_score: number = Math.abs(Math.log(panel_aspect / texture_aspect));
  const height_score: number = Math.abs(Math.log(panel_height / texture.height));
  return aspect_score + TEXTURE_HEIGHT_WEIGHT * height_score;
}

/**
 * Returns true when two textures reference the same served image. Comparing by
 * URL keeps hysteresis stable across Svelte state-proxy object identities.
 *
 * @param left - First texture
 * @param right - Second texture
 * @returns True when both are present and have the same URL
 */
function same_texture(left: UITexture | null, right: UITexture | null): boolean {
  return !!left && !!right && left.url === right.url;
}

/**
 * Computes the outer panel height a candidate texture would produce for the
 * given content. This is the stable input to texture selection: it depends only
 * on the content size, panel width, texture constants, and configured padding,
 * never on the currently applied panel height.
 *
 * @param texture - Candidate texture
 * @param panel_width - Rendered panel width
 * @param content_height - Height of the text content, excluding padding
 * @param base_padding_vertical - Component padding_vertical_px
 * @param base_padding_horizontal - Component padding_horizontal_px
 * @param min_panel_height - Minimum panel height from the smallest texture
 * @returns Actual outer panel height for that texture
 */
function texture_target_height(
  texture: UITexture,
  panel_width: number,
  content_height: number,
  base_padding_vertical: number,
  base_padding_horizontal: number,
  min_panel_height: number
): number {
  const candidate_layout: TextureLayout = compute_texture_layout(
    texture,
    panel_width,
    content_height,
    base_padding_vertical,
    base_padding_horizontal,
    min_panel_height
  );
  return Math.max(0, content_height) + 2 * (
    candidate_layout.border_vertical_px + candidate_layout.padding_vertical_px
  );
}

/**
 * Selects the texture whose own resulting panel aspect best matches its native
 * aspect. Each candidate is scored against the height its layout would produce,
 * which avoids the selected-texture -> height -> selected-texture feedback
 * loop. A small hysteresis keeps the current texture stable across resize steps.
 *
 * @param textures - Textures returned by getUITextures
 * @param panel_width - Rendered panel width
 * @param content_height - Height of the text content, excluding padding
 * @param base_padding_vertical - Component padding_vertical_px
 * @param base_padding_horizontal - Component padding_horizontal_px
 * @param min_panel_height - Minimum panel height from the smallest texture
 * @param current_texture - Previously selected texture, if any
 * @returns Best texture, or null when the list is empty or width is unmeasured
 */
export function select_texture_for_content(
  textures: UITexture[],
  panel_width: number,
  content_height: number,
  base_padding_vertical: number,
  base_padding_horizontal: number,
  min_panel_height: number,
  current_texture: UITexture | null
): UITexture | null {
  if (textures.length === 0 || panel_width <= 0) return null;

  let best_texture: UITexture = textures[0];
  let best_score: number = Infinity;
  for (const texture of textures) {
    const target_height: number = texture_target_height(
      texture,
      panel_width,
      content_height,
      base_padding_vertical,
      base_padding_horizontal,
      min_panel_height
    );
    const score: number = texture_score(texture, panel_width, target_height);
    if (score < best_score) {
      best_texture = texture;
      best_score = score;
    }
  }

  const current_match: UITexture | null = current_texture
    ? textures.find((texture: UITexture): boolean => same_texture(texture, current_texture)) ?? null
    : null;
  if (current_match) {
    const current_target_height: number = texture_target_height(
      current_match,
      panel_width,
      content_height,
      base_padding_vertical,
      base_padding_horizontal,
      min_panel_height
    );
    const current_score: number = texture_score(
      current_match,
      panel_width,
      current_target_height
    );
    if (current_score <= best_score + TEXTURE_CURRENT_HYSTERESIS) {
      return current_match;
    }
  }

  return best_texture;
}

/**
 * Compares two resolved texture layouts field by field. The measuring effect
 * uses this to avoid writing a fresh layout object when nothing changed.
 *
 * @param left - Previously applied layout, or null
 * @param right - Newly computed layout
 * @returns True when every layout field is identical
 */
export function texture_layouts_equal(left: TextureLayout | null, right: TextureLayout): boolean {
  if (!left) return false;
  return (
    left.slice_vertical_px === right.slice_vertical_px &&
    left.slice_horizontal_px === right.slice_horizontal_px &&
    left.border_vertical_px === right.border_vertical_px &&
    left.border_horizontal_px === right.border_horizontal_px &&
    left.padding_vertical_px === right.padding_vertical_px &&
    left.padding_horizontal_px === right.padding_horizontal_px
  );
}

/**
 * Resolves the slice insets for a texture, falling back to the component's
 * configured text padding when the per-file entry is missing.
 *
 * @param texture - Selected texture
 * @param fallback_vertical - Component padding_vertical_px
 * @param fallback_horizontal - Component padding_horizontal_px
 * @returns Source-image slice insets
 */
function texture_slices(
  texture: UITexture,
  fallback_vertical: number,
  fallback_horizontal: number
): { vertical: number; horizontal: number } {
  const vertical: number = texture.slice_vertical_px && texture.slice_vertical_px > 0
    ? texture.slice_vertical_px
    : fallback_vertical;
  const horizontal: number = texture.slice_horizontal_px && texture.slice_horizontal_px > 0
    ? texture.slice_horizontal_px
    : fallback_horizontal;
  return { vertical, horizontal };
}

/**
 * Computes the rendered border widths and inner padding for a 9-sliced texture.
 *
 * The total inset in each axis is the larger of the component's configured text
 * padding and the texture's slice. This keeps text off the ornate frame while
 * preserving the documented safe area for textures like vellum.
 *
 * @param texture - Selected texture
 * @param panel_width - Rendered panel width (border-box)
 * @param content_height - Height of the text content, excluding padding
 * @param base_padding_vertical - Component padding_vertical_px
 * @param base_padding_horizontal - Component padding_horizontal_px
 * @param min_panel_height - Minimum panel height from the smallest texture
 * @returns Rendered border and padding values
 */
export function compute_texture_layout(
  texture: UITexture,
  panel_width: number,
  content_height: number,
  base_padding_vertical: number,
  base_padding_horizontal: number,
  min_panel_height: number
): TextureLayout {
  const slices: { vertical: number; horizontal: number } =
    texture_slices(texture, base_padding_vertical, base_padding_horizontal);

  const safe_height: number = Math.max(0, content_height);
  const scale_x: number = panel_width / texture.width;
  const effective_horizontal: number = Math.max(base_padding_horizontal, slices.horizontal);
  const border_horizontal: number = slices.horizontal * scale_x;
  const total_horizontal: number = effective_horizontal * scale_x;
  const padding_horizontal: number = Math.max(0, total_horizontal - border_horizontal);

  const effective_vertical: number = Math.max(base_padding_vertical, slices.vertical);
  const available_vertical: number = texture.height - 2 * effective_vertical;
  const total_vertical: number = available_vertical > 0
    ? effective_vertical * safe_height / available_vertical
    : effective_vertical * (safe_height / texture.height);

  let target_height: number = safe_height + 2 * total_vertical;
  if (min_panel_height > target_height) {
    target_height = min_panel_height;
  }

  const scale_y: number = target_height / texture.height;
  const border_vertical: number = slices.vertical * scale_y;
  const actual_total_vertical: number = (target_height - safe_height) / 2;
  const padding_vertical: number = Math.max(0, actual_total_vertical - border_vertical);

  return {
    slice_vertical_px: slices.vertical,
    slice_horizontal_px: slices.horizontal,
    border_vertical_px: border_vertical,
    border_horizontal_px: border_horizontal,
    padding_vertical_px: padding_vertical,
    padding_horizontal_px: padding_horizontal
  };
}
