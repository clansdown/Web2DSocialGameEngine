/**
 * Authenticated wrappers around the server chat endpoints. Every call carries
 * `character_id`; authentication (username + session token) is injected by
 * `authenticatedPost`.
 */
import { authenticatedPost } from '../lib/api';
import type {
  chat_get_result,
  chat_messages_result,
  chat_poll,
  friends_result,
  chat_search_entry,
  language_prefs,
  push_config,
  friend_presence_result
} from './chat_protocol';

export function getChatsRequest(character_id: number): Promise<chat_get_result> {
  return authenticatedPost<chat_get_result>('getChats', { character_id });
}

export function getChatMessagesRequest(
  character_id: number,
  chat_id: string,
  options: { before_seq?: number; after_seq?: number; limit?: number; lang?: string } = {}
): Promise<chat_messages_result> {
  return authenticatedPost<chat_messages_result>('getChatMessages', {
    character_id,
    chat_id,
    ...options
  });
}

export function pollChatsRequest(
  character_id: number,
  cursor: number,
  lang: string
): Promise<chat_poll> {
  return authenticatedPost<chat_poll>('pollChats', { character_id, cursor, lang });
}

export function pollWildlandsRequest(
  character_id: number,
  cursor: number,
  lang: string
): Promise<chat_poll> {
  return authenticatedPost<chat_poll>('pollWildlands', { character_id, cursor, lang });
}

export function sendChatMessageRequest(
  character_id: number,
  chat_id: string,
  body: string,
  options: {
    safe_message_id?: string;
    gif_id?: string;
    reply_to_message_id?: string;
    language?: string;
    client_id?: string;
  } = {}
): Promise<{ id: string; seq: number; change_id: number }> {
  return authenticatedPost('sendChatMessage', { character_id, chat_id, body, ...options });
}

export function editChatMessageRequest(
  character_id: number,
  message_id: string,
  body: string,
  options: { safe_message_id?: string; gif_id?: string } = {}
): Promise<{ id: string; version: number; change_id: number }> {
  return authenticatedPost('editChatMessage', { character_id, message_id, body, ...options });
}

export function deleteChatMessageRequest(
  character_id: number,
  message_id: string
): Promise<{ id: string; change_id: number }> {
  return authenticatedPost('deleteChatMessage', { character_id, message_id });
}

export function markChatReadRequest(
  character_id: number,
  chat_id: string,
  seq: number
): Promise<{ chat_id: string; last_read_seq: number; unread_count: number }> {
  return authenticatedPost('markChatRead', { character_id, chat_id, seq });
}

export function setChatSettingsRequest(
  character_id: number,
  chat_id: string,
  settings: { sticky?: boolean; muted?: boolean; notify_level?: string }
): Promise<{ chat_id: string }> {
  return authenticatedPost('setChatSettings', { character_id, chat_id, ...settings });
}

export function openDirectChatRequest(
  character_id: number,
  target_character_id: number
): Promise<{ chat_id: string }> {
  return authenticatedPost('openDirectChat', { character_id, target_character_id });
}

export function searchChatCharactersRequest(
  character_id: number,
  query: string
): Promise<{ characters: chat_search_entry[] }> {
  return authenticatedPost('searchChatCharacters', { character_id, query });
}

export function addChatReactionRequest(
  character_id: number,
  message_id: string,
  reaction_id: string
): Promise<{ message_id: string; reactions: unknown[]; change_id: number }> {
  return authenticatedPost('addChatReaction', { character_id, message_id, reaction_id });
}

export function removeChatReactionRequest(
  character_id: number,
  message_id: string,
  reaction_id: string
): Promise<{ message_id: string; reactions: unknown[]; change_id: number }> {
  return authenticatedPost('removeChatReaction', { character_id, message_id, reaction_id });
}

export function addChatBookmarkRequest(
  character_id: number,
  message_id: string
): Promise<{ message_id: string; bookmarked: boolean }> {
  return authenticatedPost('addChatBookmark', { character_id, message_id });
}

export function removeChatBookmarkRequest(
  character_id: number,
  message_id: string
): Promise<{ message_id: string; bookmarked: boolean }> {
  return authenticatedPost('removeChatBookmark', { character_id, message_id });
}

export function getFriendsRequest(character_id: number): Promise<friends_result> {
  return authenticatedPost<friends_result>('getFriends', { character_id });
}

export function sendFriendRequestRequest(
  character_id: number,
  target_character_id: number
): Promise<unknown> {
  return authenticatedPost('sendFriendRequest', { character_id, target_character_id });
}

export function acceptFriendRequestRequest(
  character_id: number,
  target_character_id: number
): Promise<unknown> {
  return authenticatedPost('acceptFriendRequest', { character_id, target_character_id });
}

export function declineFriendRequestRequest(
  character_id: number,
  target_character_id: number
): Promise<unknown> {
  return authenticatedPost('declineFriendRequest', { character_id, target_character_id });
}

