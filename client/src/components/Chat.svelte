<script lang="ts">
  /**
   * Chat screen — full-screen responsive chat over the hash router
   * (#/activity/chat[/<chat_id>]).
   *
   * Loads the chat list + social graph once, then polls aggregate deltas
   * (`pollChats`), with a separate `pollWildlands` while the wildlands view is
   * open. All user-facing text comes from the text system.
   */
  import { onMount, onDestroy } from 'svelte';
  import { currentCharacter, language } from '../lib/stores';
  import { loadTexts } from '../lib/text';
  import { render_markdown } from '../chat/markdown';
  import { with_avatar_auth } from '../lib/avatars';
  import * as api from '../chat/chat_api';
  import type {
    chat_summary,
    chat_membership,
    chat_message,
    chat_character,
    chat_invite,
    chat_ban,
    chat_search_entry,
    friends_result,
    language_prefs,
    friend_presence_entry
  } from '../chat/chat_protocol';
  import { register_push } from '../chat/push';
  import { navigate } from '../lib/router';

  interface Props {
    onBack: () => void;
    /** Nested route segments: ['<chat_id>'] for #/activity/chat/<chat_id>. */
    rest?: string[];
  }
  let { onBack, rest = [] }: Props = $props();

  const TEXT_IDS: string[] = [
    'ui_chat_back', 'ui_chat_title', 'ui_chat_new_dm', 'ui_chat_search_placeholder',
    'ui_chat_no_chats', 'ui_chat_wildlands', 'ui_chat_send', 'ui_chat_reply', 'ui_chat_edit',
    'ui_chat_delete', 'ui_chat_bookmark', 'ui_chat_react', 'ui_chat_edited', 'ui_chat_deleted',
    'ui_chat_show_translation', 'ui_chat_hide_translation', 'ui_chat_typing', 'ui_chat_online',
    'ui_chat_offline', 'ui_chat_create_group', 'ui_chat_group_name', 'ui_chat_invite',
    'ui_chat_leave', 'ui_chat_members', 'ui_chat_friends', 'ui_chat_accept', 'ui_chat_decline',
    'ui_chat_block', 'ui_chat_unblock', 'ui_chat_mute', 'ui_chat_unmute', 'ui_chat_sticky',
    'ui_chat_settings', 'ui_chat_language_prefs', 'ui_chat_push_enable',
    'ui_chat_push_group_mentions', 'ui_chat_loading', 'ui_chat_error', 'ui_chat_chars_left',
    'ui_chat_too_long', 'ui_chat_too_many_lines', 'ui_chat_send_failed', 'ui_chat_no_messages',
    'ui_chat_cancel', 'ui_chat_markdown_hint'
  ];

  let texts = $state<Record<string, string>>({});
  let loading = $state(true);
  let fatal = $state('');
  let actionError = $state('');

  let chats = $state<chat_summary[]>([]);
  let memberships = $state<Record<string, chat_membership>>({});
  let membersByChat = $state<Record<string, chat_membership[]>>({});
  let characters = $state<Record<string, chat_character>>({});
  let messages = $state<Record<string, chat_message[]>>({});
  let lastMessages = $state<Record<string, chat_message>>({});
  let invites = $state<chat_invite[]>([]);
  let bans = $state<chat_ban[]>([]);
  let friends = $state<friends_result | null>(null);
  let presence = $state<Record<number, friend_presence_entry>>({});
  let langPrefs = $state<language_prefs | null>(null);
  let limits = $state<Record<string, { max_characters: number; max_lines: number }>>({});
  let wildlandsId = $state('');

  // The open chat is derived from the route (#/activity/chat/<chat_id>) so the
  // browser Back button / mobile swipe returns to the list. `ready` gates the
  // open side effects until the chat list has loaded.
  let active_chat_id = $derived(rest.length > 0 ? rest[0] : '');
  let ready = $state(false);
  let last_opened_id = '';
  let cursor = $state(0);
  let wildlandsCursor = $state(0);
  let composerText = $state('');
  let replyTo = $state<chat_message | null>(null);
  let editing = $state<chat_message | null>(null);
  let searchQuery = $state('');
  let searchResults = $state<chat_search_entry[]>([]);
  let showSettings = $state(false);
  let showFriends = $state(false);
  let showMembers = $state(false);
  let groupName = $state('');
  let expandedTranslations = $state<string[]>([]);
  let blockedIds = $state<number[]>([]);
  let mutedIds = $state<number[]>([]);

  let pollTimer: ReturnType<typeof setInterval> | null = null;
  let heartbeatTimer: ReturnType<typeof setInterval> | null = null;
  let typingCooldown = 0;

  let character_id = $derived($currentCharacter?.id ?? 0);
  let activeChat = $derived(chats.find((c) => c.id === active_chat_id) ?? null);
  let activeMembership = $derived(active_chat_id ? memberships[active_chat_id] : undefined);
  let activeMessages = $derived(active_chat_id ? (messages[active_chat_id] ?? []) : []);

  function t(id: string, tokens?: Record<string, string>): string {
    let value = texts[id] ?? '';
    if (tokens) {
      for (const [key, replacement] of Object.entries(tokens)) {
        value = value.replace(new RegExp(`\\{${key}\\}`, 'g'), replacement);
      }
    }
    return value;
  }

  /** Display name for a chat (direct chats show the other participant). */
  function chat_name(chat: chat_summary): string {
    if (chat.type === 'wildlands') return t('ui_chat_wildlands');
    if (chat.type === 'direct') {
      const other = other_member_id(chat);
      if (other && characters[String(other)]) return characters[String(other)].display_name;
      return '—';
    }
    return chat.name || '—';
  }

  function other_member_id(chat: chat_summary): number {
    const parts = (chat.direct_key || '').split(':');
    if (parts.length !== 2) return 0;
    const a = parseInt(parts[0], 10);
    const b = parseInt(parts[1], 10);
    return a === character_id ? b : a;
  }

  function avatar_url(id: number): string {
    // The server resolves the safe/custom layering per viewer; append session
    // credentials when the result is the age-gated custom route.
    return with_avatar_auth(characters[String(id)]?.portrait_url ?? '');
  }

  function format_time(ts: number): string {
    if (!ts) return '';
    return new Date(ts * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  }

  function limit_for(chat: chat_summary): { max_characters: number; max_lines: number } {
    if (chat.type === 'direct') {
      const other = other_member_id(chat);
      const is_friend = (friends?.friends ?? []).some((f) => f.character_id === other);
      return limits[is_friend ? 'direct_friend' : 'direct'] ?? { max_characters: 512, max_lines: 20 };
    }
    if (chat.type === 'wildlands') return limits.wildlands ?? { max_characters: 512, max_lines: 10 };
    if (chat.type === 'barony') return limits.barony ?? { max_characters: 1024, max_lines: 20 };
    if (chat.type === 'dukedom') return limits.dukedom ?? { max_characters: 1024, max_lines: 20 };
    return limits.group ?? { max_characters: 1024, max_lines: 20 };
  }

  function char_count(value: string): number {
    return Array.from(value).length;
  }

  function line_count(value: string): number {
    return value.split('\n').length;
  }

  function merge_character_map(incoming: Record<string, chat_character>): void {
    characters = { ...characters, ...incoming };
  }

  function upsert_message(message: chat_message): void {
    const list = messages[message.chat_id] ? [...messages[message.chat_id]] : [];
    const index = list.findIndex((m) => m.id === message.id);
    if (index >= 0) list[index] = message;
    else list.push(message);
    list.sort((a, b) => a.seq - b.seq);
    messages = { ...messages, [message.chat_id]: list };
    lastMessages = { ...lastMessages, [message.chat_id]: list[list.length - 1] };
  }

  function upsert_chat(chat: chat_summary): void {
    const index = chats.findIndex((c) => c.id === chat.id);
    if (index >= 0) {
      const next = [...chats];
      next[index] = chat;
      chats = next;
    } else {
      chats = [...chats, chat];
    }
  }

  function upsert_membership(membership: chat_membership, mine: boolean): void {
    if (mine) {
      memberships = { ...memberships, [membership.chat_id]: membership };
      return;
    }
    const list = membersByChat[membership.chat_id] ? [...membersByChat[membership.chat_id]] : [];
    const index = list.findIndex((m) => m.character_id === membership.character_id);
    if (index >= 0) list[index] = membership;
    else list.push(membership);
    membersByChat = { ...membersByChat, [membership.chat_id]: list };
  }

  function apply_poll(result: {
    chats: chat_summary[];
    memberships: chat_membership[];
    members: chat_membership[];
    messages: chat_message[];
    invites: chat_invite[];
    bans: chat_ban[];
    characters: Record<string, chat_character>;
  }): void {
    for (const chat of result.chats) upsert_chat(chat);
    for (const membership of result.memberships) upsert_membership(membership, true);
    for (const membership of result.members) upsert_membership(membership, false);
    for (const message of result.messages) upsert_message(message);
    merge_character_map(result.characters || {});
    for (const invite of result.invites) {
      if (!invites.some((i) => i.chat_id === invite.chat_id)) invites = [...invites, invite];
    }
    for (const ban of result.bans) {
      if (!bans.some((b) => b.chat_id === ban.chat_id)) bans = [...bans, ban];
    }
  }

  async function load_all(): Promise<void> {
    const cid = character_id;
    if (!cid) return;
    try {
      const [list, social, prefs] = await Promise.all([
        api.getChatsRequest(cid),
        api.getFriendsRequest(cid),
        api.getChatLanguagePrefsRequest(cid)
      ]);
      chats = list.chats;
      memberships = Object.fromEntries(list.memberships.map((m) => [m.chat_id, m]));
      lastMessages = list.last_messages || {};
      merge_character_map(list.characters || {});
      wildlandsId = list.wildlands_chat_id;
      cursor = list.cursor;
      blockedIds = list.blocked_character_ids || [];
      mutedIds = list.muted_character_ids || [];
      friends = social;
      merge_character_map(social.characters || {});
      langPrefs = prefs;
      limits = (list as unknown as { limits?: typeof limits }).limits ?? {};
      loading = false;
    } catch (e) {
      fatal = e instanceof Error ? e.message : String(e);
      loading = false;
    }
  }

  async function load_messages(chat_id: string): Promise<void> {
    const cid = character_id;
    if (!cid) return;
    const result = await api.getChatMessagesRequest(cid, chat_id, {
      limit: 50,
      lang: $language
    });
    messages = { ...messages, [chat_id]: result.messages };
    const directory = { ...characters };
    for (const message of result.messages) {
      if (message.author) directory[String(message.author_character_id)] = message.author;
    }
    characters = directory;
    await ensure_translations(chat_id);
  }

  async function ensure_translations(chat_id: string): Promise<void> {
    const cid = character_id;
    const list = messages[chat_id] ?? [];
    const needed = list
      .filter(
        (m) =>
          !m.deleted &&
          m.body &&
          m.source_language &&
          m.source_language !== $language &&
          !(langPrefs?.disabled_languages ?? []).includes(m.source_language) &&
          !m.translation
      )
      .map((m) => m.id);
    if (needed.length === 0) return;
    try {
      const response = await api.requestTranslationsRequest(cid, needed, $language);
      const map = (response.translations ?? {}) as Record<string, chat_message['translation']>;
      const next = (messages[chat_id] ?? []).map((m) =>
        map[m.id] ? { ...m, translation: map[m.id] } : m
      );
      messages = { ...messages, [chat_id]: next };
    } catch {
      /* translation is best-effort */
    }
  }

  /**
   * Opens a chat by navigating to its route. The route is the single source of
   * truth for which chat is open; the open side effects run in the $effect below.
   *
   * @param chat_id Chat to open.
   */
  function open_chat(chat_id: string): void {
    navigate({ type: 'activity', id: 'chat', rest: [chat_id] });
  }

  /**
   * Side effects for a newly-opened chat: reset transient UI, load history, and
   * advance the read cursor.
   *
   * @param chat_id The chat that just became active.
   */
  async function open_chat_side_effects(chat_id: string): Promise<void> {
    replyTo = null;
    editing = null;
    showMembers = false;
    try {
      await load_messages(chat_id);
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
    const membership = memberships[chat_id];
    const last = lastMessages[chat_id];
    if (membership && last && last.seq > membership.last_read_seq) {
      await mark_read(chat_id, last.seq);
    } else if (membership && (membership.unread_count ?? 0) > 0) {
      await mark_read(chat_id, last?.seq ?? membership.last_read_seq);
    }
  }

  // React to route changes: load the newly-opened chat. Guarded by
  // `last_opened_id` so polling/re-renders do not re-run the side effects.
  $effect(() => {
    if (!ready) return;
    const chat_id = active_chat_id;
    if (chat_id === last_opened_id) return;
    last_opened_id = chat_id;
    if (chat_id) void open_chat_side_effects(chat_id);
  });

  async function mark_read(chat_id: string, seq: number): Promise<void> {
    try {
      await api.markChatReadRequest(character_id, chat_id, seq);
      memberships = {
        ...memberships,
        [chat_id]: { ...(memberships[chat_id] ?? ({} as chat_membership)), unread_count: 0, last_read_seq: seq }
      };
    } catch {
      /* ignore */
    }
  }

  async function send(): Promise<void> {
    const cid = character_id;
    if (!cid || !active_chat_id) return;
    if (!composerText.trim()) return;
    actionError = '';
    try {
      if (editing) {
        await api.editChatMessageRequest(cid, editing.id, composerText);
        editing = null;
      } else {
        await api.sendChatMessageRequest(cid, active_chat_id, composerText, {
          reply_to_message_id: replyTo?.id,
          language: $language
        });
        replyTo = null;
      }
      composerText = '';
      await poll_once();
      await load_messages(active_chat_id);
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      actionError = message === 'message_too_long'
        ? t('ui_chat_too_long')
        : message === 'too_many_lines'
          ? t('ui_chat_too_many_lines')
          : message === 'rate_limited'
            ? 'Rate limited — slow down'
            : t('ui_chat_send_failed');
    }
  }

  async function remove_message(message: chat_message): Promise<void> {
    try {
      await api.deleteChatMessageRequest(character_id, message.id);
      await poll_once();
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function toggle_reaction(message: chat_message, emoji: string): Promise<void> {
    const mine = message.reactions?.some(
      (r) => r.emoji === emoji && r.character_ids.includes(character_id)
    );
    try {
      if (mine) await api.removeChatReactionRequest(character_id, message.id, emoji);
      else await api.addChatReactionRequest(character_id, message.id, emoji);
      await poll_once();
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function toggle_bookmark(message: chat_message): Promise<void> {
    try {
      await api.addChatBookmarkRequest(character_id, message.id);
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  function toggle_translation(message: chat_message): void {
    if (expandedTranslations.includes(message.id)) {
      expandedTranslations = expandedTranslations.filter((id) => id !== message.id);
    } else {
      expandedTranslations = [...expandedTranslations, message.id];
    }
  }

  function start_reply(message: chat_message): void {
    replyTo = message;
    editing = null;
  }

  function start_edit(message: chat_message): void {
    editing = message;
    replyTo = null;
    composerText = message.body;
  }

  function on_composer_input(): void {
    const now = Date.now();
    if (active_chat_id && now - typingCooldown > 4000) {
      typingCooldown = now;
      api.setChatTypingRequest(character_id, active_chat_id).catch(() => {});
    }
  }

  async function open_dm(target_character_id: number): Promise<void> {
    try {
      const result = await api.openDirectChatRequest(character_id, target_character_id);
      searchResults = [];
      searchQuery = '';
      await load_all();
      open_chat(result.chat_id);
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function run_search(): Promise<void> {
    if (!searchQuery.trim()) {
      searchResults = [];
      return;
    }
    try {
      const result = await api.searchChatCharactersRequest(character_id, searchQuery);
      searchResults = result.characters;
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function create_group(): Promise<void> {
    if (!groupName.trim()) return;
    try {
      const result = await api.createGroupChatRequest(character_id, groupName.trim());
      groupName = '';
      await load_all();
      open_chat(result.chat_id);
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function accept_invite(chat_id: string): Promise<void> {
    try {
      await api.acceptGroupInviteRequest(character_id, chat_id);
      invites = invites.filter((i) => i.chat_id !== chat_id);
      await load_all();
      open_chat(chat_id);
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function decline_invite(chat_id: string): Promise<void> {
    try {
      await api.declineGroupInviteRequest(character_id, chat_id);
      invites = invites.filter((i) => i.chat_id !== chat_id);
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function invite_friend(target_character_id: number): Promise<void> {
    if (!active_chat_id) return;
    try {
      await api.inviteToGroupRequest(character_id, active_chat_id, target_character_id);
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function leave_active(): Promise<void> {
    if (!active_chat_id) return;
    try {
      await api.leaveChatRequest(character_id, active_chat_id);
      await load_all();
      navigate({ type: 'activity', id: 'chat', rest: [] });
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function toggle_sticky(): Promise<void> {
    if (!activeMembership || !active_chat_id) return;
    try {
      await api.setChatSettingsRequest(character_id, active_chat_id, {
        sticky: !activeMembership.sticky
      });
      await poll_once();
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function toggle_mute(): Promise<void> {
    if (!activeMembership || !active_chat_id) return;
    try {
      await api.setChatSettingsRequest(character_id, active_chat_id, {
        muted: !activeMembership.muted
      });
      await poll_once();
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function accept_friend(target_character_id: number): Promise<void> {
    try {
      await api.acceptFriendRequestRequest(character_id, target_character_id);
      await load_all();
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function decline_friend(target_character_id: number): Promise<void> {
    try {
      await api.declineFriendRequestRequest(character_id, target_character_id);
      await load_all();
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function block(target_character_id: number): Promise<void> {
    try {
      await api.blockUserRequest(character_id, target_character_id);
      await load_all();
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function unblock(target_character_id: number): Promise<void> {
    try {
      await api.unblockUserRequest(character_id, target_character_id);
      await load_all();
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function toggle_language(lang: string): Promise<void> {
    const disabled = langPrefs?.disabled_languages ?? [];
    const is_disabled = disabled.includes(lang);
    try {
      await api.setChatLanguagePrefRequest(character_id, lang, is_disabled);
      langPrefs = {
        ...(langPrefs as language_prefs),
        disabled_languages: is_disabled ? disabled.filter((l) => l !== lang) : [...disabled, lang]
      };
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function enable_push(): Promise<void> {
    try {
      const ok = await register_push(character_id);
      actionError = ok ? '' : 'Push notifications are not available';
    } catch (e) {
      actionError = e instanceof Error ? e.message : String(e);
    }
  }

  async function poll_once(): Promise<void> {
    if (!character_id) return;
    try {
      const result = await api.pollChatsRequest(character_id, cursor, $language);
      apply_poll(result);
      cursor = result.cursor;
      if (active_chat_id && active_chat_id === wildlandsId) {
        const wild = await api.pollWildlandsRequest(character_id, wildlandsCursor, $language);
        apply_poll(wild);
        wildlandsCursor = wild.cursor;
      }
      if (active_chat_id) await ensure_translations(active_chat_id);
    } catch {
      /* transient poll failures are ignored */
    }
  }

  async function poll_presence(): Promise<void> {
    if (!character_id) return;
    try {
      const result = await api.getFriendPresenceRequest(character_id);
      const map: Record<number, friend_presence_entry> = {};
      for (const entry of result.friends) map[entry.character_id] = entry;
      presence = map;
    } catch {
      /* ignore */
    }
  }

  function is_typing(): boolean {
    const other = activeChat && activeChat.type === 'direct' ? other_member_id(activeChat) : 0;
    if (!other) return false;
    const entry = presence[other];
    return !!entry && entry.typing.includes(active_chat_id);
  }

  function can_manage_group(): boolean {
    const role = activeMembership?.role;
    return activeChat?.type === 'group' && (role === 'owner' || role === 'moderator');
  }

  function is_owner(): boolean {
    return activeMembership?.role === 'owner';
  }

  const REACTIONS = ['👍', '❤️', '😂', '🎉', '😮', '😢'];

  onMount(async () => {
    texts = await loadTexts(TEXT_IDS);
    await load_all();
    // Opening the route's chat is handled by the $effect above, once the list
    // has loaded.
    ready = true;
    pollTimer = setInterval(poll_once, 3500);
    heartbeatTimer = setInterval(() => {
      api.chatHeartbeatRequest(character_id).catch(() => {});
      poll_presence();
    }, 30000);
    api.chatHeartbeatRequest(character_id).catch(() => {});
  });

  onDestroy(() => {
    if (pollTimer) clearInterval(pollTimer);
    if (heartbeatTimer) clearInterval(heartbeatTimer);
  });
</script>

<div class="chat-shell">
  <div class="chat-header d-flex align-items-center gap-2 px-3 py-2 border-bottom">
    <button class="btn btn-sm btn-outline-secondary" onclick={onBack}>
      &larr; {t('ui_chat_back')}
    </button>
    <strong class="flex-grow-1">{t('ui_chat_title')}</strong>
    <button class="btn btn-sm btn-outline-secondary" onclick={() => (showFriends = !showFriends)}>
      {t('ui_chat_friends')}
    </button>
    <button class="btn btn-sm btn-outline-secondary" onclick={() => (showSettings = !showSettings)}>
      {t('ui_chat_settings')}
    </button>
  </div>

  {#if fatal}
    <div class="alert alert-danger m-3">{t('ui_chat_error')}: {fatal}</div>
  {:else if loading}
    <div class="d-flex justify-content-center p-5">
      <div class="spinner-border text-light" role="status"></div>
      <span class="ms-3">{t('ui_chat_loading')}</span>
    </div>
  {:else}
    <div class="chat-body">
      <!-- Chat list -->
      <aside class="chat-list border-end" class:mobile-hidden={active_chat_id !== ''}>
        <div class="p-2">
          <input
            class="form-control form-control-sm mb-2"
            placeholder={t('ui_chat_search_placeholder')}
            bind:value={searchQuery}
            oninput={run_search}
          />
          {#if searchResults.length > 0}
            <div class="list-group mb-2">
              {#each searchResults as entry (entry.character_id)}
                <button
                  class="list-group-item list-group-item-action py-1"
                  onclick={() => open_dm(entry.character_id)}
                >
                  {entry.display_name}
                </button>
              {/each}
            </div>
          {/if}
          <div class="input-group input-group-sm mb-2">
            <input
              class="form-control"
              placeholder={t('ui_chat_group_name')}
              bind:value={groupName}
            />
            <button class="btn btn-outline-secondary" onclick={create_group}>
              {t('ui_chat_create_group')}
            </button>
          </div>
        </div>

        {#if wildlandsId}
          <button
            class="chat-item w-100 text-start"
            class:active={active_chat_id === wildlandsId}
            onclick={() => open_chat(wildlandsId)}
          >
            <span class="badge text-bg-secondary me-2">#</span>{t('ui_chat_wildlands')}
          </button>
        {/if}

        {#each chats as chat (chat.id)}
          {@const membership = memberships[chat.id]}
          <button
            class="chat-item w-100 text-start"
            class:active={active_chat_id === chat.id}
            onclick={() => open_chat(chat.id)}
          >
            <div class="d-flex align-items-center gap-2">
              {#if chat.type === 'direct'}
                {@const other_avatar_url = avatar_url(other_member_id(chat))}
                {#if other_avatar_url}
                  <img class="chat-avatar" src={other_avatar_url} alt="" />
                {/if}
              {/if}
              <span class="flex-grow-1 text-truncate">{chat_name(chat)}</span>
              {#if membership?.muted}
                <span title={t('ui_chat_mute')}>🔇</span>
              {/if}
              {#if (membership?.unread_count ?? 0) > 0}
                <span class="badge text-bg-primary">{membership?.unread_count}</span>
              {/if}
            </div>
            {#if lastMessages[chat.id]}
              <small class="text-secondary d-block text-truncate">
                {lastMessages[chat.id].deleted ? t('ui_chat_deleted') : lastMessages[chat.id].body}
              </small>
            {/if}
          </button>
        {/each}

        {#if chats.length === 0}
          <div class="text-secondary small p-3">{t('ui_chat_no_chats')}</div>
        {/if}

        {#if invites.length > 0}
          <div class="p-2 border-top">
            {#each invites as invite (invite.chat_id)}
              <div class="small d-flex align-items-center gap-1 mb-1">
                <span class="text-truncate flex-grow-1">
                  {characters[String(invite.invited_by_character_id)]?.display_name ?? ''}
                </span>
                <button class="btn btn-sm btn-primary py-0" onclick={() => accept_invite(invite.chat_id)}>
                  {t('ui_chat_accept')}
                </button>
                <button class="btn btn-sm btn-outline-secondary py-0" onclick={() => decline_invite(invite.chat_id)}>
                  {t('ui_chat_decline')}
                </button>
              </div>
            {/each}
          </div>
        {/if}
      </aside>

      <!-- Thread -->
      <section class="chat-thread" class:mobile-hidden={active_chat_id === ''}>
        {#if activeChat}
          {@const limit = limit_for(activeChat)}
          <div class="d-flex align-items-center gap-2 px-3 py-2 border-bottom">
            <button class="btn btn-sm btn-outline-secondary d-md-none" onclick={onBack}>
              &larr;
            </button>
            <strong class="flex-grow-1 text-truncate">{chat_name(activeChat)}</strong>
            {#if is_typing()}<small class="text-secondary">{t('ui_chat_typing')}</small>{/if}
            {#if activeChat.type !== 'wildlands' && activeChat.type !== 'direct'}
              <button class="btn btn-sm btn-outline-secondary" onclick={() => (showMembers = !showMembers)}>
                {t('ui_chat_members')}
              </button>
            {/if}
            <button class="btn btn-sm btn-outline-secondary" onclick={toggle_sticky}>
              {t('ui_chat_sticky')}{activeMembership?.sticky ? ' ✓' : ''}
            </button>
            <button class="btn btn-sm btn-outline-secondary" onclick={toggle_mute}>
              {activeMembership?.muted ? t('ui_chat_unmute') : t('ui_chat_mute')}
            </button>
          </div>

          {#if showMembers && activeChat.type === 'group'}
            <div class="p-2 border-bottom small">
              {#each membersByChat[activeChat.id] ?? [] as member (member.character_id)}
                <div class="d-flex align-items-center gap-2">
                  <span class="flex-grow-1">
                    {characters[String(member.character_id)]?.display_name ?? member.character_id}
                    <span class="badge text-bg-dark">{member.role}</span>
                  </span>
                  {#if can_manage_group() && member.character_id !== character_id}
                    <button class="btn btn-sm btn-outline-primary py-0" onclick={() => member.character_id !== undefined && invite_friend(member.character_id)}>
                      {t('ui_chat_invite')}
                    </button>
                  {/if}
                </div>
              {/each}
              {#if friends}
                <div class="mt-2">
                  <div class="text-secondary">{t('ui_chat_friends')}</div>
                  {#each friends.friends as friend (friend.character_id)}
                    <button class="btn btn-sm btn-outline-secondary py-0 me-1 mb-1" onclick={() => invite_friend(friend.character_id)}>
                      + {characters[String(friend.character_id)]?.display_name ?? friend.character_id}
                    </button>
                  {/each}
                </div>
              {/if}
              {#if is_owner()}
                <button class="btn btn-sm btn-outline-danger mt-2" onclick={leave_active}>
                  {t('ui_chat_leave')}
                </button>
              {/if}
            </div>
          {/if}

          <div class="chat-messages">
            {#if activeMessages.length === 0}
              <div class="text-secondary small p-3">{t('ui_chat_no_messages')}</div>
            {/if}
            {#each activeMessages as message (message.id)}
              <div class="chat-message" class:mine={message.author_character_id === character_id}>
                {#if message.author_character_id !== character_id}
                  {@const author_avatar_url = avatar_url(message.author_character_id)}
                  {#if author_avatar_url}
                    <img class="chat-avatar me-2" src={author_avatar_url} alt="" />
                  {/if}
                {/if}
                <div class="chat-bubble">
                  {#if message.system_id}
                    <em class="text-secondary">{message.system_id}</em>
                  {:else if message.deleted}
                    <em class="text-secondary">{t('ui_chat_deleted')}</em>
                  {:else}
                    {#if message.author_character_id !== character_id}
                      <div class="chat-author small">{message.author?.display_name ?? ''}</div>
                    {/if}
                    {#if message.reply}
                      <div class="chat-reply small border-start ps-2 mb-1">
                        <strong>{characters[String(message.reply.author_character_id)]?.display_name ?? ''}</strong>
                        <span class="text-truncate d-block">{message.reply.excerpt}</span>
                      </div>
                    {/if}
                    <div class="chat-text">{@html render_markdown(message.body)}</div>
                    {#if message.translation}
                      <div class="chat-translation small mt-1">
                        {#if expandedTranslations.includes(message.id)}
                          <div>{@html render_markdown(message.translation.body)}</div>
                          <button class="btn btn-link btn-sm p-0" onclick={() => toggle_translation(message)}>
                            {t('ui_chat_hide_translation')}
                          </button>
                        {:else}
                          <button class="btn btn-link btn-sm p-0" onclick={() => toggle_translation(message)}>
                            {t('ui_chat_show_translation')}
                          </button>
                        {/if}
                      </div>
                    {/if}
                    {#if message.edited_at}
                      <span class="text-secondary small">({t('ui_chat_edited')})</span>
                    {/if}
                    {#if message.reactions?.length}
                      <div class="chat-reactions mt-1">
                        {#each message.reactions as reaction (reaction.emoji)}
                          <button
                            class="btn btn-sm btn-outline-secondary py-0 px-1 me-1"
                            onclick={() => toggle_reaction(message, reaction.emoji)}
                          >
                            {reaction.emoji} {reaction.character_ids.length}
                          </button>
                        {/each}
                      </div>
                    {/if}
                    <div class="chat-actions small mt-1">
                      <button class="btn btn-link btn-sm p-0 me-2" onclick={() => start_reply(message)}>
                        {t('ui_chat_reply')}
                      </button>
                      <button class="btn btn-link btn-sm p-0 me-2" onclick={() => toggle_bookmark(message)}>
                        {t('ui_chat_bookmark')}
                      </button>
                      {#if message.author_character_id === character_id}
                        <button class="btn btn-link btn-sm p-0 me-2" onclick={() => start_edit(message)}>
                          {t('ui_chat_edit')}
                        </button>
                        <button class="btn btn-link btn-sm p-0 me-2" onclick={() => remove_message(message)}>
                          {t('ui_chat_delete')}
                        </button>
                      {/if}
                      {#each REACTIONS as emoji}
                        <button class="btn btn-link btn-sm p-0 me-1" onclick={() => toggle_reaction(message, emoji)}>
                          {emoji}
                        </button>
                      {/each}
                    </div>
                  {/if}
                </div>
              </div>
            {/each}
          </div>

          <div class="chat-composer border-top p-2">
            {#if replyTo}
              <div class="small d-flex align-items-center mb-1">
                <span class="text-truncate flex-grow-1">
                  {t('ui_chat_reply')}: {replyTo.body}
                </span>
                <button class="btn btn-sm btn-link p-0" onclick={() => (replyTo = null)}>
                  {t('ui_chat_cancel')}
                </button>
              </div>
            {/if}
            {#if editing}
              <div class="small d-flex align-items-center mb-1">
                <span class="flex-grow-1">{t('ui_chat_edit')}</span>
                <button class="btn btn-sm btn-link p-0" onclick={() => { editing = null; composerText = ''; }}>
                  {t('ui_chat_cancel')}
                </button>
              </div>
            {/if}
            {#if actionError}
              <div class="alert alert-warning py-1 px-2 small mb-1">{actionError}</div>
            {/if}
            <textarea
              class="form-control form-control-sm"
              rows="2"
              bind:value={composerText}
              oninput={on_composer_input}
              placeholder={t('ui_chat_markdown_hint')}
            ></textarea>
            <div class="d-flex align-items-center mt-1">
              <small class="text-secondary flex-grow-1">
                {t('ui_chat_chars_left', {
                  count: String(Math.max(0, limit.max_characters - char_count(composerText)))
                })}
                {#if line_count(composerText) > limit.max_lines}
                  · {t('ui_chat_too_many_lines')}
                {/if}
              </small>
              <button class="btn btn-sm btn-primary" onclick={send} disabled={!composerText.trim()}>
                {t('ui_chat_send')}
              </button>
            </div>
          </div>
        {:else}
          <div class="d-flex align-items-center justify-content-center h-100 text-secondary">
            {t('ui_chat_title')}
          </div>
        {/if}
      </section>
    </div>
  {/if}

  {#if showFriends && friends}
    <div class="chat-overlay p-3">
      <div class="d-flex align-items-center mb-2">
        <strong class="flex-grow-1">{t('ui_chat_friends')}</strong>
        <button class="btn btn-sm btn-outline-secondary" onclick={() => (showFriends = false)}>
          {t('ui_chat_cancel')}
        </button>
      </div>
      {#if friends.incoming_requests.length}
        <div class="mb-2">
          <div class="text-secondary small">{t('ui_chat_accept')}</div>
          {#each friends.incoming_requests as request (request.character_id)}
            <div class="d-flex align-items-center gap-2 mb-1">
              <span class="flex-grow-1">
                {characters[String(request.character_id)]?.display_name ?? request.character_id}
              </span>
              <button class="btn btn-sm btn-primary py-0" onclick={() => accept_friend(request.character_id)}>
                {t('ui_chat_accept')}
              </button>
              <button class="btn btn-sm btn-outline-secondary py-0" onclick={() => decline_friend(request.character_id)}>
                {t('ui_chat_decline')}
              </button>
            </div>
          {/each}
        </div>
      {/if}
      {#each friends.friends as friend (friend.character_id)}
        <div class="d-flex align-items-center gap-2 mb-1">
          <span class="flex-grow-1">
            {characters[String(friend.character_id)]?.display_name ?? friend.character_id}
          </span>
          <button class="btn btn-sm btn-outline-primary py-0" onclick={() => open_dm(friend.character_id)}>
            {t('ui_chat_new_dm')}
          </button>
          <button class="btn btn-sm btn-outline-danger py-0" onclick={() => block(friend.character_id)}>
            {t('ui_chat_block')}
          </button>
        </div>
      {/each}
      {#if blockedIds.length}
        <div class="mt-2">
          <div class="text-secondary small">{t('ui_chat_block')}</div>
          {#each blockedIds as id (id)}
            <div class="d-flex align-items-center gap-2 mb-1">
              <span class="flex-grow-1">{characters[String(id)]?.display_name ?? id}</span>
              <button class="btn btn-sm btn-outline-secondary py-0" onclick={() => unblock(id)}>
                {t('ui_chat_unblock')}
              </button>
            </div>
          {/each}
        </div>
      {/if}
    </div>
  {/if}

  {#if showSettings}
    <div class="chat-overlay p-3">
      <div class="d-flex align-items-center mb-2">
        <strong class="flex-grow-1">{t('ui_chat_settings')}</strong>
        <button class="btn btn-sm btn-outline-secondary" onclick={() => (showSettings = false)}>
          {t('ui_chat_cancel')}
        </button>
      </div>
      <div class="mb-2">
        <div class="text-secondary small">{t('ui_chat_language_prefs')}</div>
        <div class="small">
          {langPrefs?.baseline_available
            ? `Baseline: ${langPrefs?.baseline_engine}`
            : 'Translation sidecar unavailable'}
        </div>
        {#each ['en', 'fr', 'de', 'es', 'el', 'ru', 'zh', 'ar', 'he', 'ja'] as lang (lang)}
          <label class="form-check form-check-inline">
            <input
              class="form-check-input"
              type="checkbox"
              checked={!(langPrefs?.disabled_languages ?? []).includes(lang)}
              onchange={() => toggle_language(lang)}
            />
            <span class="form-check-label">{lang}</span>
          </label>
        {/each}
      </div>
      <div class="mb-2">
        <button class="btn btn-sm btn-outline-primary" onclick={enable_push}>
          {t('ui_chat_push_enable')}
        </button>
      </div>
    </div>
  {/if}
</div>

<style>
  .chat-shell {
    position: fixed;
    inset: 0;
    display: flex;
    flex-direction: column;
    background: var(--bs-body-bg);
    z-index: 10;
  }
  .chat-body {
    flex: 1;
    display: flex;
    min-height: 0;
  }
  .chat-list {
    width: 320px;
    max-width: 40vw;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
  }
  .chat-item {
    display: block;
    padding: 0.5rem 0.75rem;
    border: none;
    border-bottom: 1px solid var(--bs-border-color);
    background: transparent;
    color: inherit;
  }
  .chat-item.active {
    background: var(--bs-tertiary-bg);
  }
  .chat-avatar {
    width: 28px;
    height: 28px;
    border-radius: 50%;
    object-fit: cover;
  }
  .chat-thread {
    flex: 1;
    display: flex;
    flex-direction: column;
    min-width: 0;
  }
  .chat-messages {
    flex: 1;
    overflow-y: auto;
    padding: 0.75rem;
  }
  .chat-message {
    display: flex;
    margin-bottom: 0.5rem;
  }
  .chat-message.mine {
    justify-content: flex-end;
  }
  .chat-bubble {
    max-width: 75%;
    padding: 0.4rem 0.6rem;
    border-radius: 0.6rem;
    background: var(--bs-tertiary-bg);
  }
  .chat-message.mine .chat-bubble {
    background: var(--bs-primary-bg-subtle);
  }
  .chat-author {
    font-weight: 600;
    opacity: 0.8;
  }
  .chat-text {
    white-space: normal;
    word-break: break-word;
  }
  .chat-overlay {
    position: absolute;
    top: 3rem;
    right: 1rem;
    width: 320px;
    max-height: 70vh;
    overflow-y: auto;
    background: var(--bs-body-bg);
    border: 1px solid var(--bs-border-color);
    border-radius: 0.5rem;
    z-index: 20;
  }
  @media (max-width: 767px) {
    .chat-list.mobile-hidden,
    .chat-thread.mobile-hidden {
      display: none;
    }
    .chat-list {
      width: 100%;
      max-width: none;
    }
  }
</style>
