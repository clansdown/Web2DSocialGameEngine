/**
 * Tower Defense HUD theme resolution.
 *
 * The server serves `tower_defense/ui_theme.json` with the kickoff payload.
 * Values are canvas units: Tower Defense uses a fixed virtual canvas and the
 * whole canvas is scaled with CSS, so text effects scale with the rest of the
 * HUD and never participate in browser-resize relayouts.
 */

import type { Button } from '../../../SimpleGame/ui/src/lib/button';
import type {
  Text,
  TextEffectOptions
} from '../../../SimpleGame/ui/src/lib/gameclasses';
import type {
  td_ink_style,
  td_outline_style,
  td_shadow_style,
  td_ui_surface,
  td_ui_theme
} from '../../lib/tower_defense_types';

export const TD_UI_SURFACES: readonly td_ui_surface[] = [
  'copper_readout',
  'cost_badge',
  'lives_readout',
  'picker_label',
  'context_button',
  'wave_button',
  'forfeit_button',
  'help_button',
  'menu_label'
];

const DEFAULT_OUTLINE: td_outline_style = {
  color: '#24140C',
  width: 1.5,
  opacity: 1,
  line_join: 'round'
};

const DEFAULT_SHADOW: td_shadow_style = {
  color: '#000000',
  offset_x: 1,
  offset_y: 1,
  blur: 1,
  opacity: 0.5
};

const DEFAULT_PRIMARY_INK: td_ink_style = {
  color: '#F1E4CC',
  outline: DEFAULT_OUTLINE,
  shadow: DEFAULT_SHADOW
};

const DEFAULT_SURFACES: Record<td_ui_surface, string> = {
  copper_readout: 'money',
  cost_badge: 'money',
  lives_readout: 'danger',
  picker_label: 'primary',
  context_button: 'primary',
  wave_button: 'primary',
  forfeit_button: 'primary',
  help_button: 'primary',
  menu_label: 'primary'
};

export const TD_UI_THEME_FALLBACK: td_ui_theme = {
  ink: {
    primary: DEFAULT_PRIMARY_INK,
    money: {
      color: '#E0A06A',
      outline: DEFAULT_OUTLINE,
      shadow: DEFAULT_SHADOW
    },
    danger: {
      color: '#E05A4F',
      outline: DEFAULT_OUTLINE,
      shadow: DEFAULT_SHADOW
    }
  },
  surfaces: DEFAULT_SURFACES
};

/**
 * Returns whether a value is a plain JSON-style object.
 *
 * @param value - Candidate value
 * @returns True for objects that are not arrays or null
 */
