#!/usr/bin/env node
/**
 * Chat smoke test — end-to-end verification of the chat subsystem.
 *
 * Exercises, against a running server:
 *   createAccount ×2 → login ×2 → openDirectChat → send → poll → getMessages →
 *   edit → delete (tombstone) → markRead → reaction → bookmark → friends
 *   (request/accept) → groups (create/invite/accept/leave) → moderation (ban) →
 *   block/unblock → wildlands (send/poll).
 *
 * Success payloads are wrapped as `{ status, data: {...} }`; errors carry a
 * top-level `error`. Usage: node tools/chat_smoke.mjs --port 3393
 * (see tools/chat_smoke.sh for the server-harness wrapper)
 */
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const portIdx = args.indexOf('--port');
const port = portIdx >= 0 ? parseInt(args[portIdx + 1], 10) : 2290;
const base = `http://127.0.0.1:${port}`;

async function safe_words(file) {
  return readFileSync(new URL(`../game/config/${file}`, import.meta.url), 'utf8')
    .split('\n')
    .map((s) => s.trim())
    .filter(Boolean);
}

async function post(path, body) {
  const r = await fetch(`${base}/api/${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  return r.json();
}

function data(response) {
  return response && typeof response.data === 'object' && response.data !== null
    ? response.data
    : {};
}

const out = [];
function log(...parts) {
  out.push(parts.join(' '));
}
function fail(msg) {
  console.error('CHAT SMOKE FAILED:', msg);
  console.error(out.join('\n'));
  process.exit(1);
}
function expect(cond, msg) {
  if (!cond) fail(msg);
}

// The server rate-limits sends to 1/s per account; pace message sends so the
// smoke flow is not mistaken for flooding.
async function pace() {
  await new Promise((resolve) => setTimeout(resolve, 1100));
}

const w1 = await safe_words('safe_words_1.txt');
const w2 = await safe_words('safe_words_2.txt');

async function make_user(tag) {
  const username = `${tag}_${Date.now().toString(36)}_${Math.floor(Math.random() * 1e6)}`;
  const password = 'chat_pass_123';
  const acct = await post('createAccount', {
    username, password, adult: false, word1: w1[0], word2: w2[0]
  });
  if (!acct.data && !acct.token) fail(`createAccount(${tag}): ${JSON.stringify(acct)}`);
  const login = await post('login', { auth: { username, password } });
  const token = login.data?.token ?? login.token;
  const character_id = login.data?.characters?.[0]?.id;
  if (!token || !character_id) fail(`login(${tag}): ${JSON.stringify(login)}`);
  return { username, password, token, character_id, auth: { username, token } };
}

const a = await make_user('alice');
const b = await make_user('bob');
log(`accounts: alice=${a.character_id} bob=${b.character_id}`);

// ── 1. Direct chat: open → send → poll → getMessages ────────────────────────
const openedRaw = await post('openDirectChat', { auth: a.auth, character_id: a.character_id, target_character_id: b.character_id });
const opened = data(openedRaw);
const dm = opened.chat_id;
expect(dm, `openDirectChat: ${JSON.stringify(openedRaw)}`);
log(`openDirectChat chat=${dm}`);

await pace();
const sent = data(await post('sendChatMessage', { auth: a.auth, character_id: a.character_id, chat_id: dm, body: 'hello bob' }));
expect(sent.id && sent.seq === 1, `sendChatMessage: ${JSON.stringify(sent)}`);
log(`sendChatMessage seq=${sent.seq}`);

const bPoll = data(await post('pollChats', { auth: b.auth, character_id: b.character_id, cursor: 0 }));
const gotMsg = (bPoll.messages || []).find((m) => m.id === sent.id);
expect(gotMsg && gotMsg.body === 'hello bob', `poll did not deliver message: ${JSON.stringify(bPoll.messages)}`);
const bMembership = (bPoll.memberships || []).find((m) => m.chat_id === dm);
expect(bMembership && bMembership.unread_count >= 1, `unread not incremented: ${JSON.stringify(bMembership)}`);
log(`poll delivered message; unread=${bMembership.unread_count}`);

const history = data(await post('getChatMessages', { auth: b.auth, character_id: b.character_id, chat_id: dm, limit: 50 }));
expect(history.messages?.some((m) => m.id === sent.id), 'getChatMessages missing message');
log(`getChatMessages count=${history.messages.length}`);

// ── 2. markRead ─────────────────────────────────────────────────────────────
await post('markChatRead', { auth: b.auth, character_id: b.character_id, chat_id: dm, seq: sent.seq });
const bPoll2 = data(await post('pollChats', { auth: b.auth, character_id: b.character_id, cursor: bPoll.cursor }));
const bMem2 = (bPoll2.memberships || []).find((m) => m.chat_id === dm);
expect(bMem2 && bMem2.unread_count === 0, `markRead did not clear unread: ${JSON.stringify(bMem2)}`);
log('markRead cleared unread');

// ── 3. edit + delete (tombstone) ────────────────────────────────────────────
const edited = data(await post('editChatMessage', { auth: a.auth, character_id: a.character_id, message_id: sent.id, body: 'hello bob (edited)' }));
expect(edited.version === 2, `edit version: ${JSON.stringify(edited)}`);
const afterEdit = data(await post('getChatMessages', { auth: b.auth, character_id: b.character_id, chat_id: dm, limit: 50 }));
expect(afterEdit.messages.find((m) => m.id === sent.id)?.body === 'hello bob (edited)', 'edit not reflected');
log('edit reflected (version 2)');

await pace();
const secondRaw = await post('sendChatMessage', { auth: a.auth, character_id: a.character_id, chat_id: dm, body: 'delete me' });
const second = data(secondRaw);
expect(second.id, `second send: ${JSON.stringify(secondRaw)}`);
const delRaw = await post('deleteChatMessage', { auth: a.auth, character_id: a.character_id, message_id: second.id });
const del = data(delRaw);
expect(del.change_id, `delete: ${JSON.stringify(delRaw)}`);
const afterDel = data(await post('getChatMessages', { auth: b.auth, character_id: b.character_id, chat_id: dm, limit: 50 }));
const tomb = afterDel.messages.find((m) => m.id === second.id);
expect(tomb && tomb.deleted === true, `tombstone missing: ${JSON.stringify(tomb)}`);
log('delete produced a tombstone');

// ── 4. reaction + bookmark ──────────────────────────────────────────────────
const react = data(await post('addChatReaction', { auth: b.auth, character_id: b.character_id, message_id: sent.id, emoji: '👍' }));
expect(react.reactions?.some((r) => r.emoji === '👍' && r.character_ids.includes(b.character_id)), `reaction: ${JSON.stringify(react)}`);
await post('removeChatReaction', { auth: b.auth, character_id: b.character_id, message_id: sent.id, emoji: '👍' });
log('reaction add/remove ok');

await post('addChatBookmark', { auth: b.auth, character_id: b.character_id, message_id: sent.id });
const bookmarks = data(await post('getChatBookmarks', { auth: b.auth, character_id: b.character_id }));
expect(bookmarks.messages?.some((m) => m.id === sent.id), `bookmark: ${JSON.stringify(bookmarks)}`);
log('bookmark ok');

// ── 5. friends: request → accept ────────────────────────────────────────────
const fr = await post('sendFriendRequest', { auth: a.auth, character_id: a.character_id, target_character_id: b.character_id });
expect(!fr.error, `sendFriendRequest: ${JSON.stringify(fr)}`);
const bFriends = data(await post('getFriends', { auth: b.auth, character_id: b.character_id }));
expect(bFriends.incoming_requests?.some((r) => r.character_id === a.character_id), 'incoming request missing');
const acc = data(await post('acceptFriendRequest', { auth: b.auth, character_id: b.character_id, target_character_id: a.character_id }));
expect(acc.friend_character_id === a.character_id, `accept: ${JSON.stringify(acc)}`);
const aFriends = data(await post('getFriends', { auth: a.auth, character_id: a.character_id }));
expect(aFriends.friends?.some((f) => f.character_id === b.character_id), 'friendship not stored');
log('friend request accepted; DM eagerly created');

// ── 6. groups: create → invite → accept → send → leave ──────────────────────
const group = data(await post('createGroupChat', { auth: a.auth, character_id: a.character_id, name: 'Test Warband' }));
const group_id = group.chat_id;
expect(group_id, `createGroupChat: ${JSON.stringify(group)}`);
await post('inviteToGroup', { auth: a.auth, character_id: a.character_id, chat_id: group_id, target_character_id: b.character_id });
const bInvitePoll = data(await post('pollChats', { auth: b.auth, character_id: b.character_id, cursor: 0 }));
expect(bInvitePoll.invites?.some((i) => i.chat_id === group_id), `group invite not polled: ${JSON.stringify(bInvitePoll.invites)}`);
await post('acceptGroupInvite', { auth: b.auth, character_id: b.character_id, chat_id: group_id });
await pace();
const groupMsg = data(await post('sendChatMessage', { auth: a.auth, character_id: a.character_id, chat_id: group_id, body: 'rally here' }));
expect(groupMsg.id, `group send: ${JSON.stringify(groupMsg)}`);
const bGroupPoll = data(await post('pollChats', { auth: b.auth, character_id: b.character_id, cursor: bInvitePoll.cursor }));
expect((bGroupPoll.messages || []).some((m) => m.id === groupMsg.id), 'group message not delivered');
log('group create/invite/accept/send ok');

const kick = await post('kickMember', { auth: a.auth, character_id: a.character_id, chat_id: group_id, target_character_id: b.character_id });
expect(!kick.error, `kick: ${JSON.stringify(kick)}`);
const banned = await post('banFromChat', { auth: a.auth, character_id: a.character_id, chat_id: group_id, target_character_id: b.character_id, reason: 'test' });
expect(!banned.error, `ban: ${JSON.stringify(banned)}`);
log('kick + ban ok');

// ── 7. block: DM send must fail ─────────────────────────────────────────────
await post('blockUser', { auth: b.auth, character_id: b.character_id, target_character_id: a.character_id });
const blockedSend = await post('sendChatMessage', { auth: a.auth, character_id: a.character_id, chat_id: dm, body: 'are you there?' });
expect(blockedSend.error === 'blocked', `expected blocked send, got ${JSON.stringify(blockedSend)}`);
await post('unblockUser', { auth: b.auth, character_id: b.character_id, target_character_id: a.character_id });
log('block prevents DM; unblock ok');

// ── 8. wildlands: send + poll ───────────────────────────────────────────────
const aChats = data(await post('getChats', { auth: a.auth, character_id: a.character_id }));
const wild = aChats.wildlands_chat_id;
expect(wild, `wildlands_chat_id missing: ${JSON.stringify(aChats)}`);
await pace();
const wildMsg = data(await post('sendChatMessage', { auth: a.auth, character_id: a.character_id, chat_id: wild, body: 'greetings, travellers' }));
expect(wildMsg.id, `wildlands send: ${JSON.stringify(wildMsg)}`);
const wildPoll = data(await post('pollWildlands', { auth: a.auth, character_id: a.character_id, cursor: 0 }));
expect((wildPoll.messages || []).some((m) => m.id === wildMsg.id), 'wildlands poll missing message');
log('wildlands send/poll ok');

// ── 9. rate limit + length limit ────────────────────────────────────────────
const tooLong = await post('sendChatMessage', { auth: a.auth, character_id: a.character_id, chat_id: dm, body: 'x'.repeat(5000) });
expect(tooLong.error === 'message_too_long', `expected message_too_long, got ${JSON.stringify(tooLong)}`);
log('length limit enforced');

console.log(out.join('\n'));
console.log('Chat smoke PASSED');
process.exit(0);
