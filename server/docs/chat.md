# Chat System

Server-authoritative chat for Ravenest Build and Battle. Five chat types —
`direct` (DM), `group`, `barony`, `dukedom` (schema-only for now) and
`wildlands` — with per-character membership, aggregate polling, integrated
translation, Web Push, and admin identity locks.

The full design rationale lives in the build plan; this file is the living
architecture reference. Config files are documented in `AGENTS.md`; the SQL
tables each have a doc under `server/tables/`.

## Storage

Chat lives in its **own SQLite database** (`chat.db`), independent of `game.db`.
There are **no cross-database foreign keys** — character/chat ids are plain
integers. `Database::getInstance().chatDB()` is the handle; `initializeChatDB()`
creates the schema.

Cross-cutting primitives:

- **`seq`** — per-chat monotonic position (pagination/read state).
- **`change_id`** — a global monotonic counter (`change_counter`). Every mutated
  row is stamped, so one integer cursor detects updates across all tables.
  Deletes are **tombstones** (`messages.deleted_at`).
- **`character_relation_counts`** — fast path: when a character has zero
  block/mute relationships, unread/poll filtering skips per-member checks.

## Membership

- **Direct** — canonical `direct_key = "<min_id>:<max_id>"`; created eagerly on
  friendship, lazily on first open. Blocking either way prevents a DM.
- **Group** — owner + moderators; invite must be accepted; max
  `group_max_members`; owner-only rename with a rolling 24h cooldown; transfer
  demotes the old owner to moderator; an owner who leaves hands off to the
  latest poster (or the longest-standing member), else the group is archived.
  A per-group `members_can_invite` toggle lets any member invite.
- **Barony** — materialized from `baronies`/`barony_members` at startup
  (`ChatService::reconcile_alliances`) and on game events
  (`sync_barony_chat`). Owner = `baron_character_id ?? owner_character_id`.
- **Dukedom** — schema/type only; no membership until the game has dukedoms.
- **Wildlands** — one global chat, open to everyone; never materialized and
  never part of `pollChats` (its own `pollWildlands`, no badge, no push).

## Minor visibility and safe content

Chat follows `docs/minor_content_visibility.md`:

- A minor sees only messages with `safe_message_id` (plus server system
  messages). Custom messages are omitted entirely — no placeholder, unread
  count, preview, mention, bookmark, or push.
- A minor authors only a `safe_message_id` and, optionally, a `minor_safe`
  GIF id. `body` from a minor is rejected with
  `minor_custom_content_forbidden`.
- `getChats`/`pollChats` resolve names/avatars and last-message previews through
  the viewer context. Group chats are adult-only: minors neither see them nor
  receive their invites.
- Reactions and GIFs are server catalog ids. The legacy `message_reactions.emoji`
  column stores the reaction id; arbitrary emoji are rejected.
- Barony chat titles come from the barony's safe/custom identity, never from
  the raw `chats.name` column.
- Hidden custom messages do not create an unread badge because
  `apply_unread`/`dispatch_push` use the same visibility decision.
- `requestTranslations` ignores safe messages (server text) and custom messages
  for a minor viewer.

## Endpoints

All POST JSON under `/api/*`, `{status:"ok", data:{...}}` / `{error:"..."}`.
Per-endpoint docs live in `api/`.

| Area | Endpoints |
|---|---|
| Core | `getChats`, `getChatMessages`, `pollChats`, `pollWildlands`, `sendChatMessage`, `editChatMessage`, `deleteChatMessage`, `markChatRead`, `setChatSettings`, `openDirectChat`, `searchChatCharacters` |
| Translation | `requestTranslations`, `getChatLanguagePrefs`, `setChatLanguagePref` |
| Reactions/bookmarks | `addChatReaction`, `removeChatReaction`, `addChatBookmark`, `removeChatBookmark`, `getChatBookmarks` |
| Social | `sendFriendRequest`, `acceptFriendRequest`, `declineFriendRequest`, `unfriend`, `blockUser`, `unblockUser`, `muteCharacter`, `unmuteCharacter`, `getFriends` |
| Groups | `createGroupChat`, `inviteToGroup`, `acceptGroupInvite`, `declineGroupInvite`, `leaveChat`, `transferGroupOwnership`, `setGroupModerator`, `kickMember`, `renameGroup`, `setGroupInvitePolicy` |
| Moderation | `banFromChat`, `unbanFromChat`, `getModerationLog` |
| Presence | `chatHeartbeat`, `getFriendPresence`, `setChatTyping` |
| Push | `getPushConfig`, `subscribePush`, `unsubscribePush`, `setPushPreferences` |
| Admin | `adminGetIdentityHistory`, `adminLockIdentity` |

