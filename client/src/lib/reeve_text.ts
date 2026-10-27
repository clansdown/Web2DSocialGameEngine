/**
 * Helpers for the Reeve advisor's text-id + args wire model.
 *
 * The server never sends display strings: every line is a text id plus numeric
 * or text-id arguments. These helpers collect the ids to load, resolve nested
 * ids to their translated strings, and substitute `{token}` placeholders.
 */

import type { ReeveArg, ReeveReport, ReeveSeverity, ReeveText } from './api';

/**
 * Formats a number for a text token without trailing `.0` noise.
 *
 * @param value - Numeric value from a Reeve arg
 * @returns Compact decimal string
 */
export function format_reeve_number(value: number): string {
  if (!Number.isFinite(value)) return '0';
  if (Number.isInteger(value)) return String(value);
  return value.toFixed(1);
}

/**
 * Recursively collects every text id referenced by a Reeve report, including
 * text ids used as argument values.
 *
 * @param report - Reeve report (or null)
 * @returns Sorted, unique text ids
 */
export function collect_reeve_text_ids(report: ReeveReport | null): string[] {
  const ids = new Set<string>();

  const visit = (value: unknown): void => {
    if (Array.isArray(value)) {
      for (const item of value) visit(item);
      return;
    }
    if (value === null || typeof value !== 'object') return;

    const record = value as Record<string, unknown>;
    if (typeof record.text_id === 'string' && record.text_id.length > 0) {
      ids.add(record.text_id);
    }
    if (record.args && typeof record.args === 'object') {
      const args = record.args as Record<string, unknown>;
      for (const arg of Object.values(args)) {
        if (arg && typeof arg === 'object' &&
            typeof (arg as Record<string, unknown>).text_id === 'string') {
          const text_id = (arg as Record<string, unknown>).text_id as string;
          if (text_id.length > 0) ids.add(text_id);
        }
      }
    }
    for (const [key, child] of Object.entries(record)) {
      if (key === 'text_id') continue;
      visit(child);
    }
  };

  visit(report);
  return [...ids].sort();
}

/**
 * Resolves one Reeve argument. Numbers are formatted; text-id arguments are
 * looked up in the loaded translation map.
 *
 * @param arg - Number or `{ text_id }`
 * @param texts - Loaded text-id → translated string map
 * @returns Display string (empty when the text id is missing)
 */
export function resolve_reeve_arg(
  arg: ReeveArg | undefined,
  texts: Record<string, string>
): string {
  if (arg === undefined) return '';
  if (typeof arg === 'number') return format_reeve_number(arg);
  if (arg && typeof arg === 'object' && typeof arg.text_id === 'string') {
    return texts[arg.text_id] ?? '';
  }
  return '';
}

/**
 * Resolves every argument in a Reeve text's `args` map.
 *
 * @param args - Raw argument map
 * @param texts - Loaded text map
 * @returns Token → display string map for text substitution
 */
export function resolve_reeve_args(
  args: Record<string, ReeveArg> | undefined,
  texts: Record<string, string>
): Record<string, string> {
  const resolved: Record<string, string> = {};
  if (!args) return resolved;
  for (const [key, value] of Object.entries(args)) {
    resolved[key] = resolve_reeve_arg(value, texts);
  }
  return resolved;
}

/**
 * Resolves a Reeve text id through the loaded translation map.
 *
 * @param text - Reeve text descriptor
 * @param texts - Loaded text map
 * @returns Translated content (empty when missing)
 */
export function resolve_reeve_text(
  text: ReeveText | undefined,
  texts: Record<string, string>
): string {
  if (!text || !text.text_id) return '';
  return texts[text.text_id] ?? '';
}

/**
 * Replaces `{key}` and `<key>` placeholders in translated content.
 *
 * @param content - Translated Markdown source
 * @param tokens - Token → replacement map
 * @returns Content with placeholders substituted
 */
export function apply_reeve_tokens(
  content: string,
  tokens: Record<string, string>
): string {
  if (!content) return '';
  let result = content;
  for (const [key, value] of Object.entries(tokens)) {
    result = result.split(`{${key}}`).join(value);
    result = result.split(`<${key}>`).join(value);
  }
  return result;
}

/**
 * Maps a Reeve severity to a Bootstrap text colour class.
 *
 * @param severity - Report severity
 * @returns Bootstrap utility class
 */
export function reeve_severity_class(severity: ReeveSeverity): string {
  switch (severity) {
    case 'critical': return 'text-danger';
    case 'problem': return 'text-warning';
    case 'watch': return 'text-info';
    case 'ok': return 'text-success';
  }
  return 'text-muted';
}

const KNOWN_REEVE_ERRORS = new Set<string>([
  'insufficient_resources',
  'insufficient_arable_land',
  'insufficient_forest_land',
  'prerequisites_not_met',
  'dependencies_not_met',
  'exclusive_building_exists',
  'max_per_fiefdom_reached',
  'home_base_required',
  'invalid_location',
  'invalid_output',
  'output_locked',
  'invalid_rate',
  'building_not_found',
  'invalid_config',
  'invalid_resource'
]);

/**
 * Maps an action error code to its Reeve translation text id.
 *
 * @param code - Server error code (may be empty)
 * @returns A `reeve_error_*` text id; `reeve_error_unknown` when unrecognized
 */
export function reeve_error_text_id(code: string | undefined): string {
  if (code && KNOWN_REEVE_ERRORS.has(code)) {
    if (code === 'dependencies_not_met') return 'reeve_error_prerequisites_not_met';
    if (code === 'invalid_output' || code === 'output_locked' ||
        code === 'invalid_rate' || code === 'building_not_found' ||
        code === 'invalid_config' || code === 'invalid_resource') {
      return 'reeve_error_unknown';
    }
    return `reeve_error_${code}`;
  }
  return 'reeve_error_unknown';
}