export function unfriendRequest(character_id: number, target_character_id: number): Promise<unknown> {
  return authenticatedPost('unfriend', { character_id, target_character_id });
}

export function blockUserRequest(character_id: number, target_character_id: number): Promise<unknown> {
  return authenticatedPost('blockUser', { character_id, target_character_id });
}

export function unblockUserRequest(
  character_id: number,
  target_character_id: number
): Promise<unknown> {
  return authenticatedPost('unblockUser', { character_id, target_character_id });
}

export function muteCharacterRequest(
  character_id: number,
  target_character_id: number
): Promise<unknown> {
  return authenticatedPost('muteCharacter', { character_id, target_character_id });
}

export function unmuteCharacterRequest(
  character_id: number,
  target_character_id: number
): Promise<unknown> {
  return authenticatedPost('unmuteCharacter', { character_id, target_character_id });
}

export function createGroupChatRequest(
  character_id: number,
  name: string
): Promise<{ chat_id: string; name: string }> {
  return authenticatedPost('createGroupChat', { character_id, name });
}

export function inviteToGroupRequest(
  character_id: number,
  chat_id: string,
  target_character_id: number
): Promise<unknown> {
  return authenticatedPost('inviteToGroup', { character_id, chat_id, target_character_id });
}

export function acceptGroupInviteRequest(character_id: number, chat_id: string): Promise<unknown> {
  return authenticatedPost('acceptGroupInvite', { character_id, chat_id });
}

export function declineGroupInviteRequest(character_id: number, chat_id: string): Promise<unknown> {
  return authenticatedPost('declineGroupInvite', { character_id, chat_id });
}

export function leaveChatRequest(character_id: number, chat_id: string): Promise<unknown> {
  return authenticatedPost('leaveChat', { character_id, chat_id });
}

export function transferGroupOwnershipRequest(
  character_id: number,
  chat_id: string,
  target_character_id: number
): Promise<unknown> {
  return authenticatedPost('transferGroupOwnership', { character_id, chat_id, target_character_id });
}

export function setGroupModeratorRequest(
  character_id: number,
  chat_id: string,
  target_character_id: number,
  moderator: boolean
): Promise<unknown> {
  return authenticatedPost('setGroupModerator', {
    character_id,
    chat_id,
    target_character_id,
    moderator
  });
}

export function kickMemberRequest(
  character_id: number,
  chat_id: string,
  target_character_id: number
): Promise<unknown> {
  return authenticatedPost('kickMember', { character_id, chat_id, target_character_id });
}

export function renameGroupRequest(
  character_id: number,
  chat_id: string,
  name: string
): Promise<unknown> {
  return authenticatedPost('renameGroup', { character_id, chat_id, name });
}

export function setGroupInvitePolicyRequest(
  character_id: number,
  chat_id: string,
  members_can_invite: boolean
): Promise<unknown> {
  return authenticatedPost('setGroupInvitePolicy', { character_id, chat_id, members_can_invite });
}

export function banFromChatRequest(
  character_id: number,
  chat_id: string,
  target_character_id: number,
  reason: string
): Promise<unknown> {
  return authenticatedPost('banFromChat', { character_id, chat_id, target_character_id, reason });
}

export function requestTranslationsRequest(
  character_id: number,
  message_ids: string[],
  lang: string
): Promise<{ translations: Record<string, unknown> }> {
  return authenticatedPost('requestTranslations', { character_id, message_ids, lang });
}

export function getChatLanguagePrefsRequest(character_id: number): Promise<language_prefs> {
  return authenticatedPost<language_prefs>('getChatLanguagePrefs', { character_id });
}

export function setChatLanguagePrefRequest(
  character_id: number,
  language: string,
  enabled: boolean
): Promise<unknown> {
  return authenticatedPost('setChatLanguagePref', { character_id, language, enabled });
}

export function getFriendPresenceRequest(character_id: number): Promise<friend_presence_result> {
  return authenticatedPost<friend_presence_result>('getFriendPresence', { character_id });
}

export function chatHeartbeatRequest(character_id: number): Promise<unknown> {
  return authenticatedPost('chatHeartbeat', { character_id });
}

export function setChatTypingRequest(character_id: number, chat_id: string): Promise<unknown> {
  return authenticatedPost('setChatTyping', { character_id, chat_id });
}

export function getPushConfigRequest(character_id: number): Promise<push_config> {
  return authenticatedPost<push_config>('getPushConfig', { character_id });
}

export function subscribePushRequest(
  character_id: number,
  subscription: { endpoint: string; p256dh: string; auth: string }
): Promise<unknown> {
  return authenticatedPost('subscribePush', { character_id, ...subscription });
}

export function setPushPreferencesRequest(
  character_id: number,
  prefs: { push_enabled?: boolean; dm_from_friends?: boolean; group_mentions?: boolean }
): Promise<unknown> {
  return authenticatedPost('setPushPreferences', { character_id, ...prefs });
}
