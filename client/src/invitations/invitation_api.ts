/**
 * Player invitation-code API.
 *
 * Codes are created by the server, are single-use, and are only displayed in
 * plaintext to the player who created them (and to staff).
 */

import { authenticatedPost } from '../lib/api';
import type { registration_mode } from '../lib/api';

export type { registration_mode };

export interface invitation_code_dto {
  id: string;
  code: string;
  created_by_kind: 'staff' | 'player';
  created_at: number;
  used: boolean;
  used_at: number;
  revoked: boolean;
  revoked_at: number;
}

export interface invitation_status_dto {
  mode: registration_mode;
  invite_required: boolean;
  players_can_generate: boolean;
  enabled: boolean;
  can_generate: boolean;
  daily_limit: number;
  lifetime_limit: number;
  generated_today: number;
  generated_total: number;
  daily_remaining: number;
  lifetime_remaining: number;
  codes: invitation_code_dto[];
}

export interface create_invitation_code_response {
  invitation: invitation_code_dto;
  code: string;
}

/**
 * Loads the current player's invitation allowance and issued codes.
 *
 * @returns Promise<invitation_status_dto> - Limits, remaining counts, and codes
 */
export async function getInvitationStatusRequest(): Promise<invitation_status_dto> {
  return authenticatedPost<invitation_status_dto>('getInvitationStatus', {});
}

/**
 * Generates one new single-use invitation code for the current player.
 *
 * @returns Promise<create_invitation_code_response> - New code and its record
 */
export async function createInvitationCodeRequest(): Promise<create_invitation_code_response> {
  return authenticatedPost<create_invitation_code_response>('createInvitationCode', {});
}
