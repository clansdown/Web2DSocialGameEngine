/**
 * combat_configs.ts — shared one-shot cache for `/api/combatGetConfigs`.
 *
 * CombatGame needs structures/formations and the combatant art metadata;
 * DeployView needs the art metadata too. Fetching once and sharing the promise
 * avoids duplicate requests within a match.
 */
import { combatGetConfigsRequest, type combat_configs_dto } from '../lib/api';

/** In-flight/complete request, shared by every caller. */
let pending: Promise<combat_configs_dto> | null = null;

/**
 * Loads the combat configs once per page, sharing the in-flight request.
 *
 * @returns Promise resolving to the combat configs (rulesets, maps, art, …)
 */
export function load_combat_configs(): Promise<combat_configs_dto> {
  if (!pending) {
    pending = combatGetConfigsRequest().catch((error: unknown) => {
      // Allow a later attempt to retry rather than caching the failure.
      pending = null;
      throw error;
    });
  }
  return pending;
}
