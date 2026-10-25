/**
 * Chat wire types. Mirrors the JSON produced by the server chat endpoints
 * (see api/getChats.md and the server's `server/chat/ChatService.cpp`).
 */

export type chat_type = 'direct' | 'group' | 'barony' | 'dukedom' | 'wildlands';

export interface chat_character {
  display_name: string;
  portrait_url: string;
}

export interface chat_summary {
  id: string;
  type: chat_type;
  name: string;
  owner_character_id: number;
  direct_key: string;
  members_can_invite: boolean;
  created_at: number;
  name_changed_at: number;
  archived: boolean;
  last_seq: number;
  source_kind: string;
  source_id: number;
}

export interface chat_membership {
  chat_id: string;
  character_id?: number;
  role: string;
  sticky: boolean;
  muted: boolean;
  notify_level: string;
  last_read_seq: number;
  unread_count: number;
  joined_at: number;
  left_at: number;
  change_id: number;
}

export interface chat_reaction {
  reaction_id: string;
  emoji: string;
  character_ids: number[];
}

export interface chat_reply_ref {
  message_id: string;
  author_character_id: number;
  excerpt: string;
  source_language: string;
}

export interface chat_translation {
  body: string;
  engine: string;
  language: string;
  detected_source_language: string;
  confidence: number;
}

export interface chat_message {
  id: string;
  chat_id: string;
  seq: number;
  author_character_id: number;
  author: chat_character | null;
  body: string;
  /** Server-owned safe text id, or null for custom text. */
  safe_message_id: string | null;
  source_language: string;
  source_language_confidence: number;
  gif: { id: string; url: string; preview_url: string; minor_safe: boolean } | null;
  created_at: number;
  edited_at: number;
  deleted: boolean;
  version: number;
  change_id: number;
  system_id: string;
  system_args: Record<string, unknown>;
  reply: chat_reply_ref | null;
  reactions: chat_reaction[];
  translation: chat_translation | null;
}

export interface chat_invite {
  chat_id: string;
  invited_by_character_id: number;
  created_at: number;
  expires_at: number;
  change_id: number;
}

export interface chat_ban {
  chat_id: string;
  banned_by_character_id: number;
  reason: string;
  created_at: number;
  change_id: number;
}

export interface chat_poll {
  cursor: number;
  has_more: boolean;
  chats: chat_summary[];
  memberships: chat_membership[];
  members: chat_membership[];
  messages: chat_message[];
  translations: chat_translation[];
  friend_events: unknown[];
  invites: chat_invite[];
  bans: chat_ban[];
  characters: Record<string, chat_character>;
}

export interface chat_get_result {
  cursor: number;
  chats: chat_summary[];
  memberships: chat_membership[];
  last_messages: Record<string, chat_message>;
  characters: Record<string, chat_character>;
  wildlands_chat_id: string;
  blocked_character_ids: number[];
  muted_character_ids: number[];
}

export interface chat_messages_result {
  chat_id: string;
  messages: chat_message[];
  has_more: boolean;
}

export interface friend_entry {
  character_id: number;
  since?: number;
  requested_at?: number;
}

export interface friends_result {
  friends: friend_entry[];
  incoming_requests: friend_entry[];
  outgoing_requests: friend_entry[];
  blocked_character_ids: number[];
  muted_character_ids: number[];
  characters: Record<string, chat_character>;
}

export interface chat_search_entry {
  character_id: number;
  display_name: string;
  portrait_url: string;
  is_friend: boolean;
  is_blocked: boolean;
}

export interface language_prefs {
  game_language: string;
  disabled_languages: string[];
  baseline_engine: string;
  baseline_available: boolean;
  llm_available: boolean;
}

export interface push_config {
  public_key: string;
  enabled: boolean;
  key_id: string;
}

export interface friend_presence_entry {
  character_id: number;
  online: boolean;
  typing: string[];
}

export interface friend_presence_result {
  friends: friend_presence_entry[];
}
