/**
 * Player-facing support-ticket and notification API.
 *
 * Every call is account-level (the server resolves the authenticated account).
 * A support-locked account can still list/read tickets and notifications, but
 * create/reply calls return `support_locked`.
 */

import { authenticatedPost } from '../lib/api';

export type support_status = 'open' | 'awaiting_staff' | 'awaiting_player' | 'resolved' | 'closed';
export type support_priority = 'normal' | 'high' | 'urgent';
export type support_category = 'appeal' | 'report' | 'technical' | 'account' | 'other';

export interface support_ticket_dto {
  id: string;
  account_id: number;
  character_id: number;
  category: support_category;
  subject: string;
  status: support_status;
  priority: support_priority;
  assigned_account_id: number;
  created_at: number;
  updated_at: number;
  closed_at: number;
  unread_for_player: number;
  unread_for_staff: number;
  message_count: number;
  last_message_preview: string;
  last_message_at: number;
  username?: string;
  assigned_username?: string;
  character_name?: string;
}

export interface support_message_dto {
  id: string;
  ticket_id: string;
  author_kind: 'player' | 'staff' | 'system';
  author_account_id: number;
  author_character_id: number;
  body: string;
  is_internal: boolean;
  created_at: number;
  author_username?: string;
  author_character_name?: string;
}

export interface support_ticket_detail_dto {
  ticket: support_ticket_dto;
  messages: support_message_dto[];
  support_locked: boolean;
}

export interface support_context_dto {
  support_locked: boolean;
  unread_tickets: number;
  unread_notifications: number;
}

export interface notification_dto {
  id: string;
  account_id: number;
  character_id: number;
  kind: string;
  text_id: string;
  text_args: Record<string, unknown>;
  ticket_id: string;
  action_id: string;
  route: string;
  created_at: number;
  read_at: number;
  read: boolean;
}

export async function getSupportContextRequest(): Promise<support_context_dto> {
  return authenticatedPost<support_context_dto>('getSupportContext', {});
}

export async function listSupportTicketsRequest(
  options: { status?: string; category?: string; query?: string; limit?: number; offset?: number } = {}
): Promise<{ tickets: support_ticket_dto[]; support_locked: boolean; unread_notifications: number }> {
  return authenticatedPost('listSupportTickets', {
    status: options.status ?? '',
    category: options.category ?? '',
    query: options.query ?? '',
    limit: options.limit ?? 100,
    offset: options.offset ?? 0
  });
}

export async function getSupportTicketRequest(ticket_id: string): Promise<support_ticket_detail_dto> {
  return authenticatedPost<support_ticket_detail_dto>('getSupportTicket', { ticket_id });
}

export async function createSupportTicketRequest(
  subject: string,
  body: string,
  category: support_category,
  character_id?: number
): Promise<{ ticket: support_ticket_dto }> {
  return authenticatedPost('createSupportTicket', {
    subject,
    body,
    category,
    character_id: character_id ?? 0
  });
}

export async function replySupportTicketRequest(
  ticket_id: string,
  body: string
): Promise<{ message_id: string; ticket: support_ticket_dto }> {
  return authenticatedPost('replySupportTicket', { ticket_id, body });
}

export async function reopenSupportTicketRequest(
  ticket_id: string
): Promise<{ ticket: support_ticket_dto }> {
  return authenticatedPost('reopenSupportTicket', { ticket_id });
}

export async function getNotificationsRequest(
  limit = 50,
  offset = 0
): Promise<{ notifications: notification_dto[]; unread: number; support_locked: boolean }> {
  return authenticatedPost('getNotifications', { limit, offset });
}

export async function markNotificationReadRequest(
  notification_id: string
): Promise<{ unread: number }> {
  return authenticatedPost('markNotificationRead', { notification_id });
}

export async function markAllNotificationsReadRequest(): Promise<{ unread: number }> {
  return authenticatedPost('markAllNotificationsRead', {});
}
