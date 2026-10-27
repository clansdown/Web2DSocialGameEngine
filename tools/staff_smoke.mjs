#!/usr/bin/env node
/**
 * Staff/support smoke test.
 *
 * Creates a staff account and a player account, grants staff via the CLI,
 * exercises account restriction + support + journal/undo, and verifies that a
 * non-staff player cannot reach staff endpoints.
 *
 * Usage: node tools/staff_smoke.mjs --port 3393 --db-dir test_run/smoke_3393
 */
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
function arg(name, fallback) {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : fallback;
}
const port = parseInt(arg('--port', '2290'), 10);
const db_dir = arg('--db-dir', '');
const base = `http://127.0.0.1:${port}`;

async function safe_words(file) {
  return readFileSync(new URL(`../game/config/${file}`, import.meta.url), 'utf8')
    .split('\n')
    .map((s) => s.trim())
    .filter(Boolean);
}

async function post(path, body) {
  const response = await fetch(`${base}/api/${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  return response.json();
}

function data(response) {
  return response && typeof response.data === 'object' && response.data !== null
    ? response.data
    : {};
}

function fail(message) {
  console.error('STAFF SMOKE FAILED:', message);
  process.exit(2);
}

const words1 = await safe_words('safe_words_1.txt');
const words2 = await safe_words('safe_words_2.txt');
const suffix = Date.now().toString(36);

async function create(name) {
  const username = `${name}_${suffix}`;
  const password = 'staff_pass_123';
  const created = await post('createAccount', {
    username,
    password,
    adult: false,
    word1: words1[0],
    word2: words2[0]
  });
  if (created.error || !created.data?.token) fail(`createAccount ${name}: ${JSON.stringify(created)}`);
  const login = await post('login', { auth: { username, password } });
  const token = login.data?.token;
  const character_id = login.data?.characters?.[0]?.id;
  if (!token || !character_id) fail(`login ${name}: ${JSON.stringify(login)}`);
  return { username, password, token, character_id, auth: { username, token } };
}

const staff_account = await create('staff');
const player = await create('player');

if (!db_dir) fail('--db-dir is required');
execFileSync(
  fileURLToPath(new URL('../server/build/staff_admin', import.meta.url)),
  ['--db-dir', db_dir, 'grant', staff_account.username, 'superadmin'],
  { stdio: 'pipe' }
);

const context = data(await post('staffGetContext', { auth: staff_account.auth }));
if (!context.is_staff) fail(`staffGetContext: ${JSON.stringify(context)}`);
if (!context.capabilities?.includes('staff.undo')) fail('staff lacks staff.undo');
console.log(`staff context roles=${context.roles.join(',')} capabilities=${context.capabilities.length}`);

const denied = await post('staffGetContext', { auth: player.auth });
if (denied.error !== 'not_staff') fail(`non-staff should be denied, got ${JSON.stringify(denied)}`);
console.log('non-staff staff endpoint denied');

const search = data(await post('staffSearchAccounts', { auth: staff_account.auth, query: player.username }));
const player_row = (search.accounts ?? []).find((entry) => entry.username === player.username);
if (!player_row) fail(`staffSearchAccounts did not find ${player.username}`);

const social = data(await post('staffSetSocialRestriction', {
  auth: staff_account.auth,
  account_id: player_row.account_id,
  restricted: true,
  reason: 'smoke test'
}));
if (!social.action_id) fail(`set social restriction: ${JSON.stringify(social)}`);
let account = data(await post('staffGetAccount', {
  auth: staff_account.auth,
  account_id: player_row.account_id
}));
if (!account.social_restricted) fail('social restriction did not apply');

function account_character(account_row, character_id) {
  return (account_row.characters ?? []).find((entry) => entry.id === character_id);
}

const pass_denied = await post('staffGrantMonthlyPass', {
  auth: player.auth,
  character_id: player.character_id,
  months: 1,
  reason: 'not staff'
});
if (pass_denied.error !== 'not_staff') {
  fail(`non-staff monthly pass grant should be denied: ${JSON.stringify(pass_denied)}`);
}

const pass_bad_months = await post('staffGrantMonthlyPass', {
  auth: staff_account.auth,
  character_id: player.character_id,
  months: 0,
  reason: 'smoke invalid months'
});
if (pass_bad_months.error !== 'months_out_of_range') {
  fail(`0-month grant should be rejected: ${JSON.stringify(pass_bad_months)}`);
}

const now = Math.floor(Date.now() / 1000);
const day = 86400;
const pass_grant_2 = data(await post('staffGrantMonthlyPass', {
  auth: staff_account.auth,
  character_id: player.character_id,
  months: 2,
  reason: 'smoke grant 2'
}));
if (!pass_grant_2.action_id) fail(`grant 2 months: ${JSON.stringify(pass_grant_2)}`);

account = data(await post('staffGetAccount', {
  auth: staff_account.auth,
  account_id: player_row.account_id
}));
let pass = account_character(account, player.character_id)?.monthly_pass;
if (!pass?.active || pass.permanent) fail(`2-month pass did not activate: ${JSON.stringify(pass)}`);
const two_month_expiry = pass.expires_at;
if (Math.abs(two_month_expiry - (now + 62 * day)) > 10) {
  fail(`2-month expiry expected ~${now + 62 * day}, got ${two_month_expiry}`);
}

const pass_grant_1 = data(await post('staffGrantMonthlyPass', {
  auth: staff_account.auth,
  character_id: player.character_id,
  months: 1,
  reason: 'smoke grant 1'
}));
if (!pass_grant_1.action_id) fail(`grant 1 month: ${JSON.stringify(pass_grant_1)}`);

account = data(await post('staffGetAccount', {
  auth: staff_account.auth,
  account_id: player_row.account_id
}));
pass = account_character(account, player.character_id)?.monthly_pass;
if (Math.abs(pass.expires_at - (two_month_expiry + 31 * day)) > 10) {
  fail(`1-month grant did not stack: ${JSON.stringify(pass)}`);
}
const three_month_expiry = pass.expires_at;

const pass_permanent = data(await post('staffGrantMonthlyPass', {
  auth: staff_account.auth,
  character_id: player.character_id,
  permanent: true,
  reason: 'smoke permanent'
}));
if (!pass_permanent.action_id) fail(`make permanent: ${JSON.stringify(pass_permanent)}`);

account = data(await post('staffGetAccount', {
  auth: staff_account.auth,
  account_id: player_row.account_id
}));
pass = account_character(account, player.character_id)?.monthly_pass;
if (!pass?.active || !pass.permanent) fail(`permanent pass did not apply: ${JSON.stringify(pass)}`);

const pass_revoke = data(await post('staffRevokeMonthlyPass', {
  auth: staff_account.auth,
  character_id: player.character_id,
  reason: 'smoke revoke'
}));
if (!pass_revoke.action_id) fail(`revoke pass: ${JSON.stringify(pass_revoke)}`);

account = data(await post('staffGetAccount', {
  auth: staff_account.auth,
  account_id: player_row.account_id
}));
pass = account_character(account, player.character_id)?.monthly_pass;
if (pass.active) fail(`revoke did not clear the pass: ${JSON.stringify(pass)}`);

const pass_revoke_inactive = await post('staffRevokeMonthlyPass', {
  auth: staff_account.auth,
  character_id: player.character_id,
  reason: 'smoke inactive revoke'
});
if (pass_revoke_inactive.error !== 'pass_not_active') {
  fail(`inactive revoke should be rejected: ${JSON.stringify(pass_revoke_inactive)}`);
}

const pass_restore_permanent = data(await post('staffUndo', {
  auth: staff_account.auth,
  action_id: pass_revoke.action_id,
  reason: 'smoke undo revoke'
}));
if (!pass_restore_permanent.action_id) fail(`undo revoke: ${JSON.stringify(pass_restore_permanent)}`);

account = data(await post('staffGetAccount', {
  auth: staff_account.auth,
  account_id: player_row.account_id
}));
pass = account_character(account, player.character_id)?.monthly_pass;
if (!pass?.permanent) fail(`undo revoke did not restore permanent: ${JSON.stringify(pass)}`);

const pass_restore_three = data(await post('staffUndo', {
  auth: staff_account.auth,
  action_id: pass_permanent.action_id,
  reason: 'smoke undo permanent'
}));
if (!pass_restore_three.action_id) fail(`undo permanent: ${JSON.stringify(pass_restore_three)}`);

account = data(await post('staffGetAccount', {
  auth: staff_account.auth,
  account_id: player_row.account_id
}));
pass = account_character(account, player.character_id)?.monthly_pass;
if (pass.permanent || Math.abs(pass.expires_at - three_month_expiry) > 10) {
  fail(`undo permanent did not restore 3-month expiry: ${JSON.stringify(pass)}`);
}

const pass_restore_two = data(await post('staffUndo', {
  auth: staff_account.auth,
  action_id: pass_grant_1.action_id,
  reason: 'smoke undo extra month'
}));
if (!pass_restore_two.action_id) fail(`undo extra month: ${JSON.stringify(pass_restore_two)}`);

account = data(await post('staffGetAccount', {
  auth: staff_account.auth,
  account_id: player_row.account_id
}));
pass = account_character(account, player.character_id)?.monthly_pass;
if (Math.abs(pass.expires_at - two_month_expiry) > 10) {
  fail(`undo extra month did not restore 2-month expiry: ${JSON.stringify(pass)}`);
}

const pass_restore_none = data(await post('staffUndo', {
  auth: staff_account.auth,
  action_id: pass_grant_2.action_id,
  reason: 'smoke undo original'
}));
if (!pass_restore_none.action_id) fail(`undo original grant: ${JSON.stringify(pass_restore_none)}`);

account = data(await post('staffGetAccount', {
  auth: staff_account.auth,
  account_id: player_row.account_id
}));
pass = account_character(account, player.character_id)?.monthly_pass;
if (pass.active) fail(`undo original grant left a pass: ${JSON.stringify(pass)}`);
const pass_events = pass.events ?? [];
if (pass_events.length !== 8) {
  fail(`expected 8 append-only monthly-pass events, got ${pass_events.length}: ${JSON.stringify(pass_events)}`);
}
if (pass_events[0].event !== 'revoke' || pass_events[0].source !== 'staff_undo') {
  fail(`newest event should be undo revoke: ${JSON.stringify(pass_events[0])}`);
}
if (!pass_events.some((entry) => entry.event === 'grant' && entry.source === 'staff_grant' && entry.months === 2)) {
  fail('2-month staff grant missing from the log');
}
if (!pass_events.some((entry) => entry.event === 'grant' && entry.source === 'staff_grant' && entry.permanent)) {
  fail('permanent staff grant missing from the log');
}
if (!pass_events.some((entry) => entry.event === 'revoke' && entry.source === 'staff_revoke')) {
  fail('staff revoke missing from the log');
}
if (!pass_events.some((entry) => entry.event === 'restore' && entry.source === 'staff_undo')) {
  fail('undo restore missing from the log');
}
console.log(`monthly pass grant/stack/permanent/revoke/undo + ${pass_events.length} log events verified`);

const lock = data(await post('staffSetSupportLock', {
  auth: staff_account.auth,
  account_id: player_row.account_id,
  locked: true,
  reason: 'smoke test'
}));
if (!lock.action_id) fail(`set support lock: ${JSON.stringify(lock)}`);

const blocked = await post('createSupportTicket', {
  auth: player.auth,
  subject: 'Blocked ticket',
  body: 'should not be created',
  category: 'other'
});
if (blocked.error !== 'support_locked') fail(`support lock did not block create: ${JSON.stringify(blocked)}`);
console.log('support-locked player cannot create a ticket');

const created_ticket = data(await post('staffCreateSupportTicket', {
  auth: staff_account.auth,
  account_id: player_row.account_id,
  subject: 'Staff notice',
  body: 'A final staff message.',
  category: 'account'
}));
if (!created_ticket.ticket?.id) fail(`staff create ticket: ${JSON.stringify(created_ticket)}`);

const notifications = data(await post('getNotifications', { auth: player.auth }));
if (!(notifications.notifications ?? []).some((entry) => entry.kind === 'support_ticket')) {
  fail('player did not receive a staff-ticket notification');
}
console.log(`staff-created ticket notified player (unread=${notifications.unread})`);

const player_state = data(await post('getPlayerState', {
  auth: player.auth,
  character_id: player.character_id
}));
if ((player_state.unread_notifications ?? 0) < 1) {
  fail(`getPlayerState did not report unread notifications: ${JSON.stringify(player_state)}`);
}
if (player_state.support_locked !== true) {
  fail('getPlayerState did not report support_locked');
}
console.log('getPlayerState reports unread notifications + support lock');

const journal = data(await post('staffListJournal', { auth: staff_account.auth, limit: 50 }));
if (!(journal.entries ?? []).length) fail('journal is empty');
console.log(`journal entries=${journal.entries.length}`);

const preflight = data(await post('staffPreflightUndo', {
  auth: staff_account.auth,
  action_id: social.action_id
}));
if (preflight.status !== 'available') fail(`undo preflight: ${JSON.stringify(preflight)}`);
const undone = data(await post('staffUndo', {
  auth: staff_account.auth,
  action_id: social.action_id,
  reason: 'smoke undo'
}));
if (!undone.action_id) fail(`undo: ${JSON.stringify(undone)}`);
account = data(await post('staffGetAccount', {
  auth: staff_account.auth,
  account_id: player_row.account_id
}));
if (account.social_restricted) fail('social restriction was not undone');
console.log('journal preflight + undo restored social restriction');

const unstick = data(await post('staffUnstickGameSession', {
  auth: staff_account.auth,
  character_id: player.character_id,
  reason: 'smoke unstick'
}));
if (!unstick.action_id) fail(`unstick: ${JSON.stringify(unstick)}`);

// Chat moderation is part of the same journal. Promote the player to adult so
// they can author a custom message, then have staff delete it and undo.
const adult = data(await post('staffSetAdultStatus', {
  auth: staff_account.auth,
  account_id: player_row.account_id,
  adult: true,
  reason: 'smoke adult'
}));
if (!adult.action_id) fail(`staffSetAdultStatus: ${JSON.stringify(adult)}`);

const chats = data(await post('getChats', {
  auth: player.auth,
  character_id: player.character_id
}));
const wildlands_id = chats.wildlands_chat_id;
if (!wildlands_id) fail(`getChats missing wildlands: ${JSON.stringify(chats)}`);
const sent = data(await post('sendChatMessage', {
  auth: player.auth,
  character_id: player.character_id,
  chat_id: wildlands_id,
  body: 'smoke moderation target'
}));
if (!sent.id) fail(`sendChatMessage: ${JSON.stringify(sent)}`);

const deleted = data(await post('deleteChatMessage', {
  auth: staff_account.auth,
  character_id: staff_account.character_id,
  message_id: sent.id
}));
if (!deleted.id) fail(`staff deleteChatMessage: ${JSON.stringify(deleted)}`);
const chat_journal = data(await post('staffListJournal', {
  auth: staff_account.auth,
  action_type: 'chat.delete_message',
  limit: 10
}));
const chat_entry = (chat_journal.entries ?? [])[0];
if (!chat_entry) fail('chat moderation action was not journaled');
const chat_undo = data(await post('staffUndo', {
  auth: staff_account.auth,
  action_id: chat_entry.id,
  reason: 'smoke chat undo'
}));
if (!chat_undo.action_id) fail(`chat undo: ${JSON.stringify(chat_undo)}`);

const restored = data(await post('getChatMessages', {
  auth: player.auth,
  character_id: player.character_id,
  chat_id: wildlands_id,
  after_seq: 0,
  limit: 200
}));
const restored_message = (restored.messages ?? []).find((entry) => entry.id === sent.id);
if (!restored_message || restored_message.deleted || restored_message.body !== 'smoke moderation target') {
  fail(`chat undo did not restore the message: ${JSON.stringify(restored_message)}`);
}
console.log('scoped/staff chat moderation journaled and undone');

const chat_search = data(await post('staffSearchChats', {
  auth: staff_account.auth,
  query: wildlands_id
}));
if (!(chat_search.chats ?? []).length) fail('staff chat search returned no result');

// CLI revoke is the only demotion path; the next request must lose access.
execFileSync(
  fileURLToPath(new URL('../server/build/staff_admin', import.meta.url)),
  ['--db-dir', db_dir, 'revoke', staff_account.username],
  { stdio: 'pipe' }
);
const revoked = await post('staffGetContext', { auth: staff_account.auth });
if (revoked.error !== 'not_staff') fail(`revoked staff should be denied, got ${JSON.stringify(revoked)}`);
console.log('staff CLI revoke removed access');

console.log('STAFF SMOKE PASSED');
