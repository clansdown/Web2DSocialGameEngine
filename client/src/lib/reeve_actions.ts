/**
 * Executes one Reeve recommendation against the existing game endpoints.
 *
 * The Reeve never invents a new mutation path: every action here is the same
 * endpoint the player would otherwise use manually. Build actions preserve the
 * server's `error_code` so the panel can render a translated explanation.
 */

import { apiPost } from './api';
import type { ApiResponse, BuildResponse, ReeveAction } from './api';

export interface ReeveActionAuth {
  username: string;
  token: string;
}

export interface ReeveActionResult {
  ok: boolean;
  error_code?: string;
  error?: string;
}

function failure<T>(response: ApiResponse<T>): ReeveActionResult {
  return {
    ok: false,
    error_code: response.error_code,
    error: response.error
  };
}

/**
 * Executes a Reeve action through the normal API.
 *
 * @param action - Structured action emitted by the Reeve
 * @param fiefdomId - Fiefdom the action belongs to
 * @param characterId - Owner character (build/upgrade/convert endpoints)
 * @param auth - Session credentials
 * @returns Success flag plus the server error code (when failed)
 */
export async function execute_reeve_action(
  action: ReeveAction,
  fiefdomId: number,
  characterId: number,
  auth: ReeveActionAuth
): Promise<ReeveActionResult> {
  const options = { username: auth.username, token: auth.token };

  switch (action.kind) {
    case 'build': {
      const response = await apiPost<BuildResponse>('Build', {
        action: 'create',
        fiefdom_id: fiefdomId,
        building_type: action.building_type,
        x: action.x,
        y: action.y,
        character_id: characterId
      }, options);
      return response.error ? failure(response) : { ok: true };
    }
    case 'upgrade': {
      const response = await apiPost<BuildResponse>('Build', {
        action: 'upgrade',
        fiefdom_id: fiefdomId,
        building_id: action.building_id,
        character_id: characterId
      }, options);
      return response.error ? failure(response) : { ok: true };
    }
    case 'convert': {
      const response = await apiPost<BuildResponse>('Build', {
        action: 'convert',
        fiefdom_id: fiefdomId,
        building_id: action.building_id,
        character_id: characterId
      }, options);
      return response.error ? failure(response) : { ok: true };
    }
    case 'build_road_link': {
      for (const cell of action.cells) {
        const response = await apiPost<BuildResponse>('Build', {
          action: 'create',
          fiefdom_id: fiefdomId,
          building_type: 'road',
          x: cell.x,
          y: cell.y,
          character_id: characterId
        }, options);
        if (response.error) return failure(response);
      }
      return { ok: true };
    }
    case 'set_reserve': {
      const response = await apiPost<{ reserves: Record<string, number> }>(
        'setFiefdomReserve', {
          fiefdom_id: fiefdomId,
          resource: action.resource,
          reserve: action.value
        }, options);
      return response.error ? failure(response) : { ok: true };
    }
    case 'set_import': {
      const response = await apiPost<{ import_settings: Record<string, boolean> }>(
        'setFiefdomImport', {
          fiefdom_id: fiefdomId,
          resource: action.resource,
          auto_import: action.enabled
        }, options);
      return response.error ? failure(response) : { ok: true };
    }
    case 'set_output_rate': {
      const response = await apiPost<{ output_rates: Record<string, number> }>(
        'setBuildingOutputRate', {
          building_id: action.building_id,
          output: action.output,
          rate: action.rate
        }, options);
      return response.error ? failure(response) : { ok: true };
    }
  }
  return { ok: false, error_code: 'unknown' };
}
