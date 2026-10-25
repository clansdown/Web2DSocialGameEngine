/**
 * Staff-console API. All mutations are server-authoritative and journaled.
 * These calls require a staff record with the matching capability.
 */

import { authenticatedPost } from '../lib/api';
import type {
  support_message_dto,
  support_ticket_dto
} from '../support/support_api';

export interface staff_role_dto {
  id: string;
  name: string;
  description: string;
  capabilities: string[];
}

export interface staff_context_dto {
  is_staff: boolean;
  account_id: number;
  roles: string[];
  capabilities: string[];
  unread_notifications: number;
  support_unread: number;
  chat_moderation_mode: boolean;
  roles_catalog: staff_role_dto[];
  registration_mode?: 'off' | 'global_code' | 'invite_only';
  invitations_enabled?: boolean;
}

export interface staff_character_dto {
  id: number;
  account_id: number;
  display_name: string;
  safe_display_name: string;
  level: number;
  sex: string;
  name_locked: boolean;
  avatar_locked: boolean;
  avatar_changed_at: number;
  has_custom_avatar: boolean;
  avatar_safe_id?: string;
  avatar_custom_id?: string;
  avatar_storage?: string;
}

export interface staff_account_dto {
  account_id: number;
  username: string;
  adult: boolean;
  social_restricted: boolean;
  support_locked: boolean;
  created_at: number;
  characters?: staff_character_dto[];
}

export interface staff_journal_entry_dto {
  id: string;
  operation: string;
  actor_account_id: number;
  actor_character_id: number;
  actor_type: string;
  authority: string;
  capability: string;
  action_type: string;
  action_version: number;
  target_type: string;
  target_id: string;
  scope: string;
  reason: string;
  before_state: Record<string, unknown>;
  after_state: Record<string, unknown>;
  undo_descriptor: Record<string, unknown>;
  reversible: boolean;
  status: string;
  error: string;
  parent_action_id: string;
  undo_of: string;
  created_at: number;
  updated_at: number;
  applied_at: number;
  preflight: string;
}

export interface staff_barony_dto {
  id: number;
  name: string;
  safe_name_id: string;
  safe_tag_ids: string[];
  description: string;
  accepts_minors: boolean;
  owner_character_id: number;
  baron_character_id: number;
  owner_name: string;
  member_count: number;
  defunct_at: number;
}

export interface staff_banned_hash_dto {
  sha256: string;
  reason: string;
  banned_by_account_id: number;
  created_at: number;
  revoked_at: number;
  action_id: string;
}

export async function staffGetContextRequest(): Promise<staff_context_dto> {
  return authenticatedPost<staff_context_dto>('staffGetContext', {});
}

export interface staff_invitation_code_dto {
  id: string;
  code: string;
  created_by_kind: 'staff' | 'player';
  created_by_account_id: number;
  created_by_username: string;
  created_at: number;
  used: boolean;
  used_at: number;
  used_by_account_id: number;
  used_by_username: string;
  revoked: boolean;
  revoked_at: number;
  revoked_by_account_id: number;
}

export async function staffListInvitationCodesRequest(filter: {
  status?: string;
  created_by_kind?: string;
  query?: string;
  limit?: number;
  offset?: number;
}): Promise<{ mode: string; invitations_enabled: boolean; codes: staff_invitation_code_dto[] }> {
  return authenticatedPost('staffListInvitationCodes', filter);
}

export async function staffCreateInvitationCodeRequest(reason: string): Promise<{
  action_id: string;
  code: string;
  invitation: staff_invitation_code_dto;
}> {
  return authenticatedPost('staffCreateInvitationCode', { reason });
}

export async function staffRevokeInvitationCodeRequest(
  code_id: string,
  reason: string
): Promise<{ action_id: string; invitation: staff_invitation_code_dto }> {
  return authenticatedPost('staffRevokeInvitationCode', { code_id, reason });
}

export async function staffListStaffRequest(): Promise<{
  staff: { account_id: number; username: string; roles: string[]; created_at: number; revoked_at: number }[];
}> {
  return authenticatedPost('staffListStaff', {});
}