function is_record(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/**
 * Reads a non-empty string or returns the fallback.
 *
 * @param value - Candidate value
 * @param fallback - Value to use when the candidate is invalid
 * @returns A non-empty string
 */
function read_string(value: unknown, fallback: string): string {
  return typeof value === 'string' && value.trim() !== '' ? value : fallback;
}

/**
 * Reads a finite number or returns the fallback.
 *
 * @param value - Candidate value
 * @param fallback - Value to use when the candidate is invalid
 * @returns A finite number
 */
function read_number(value: unknown, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

/**
 * Reads a finite non-negative number or returns the fallback.
 *
 * @param value - Candidate value
 * @param fallback - Value to use when the candidate is invalid
 * @returns A finite non-negative number
 */
function read_non_negative_number(value: unknown, fallback: number): number {
  const number = read_number(value, fallback);
  return number >= 0 ? number : fallback;
}

/**
 * Reads a finite positive number or returns the fallback.
 *
 * @param value - Candidate value
 * @param fallback - Value to use when the candidate is invalid
 * @returns A finite positive number
 */
function read_positive_number(value: unknown, fallback: number): number {
  const number = read_number(value, fallback);
  return number > 0 ? number : fallback;
}

/**
 * Reads an opacity in the range 0..1 or returns the fallback.
 *
 * @param value - Candidate value
 * @param fallback - Value to use when the candidate is invalid
 * @returns An opacity in 0..1
 */
function read_opacity(value: unknown, fallback: number): number {
  const number = read_number(value, fallback);
  return Math.max(0, Math.min(1, number));
}

/**
 * Reads a valid canvas line join or returns the fallback.
 *
 * @param value - Candidate value
 * @param fallback - Value to use when the candidate is invalid
 * @returns A valid CanvasLineJoin
 */
function read_line_join(value: unknown, fallback: CanvasLineJoin): CanvasLineJoin {
  if (value === 'round' || value === 'bevel' || value === 'miter') {
    return value;
  }
  return fallback;
}

/**
 * Normalizes an outline block.
 *
 * @param value - Candidate outline object
 * @param fallback - Complete fallback outline
 * @returns A normalized outline
 */
function parse_outline(
  value: unknown,
  fallback: td_outline_style
): td_outline_style {
  if (!is_record(value)) return fallback;
  return {
    color: read_string(value.color, fallback.color),
    width: read_positive_number(value.width, fallback.width),
    opacity: read_opacity(value.opacity, fallback.opacity ?? 1),
    line_join: read_line_join(value.line_join, fallback.line_join ?? 'round')
  };
}

/**
 * Normalizes a shadow block.
 *
 * An absent or invalid shadow disables the shadow for that role rather than
 * inheriting a role's fallback shadow. This makes partial authoring explicit.
 *
 * @param value - Candidate shadow object
 * @returns A normalized shadow, or undefined when disabled
 */
function parse_shadow(value: unknown): td_shadow_style | undefined {
  if (!is_record(value)) return undefined;
  return {
    color: read_string(value.color, '#000000'),
    offset_x: read_number(value.offset_x, 1),
    offset_y: read_number(value.offset_y, 1),
    blur: read_non_negative_number(value.blur, 1),
    opacity: read_opacity(value.opacity, 1)
  };
}

/**
 * Normalizes one semantic ink role.
 *
 * @param value - Candidate ink object
 * @param fallback - Complete fallback ink for this role
 * @returns A normalized ink style
 */
function parse_ink(value: unknown, fallback: td_ink_style): td_ink_style {
  if (!is_record(value)) return fallback;
  return {
    color: read_string(value.color, fallback.color),
    outline: parse_outline(value.outline, fallback.outline),
    shadow: 'shadow' in value ? parse_shadow(value.shadow) : undefined
  };
}

/**
 * Normalizes the server-served HUD theme.
 *
 * Missing or malformed fields fall back to `TD_UI_THEME_FALLBACK`. The
 * returned object is safe to use as a per-session immutable snapshot.
 *
 * @param raw - Raw `ui_theme` value from the kickoff response
 * @returns A complete normalized theme
 */
export function load_td_ui_theme(raw: unknown): td_ui_theme {
  if (!is_record(raw)) return TD_UI_THEME_FALLBACK;

  const ink: Record<string, td_ink_style> = { ...TD_UI_THEME_FALLBACK.ink };
  const ink_raw = raw.ink;
  if (is_record(ink_raw)) {
    for (const [role_name, role_value] of Object.entries(ink_raw)) {
      if (!role_name) continue;
      const fallback = ink[role_name] ?? DEFAULT_PRIMARY_INK;
      ink[role_name] = parse_ink(role_value, fallback);
    }
  }

  const surfaces: Record<td_ui_surface, string> = {
    ...TD_UI_THEME_FALLBACK.surfaces
  };
  const surfaces_raw = raw.surfaces;
  if (is_record(surfaces_raw)) {
    for (const surface of TD_UI_SURFACES) {
      const role_name = surfaces_raw[surface];
      if (typeof role_name === 'string' && role_name.trim() !== '') {
        surfaces[surface] = role_name;
      }
    }
  }

  return { ink, surfaces };
}

/**
 * Resolves a surface to its semantic ink role.
 *
 * Precedence: surface role -> primary -> built-in fallback primary.
 *
 * @param theme - Normalized theme
 * @param surface - HUD surface name
 * @returns The resolved ink style
 */
export function resolve_td_ink(
  theme: td_ui_theme,
  surface: td_ui_surface
): td_ink_style {
  const role_name = theme.surfaces[surface];
  const role = role_name ? theme.ink[role_name] : undefined;
  return role ?? theme.ink.primary ?? DEFAULT_PRIMARY_INK;
}

/**
 * Converts a theme ink style into SimpleGame text-effect options.
 *
 * @param ink - Resolved ink style
 * @returns Paint-only SimpleGame effect options
 */
export function td_ink_to_text_effects(ink: td_ink_style): TextEffectOptions {
  const effects: TextEffectOptions = {
    outline: {
      color: ink.outline.color,
      width: ink.outline.width,
      opacity: ink.outline.opacity,
      lineJoin: ink.outline.line_join
    }
  };
  if (ink.shadow) {
    effects.shadow = {
      color: ink.shadow.color,
      offsetX: ink.shadow.offset_x,
      offsetY: ink.shadow.offset_y,
      blur: ink.shadow.blur,
      opacity: ink.shadow.opacity
    };
  }
  return effects;
}

/**
 * Applies a theme ink style to a SimpleGame `Text` object.
 *
 * Effects are paint-only; this never changes text measurement or layout.
 *
 * @param text - Target text object
 * @param ink - Resolved ink style
 */
export function apply_td_text_ink(text: Text, ink: td_ink_style): void {
  text.foreground = ink.color;
  text.setTextEffects(td_ink_to_text_effects(ink));
}

/**
 * Applies a theme ink style to a SimpleGame `Button` label.
 *
 * Effects are paint-only; this never changes button dimensions or hitboxes.
 *
 * @param button - Target button
 * @param ink - Resolved ink style
 */
export function apply_td_button_ink(button: Button, ink: td_ink_style): void {
  button.setForegroundColor(ink.color);
  button.setTextEffects(td_ink_to_text_effects(ink));
}

/**
 * Converts a CSS colour to an RGBA colour with the supplied opacity.
 *
 * Canvas normalizes the input colour; hex and rgb/rgba forms are then parsed.
 * Unsupported forms are returned unchanged as a best-effort fallback.
 *
 * @param ctx - Canvas context used for colour normalization
 * @param color - CSS colour string
 * @param opacity - Opacity multiplier in 0..1
 * @returns A colour string suitable for `shadowColor`
 */
function canvas_color_with_opacity(
  ctx: CanvasRenderingContext2D,
  color: string,
  opacity: number
): string {
  if (!(opacity < 1)) return color;

  ctx.save();
  ctx.fillStyle = '#000000';
  ctx.fillStyle = color;
  const normalized = typeof ctx.fillStyle === 'string' ? ctx.fillStyle : color;
  ctx.restore();

  const hex_match =
    /^#([0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.exec(normalized);
  if (hex_match) {
    const hex = hex_match[1];
    let r: number;
    let g: number;
    let b: number;
    let a = 1;
    if (hex.length === 3 || hex.length === 4) {
      r = parseInt(hex[0] + hex[0], 16);
      g = parseInt(hex[1] + hex[1], 16);
      b = parseInt(hex[2] + hex[2], 16);
      if (hex.length === 4) a = parseInt(hex[3] + hex[3], 16) / 255;
    } else {
      r = parseInt(hex.slice(0, 2), 16);
      g = parseInt(hex.slice(2, 4), 16);
      b = parseInt(hex.slice(4, 6), 16);
      if (hex.length === 8) a = parseInt(hex.slice(6, 8), 16) / 255;
    }
    return `rgba(${r}, ${g}, ${b}, ${Math.max(0, Math.min(1, a * opacity))})`;
  }

  const rgb_match =
    /^rgba?\(\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)(?:\s*,\s*([0-9.]+))?\s*\)$/.exec(
      normalized
    );
  if (rgb_match) {
    const r = Math.max(0, Math.min(255, Number(rgb_match[1])));
    const g = Math.max(0, Math.min(255, Number(rgb_match[2])));
    const b = Math.max(0, Math.min(255, Number(rgb_match[3])));
    const source_alpha = rgb_match[4] !== undefined ? Number(rgb_match[4]) : 1;
    const alpha = Math.max(0, Math.min(1, source_alpha * opacity));
    return `rgba(${Math.round(r)}, ${Math.round(g)}, ${Math.round(b)}, ${alpha})`;
  }

  return color;
}

/**
 * Applies or clears the canvas shadow for a theme ink style.
 *
 * @param ctx - Canvas context
 * @param shadow - Shadow style, or undefined to clear
 */
function apply_canvas_shadow(
  ctx: CanvasRenderingContext2D,
  shadow: td_shadow_style | undefined
): void {
  if (!shadow) {
    ctx.shadowColor = 'transparent';
    ctx.shadowBlur = 0;
    ctx.shadowOffsetX = 0;
    ctx.shadowOffsetY = 0;
    return;
  }
  ctx.shadowColor = canvas_color_with_opacity(
    ctx,
    shadow.color,
    shadow.opacity ?? 1
  );
  ctx.shadowBlur = shadow.blur;
  ctx.shadowOffsetX = shadow.offset_x;
  ctx.shadowOffsetY = shadow.offset_y;
}

/**
 * Draws canvas text with the same outline/shadow ordering as SimpleGame.
 *
 * When an outline exists, the outline casts the shadow and the fill is drawn
 * cleanly on top. When no outline exists, the fill casts the shadow.
 * The caller must set font, text alignment, and text baseline.
 *
 * @param ctx - Canvas context
 * @param text - Text to draw
 * @param x - Draw x coordinate
 * @param y - Draw y coordinate
 * @param ink - Resolved ink style
 */
export function draw_td_text_with_ink(
  ctx: CanvasRenderingContext2D,
  text: string,
  x: number,
  y: number,
  ink: td_ink_style
): void {
  if (!text) return;

  if (ink.outline) {
    ctx.save();
    apply_canvas_shadow(ctx, ink.shadow);
    ctx.strokeStyle = ink.outline.color;
    ctx.lineWidth = ink.outline.width;
    ctx.lineJoin = ink.outline.line_join ?? 'round';
    const outline_opacity = ink.outline.opacity ?? 1;
    if (outline_opacity < 1) {
      ctx.globalAlpha *= outline_opacity;
    }
    ctx.strokeText(text, x, y);
    ctx.restore();

    ctx.save();
    apply_canvas_shadow(ctx, undefined);
    ctx.fillStyle = ink.color;
    ctx.fillText(text, x, y);
    ctx.restore();
    return;
  }

  ctx.save();
  apply_canvas_shadow(ctx, ink.shadow);
  ctx.fillStyle = ink.color;
  ctx.fillText(text, x, y);
  ctx.restore();
}