## Polling protocol

`pollChats { character_id, cursor, lang }` returns typed delta collections:
`chats`, `memberships` (my rows), `members`, `messages`, `translations`,
`friend_events`, `invites`, `bans`, `characters`. Rows with
`change_id > cursor` are returned (capped); `cursor` advances to the max
applied. A fresh device calls `getChats` for the baseline cursor and then polls
forward; history is fetched only via `getChatMessages`.

`pollWildlands` uses the same envelope for the single wildlands chat.

The transport is deliberately polling-first. The delta envelope is
transport-agnostic, so a future `/ws/chat` is additive.

## Translation

Integral, not bolted on. Every message stores a detected `source_language` and
confidence. On view, `requestTranslations` returns stored translations for the
viewer's language or generates them via the baseline engine, storing them per
`(message_id, target_language, engine, model_version)`. The UI shows the original
and a collapsible translation (shown by default).

Engines (`server/chat/TranslationEngine.hpp`):

- **Baseline** — `MadladTranslationEngine` calls the internal localhost sidecar
  (`server/translation/`): MADLAD-400-3B via CTranslate2, Lingua detection. If
  the sidecar is down the game degrades to the original text.
- **Premium LLM** — `StubLlmTranslationEngine`, deliberately inert. The seam
  accepts a target-language set and conversation context so a real backend is
  additive (deferred).

Per-character language preferences are stored in `chat_language_prefs` (absence
= enabled; default: translate every source language except the account's
**display language**). The display language is the interface language chosen at
first launch and persisted on `users.language` at account creation; the client
adopts it on every device. When no `lang` is supplied, the server defaults the
translation target to it.

## Notifications (Web Push)

`PushSender` implements VAPID (ES256 JWT) + RFC 8291 `aes128gcm` encryption and
POSTs via libcurl. Subscriptions are keyed on **account**; the payload names the
addressed character. Eligibility: not wildlands; not chat-muted; author not
person-muted/blocked; DMs push for friends (per pref) or `notify_level='all'`;
group/barony/dukedom push only with the `group_mentions` opt-in and a mention
(or `notify_level='all'`). The service worker (`client/public/sw.js`) suppresses
the OS notification when a client window is focused (per-device dedupe).

Push bodies are translated exactly as the recipient would see the message when
viewing it: to their display language (`users.language`), skipping when the
source matches the display language or the recipient disabled that source
language, using stored-or-library translations only (never the LLM engine, and
never a blocking LLM call). If only an LLM translation would exist, the body
falls back to the original; the recipient still sees the LLM translation when
they open the chat.

## Moderation & admin

Chat-level bans (`chat_bans`) and an append-only `moderation_log` retained
forever. `mods_see_blocked` (config, default true) lets moderators see blocked
users in chats they moderate. Admins are account ids in
`game/config/chat_admins.json`. Admin identity locks are documented in
`server/docs/chat_identity_lock.md`.

## Rate limiting

Per account: `1/s`, `30/min`, `100/5min` (config). A breach records a violation
and applies the next escalating timeout
(`30s → 5m → 30m → 2h → 24h → 7d`); levels decay after `decay_seconds`.
Enforced on `sendChatMessage`.

## Presence

In-memory only (`server/chat/Presence.hpp`): `chatHeartbeat` marks online (60s
TTL); `setChatTyping` sets a ~5s typing flag. `getFriendPresence` returns
online/typing for friends. Nothing is persisted; a restart drops everyone
offline.

## Deferred seams

- Real LLM translation backend.
- Wildlands topics (`chats.topic_id` reserved).
- Admin-contact / appeal subsystem.
- Dukedom membership.
- GIF provider/curation (one server-provided GIF per message; `messages.gif`).
- Library-translation reaping.
- Server-side search (search is client-side over the loaded window).
- Asynchronous push dispatch (currently synchronous on the HTTP thread).