export async function staffSetChatModerationModeRequest(
  enabled: boolean
): Promise<{ chat_moderation_mode: boolean }> {
  return authenticatedPost('staffSetChatModerationMode', { enabled });
}

export async function staffSearchAccountsRequest(
  query: string
): Promise<{ accounts: staff_account_dto[] }> {
  return authenticatedPost('staffSearchAccounts', { query });
}

export async function staffGetAccountRequest(
  account_id: number
): Promise<staff_account_dto> {
  return authenticatedPost<staff_account_dto>('staffGetAccount', { account_id });
}

export async function staffSetAdultStatusRequest(
  account_id: number,
  adult: boolean,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffSetAdultStatus', { account_id, adult, reason });
}

export async function staffSetSocialRestrictionRequest(
  account_id: number,
  restricted: boolean,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffSetSocialRestriction', { account_id, restricted, reason });
}

export async function staffSetSupportLockRequest(
  account_id: number,
  locked: boolean,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffSetSupportLock', { account_id, locked, reason });
}

export async function staffGetIdentityHistoryRequest(
  character_id: number
): Promise<{ character_id: number; history: { id: number; kind: string; value: string; changed_at: number }[] }> {
  return authenticatedPost('staffGetIdentityHistory', { target_character_id: character_id });
}

export async function staffLockIdentityRequest(
  character_id: number,
  options: {
    lock_name?: boolean;
    lock_avatar?: boolean;
    revert_name?: boolean;
    revert_avatar?: boolean;
    revert_to?: 'safe' | 'prior';
    reason?: string;
  }
): Promise<{ action_id: string }> {
  const body: Record<string, unknown> = {
    character_id,
    revert_name: options.revert_name ?? false,
    revert_avatar: options.revert_avatar ?? false,
    revert_to: options.revert_to ?? 'safe',
    reason: options.reason ?? ''
  };
  if (options.lock_name !== undefined) body.lock_name = options.lock_name;
  if (options.lock_avatar !== undefined) body.lock_avatar = options.lock_avatar;
  return authenticatedPost('staffLockIdentity', body);
}

export async function staffRemoveCustomAvatarRequest(
  character_id: number,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffRemoveCustomAvatar', { character_id, reason });
}

export async function staffBanAvatarHashRequest(
  sha256: string,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffBanAvatarHash', { sha256, reason });
}

export async function staffUnbanAvatarHashRequest(
  sha256: string,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffUnbanAvatarHash', { sha256, reason });
}

export async function staffListAvatarHashBansRequest(
  include_revoked = false
): Promise<{ bans: staff_banned_hash_dto[] }> {
  return authenticatedPost('staffListAvatarHashBans', { include_revoked });
}

export async function staffSearchBaroniesRequest(
  query: string
): Promise<{ baronies: staff_barony_dto[] }> {
  return authenticatedPost('staffSearchBaronies', { query });
}

export async function staffSetBaronyIdentityRequest(
  barony_id: number,
  safe_name_id: string,
  safe_tag_ids: string[],
  name: string,
  description: string,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffSetBaronyIdentity', {
    barony_id,
    safe_name_id,
    safe_tag_ids,
    name,
    description,
    reason
  });
}

export async function staffSetBaronyAcceptsMinorsRequest(
  barony_id: number,
  accepts_minors: boolean,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffSetBaronyAcceptsMinors', {
    barony_id,
    accepts_minors,
    reason
  });
}

export async function staffListJournalRequest(
  options: {
    action_type?: string;
    actor_account_id?: string;
    target_type?: string;
    target_id?: string;
    status?: string;
    operation?: string;
    query?: string;
    limit?: number;
    offset?: number;
  } = {}
): Promise<{ entries: staff_journal_entry_dto[] }> {
  return authenticatedPost('staffListJournal', {
    action_type: options.action_type ?? '',
    actor_account_id: options.actor_account_id ?? '',
    target_type: options.target_type ?? '',
    target_id: options.target_id ?? '',
    status: options.status ?? '',
    operation: options.operation ?? '',
    query: options.query ?? '',
    limit: options.limit ?? 100,
    offset: options.offset ?? 0
  });
}

export async function staffGetJournalEntryRequest(
  action_id: string
): Promise<{
  entry: staff_journal_entry_dto;
  preflight: { status: string; reason: string; resources: unknown[] };
}> {
  return authenticatedPost('staffGetJournalEntry', { action_id });
}

export async function staffPreflightUndoRequest(
  action_id: string
): Promise<{ status: string; reason: string; resources: unknown[] }> {
  return authenticatedPost('staffPreflightUndo', { action_id });
}

export async function staffUndoRequest(
  action_id: string,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffUndo', { action_id, reason });
}

export async function staffListSupportQueueRequest(
  options: { status?: string; category?: string; query?: string; limit?: number; offset?: number } = {}
): Promise<{ tickets: support_ticket_dto[]; unread: number }> {
  return authenticatedPost('staffListSupportQueue', {
    status: options.status ?? '',
    category: options.category ?? '',
    query: options.query ?? '',
    limit: options.limit ?? 100,
    offset: options.offset ?? 0
  });
}

export async function staffGetSupportTicketRequest(
  ticket_id: string
): Promise<{
  ticket: support_ticket_dto;
  messages: support_message_dto[];
  actions: staff_journal_entry_dto[];
}> {
  return authenticatedPost('staffGetSupportTicket', { ticket_id });
}

export async function staffReplySupportTicketRequest(
  ticket_id: string,
  body: string,
  internal: boolean
): Promise<{ message_id: string; ticket: support_ticket_dto }> {
  return authenticatedPost('staffReplySupportTicket', { ticket_id, body, internal });
}

export async function staffCreateSupportTicketRequest(
  account_id: number,
  subject: string,
  body: string,
  category: string,
  character_id = 0
): Promise<{ ticket: support_ticket_dto }> {
  return authenticatedPost('staffCreateSupportTicket', {
    account_id,
    subject,
    body,
    category,
    character_id
  });
}

export async function staffSetSupportTicketStatusRequest(
  ticket_id: string,
  status: string
): Promise<{ ticket: support_ticket_dto }> {
  return authenticatedPost('staffSetSupportTicketStatus', { ticket_id, status });
}

export async function staffAssignSupportTicketRequest(
  ticket_id: string,
  assigned_account_id: number
): Promise<{ ticket: support_ticket_dto }> {
  return authenticatedPost('staffAssignSupportTicket', { ticket_id, assigned_account_id });
}

export async function staffLinkSupportActionRequest(
  ticket_id: string,
  action_id: string
): Promise<{ linked: boolean }> {
  return authenticatedPost('staffLinkSupportAction', { ticket_id, action_id });
}

export async function staffUnstickGameSessionRequest(
  character_id: number,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffUnstickGameSession', { character_id, reason });
}

export async function staffSetGamePhaseRequest(
  character_id: number,
  game_phase: string,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffSetGamePhase', { character_id, game_phase, reason });
}

export async function staffSetMarchProgressRequest(
  character_id: number,
  mini_game: string,
  next_level: number,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffSetMarchProgress', {
    character_id,
    mini_game,
    next_level,
    reason
  });
}

export async function staffCancelConstructionRequest(
  building_id: number,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffCancelConstruction', { building_id, reason });
}

export async function staffAdjustFiefdomResourceRequest(
  fiefdom_id: number,
  resource: string,
  value: number,
  reason: string
): Promise<{ action_id: string }> {
  return authenticatedPost('staffAdjustFiefdomResource', {
    fiefdom_id,
    resource,
    value,
    reason
  });
}

export async function staffSearchChatsRequest(
  query: string
): Promise<{ chats: { id: string; type: string; name: string; owner_character_id: number; direct_key: string; created_at: number }[] }> {
  return authenticatedPost('staffSearchChats', { query });
}
