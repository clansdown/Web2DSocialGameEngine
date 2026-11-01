#!/usr/bin/env node
/**
 * Translation smoke test — verifies the asynchronous chat translation queue
 * against a running server backed by the passthrough sidecar (see
 * tools/translation_smoke.sh).
 *
 * Flow: createAccount ×N → login → openDirectChat → send (source "en") →
 * requestTranslations(target "fr") (returns immediately, enqueues) → poll until
 * the stored translation appears → edit-race check → slow-sidecar send latency
 * → translation_targets (adult/minor/block/source/cap rules).
 *
 * Usage: node tools/translation_smoke.mjs --port 3394
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

function fail(msg) {
  console.error('TRANSLATION SMOKE FAILED:', msg);
  process.exit(1);
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

const w1 = await safe_words('safe_words_1.txt');
const w2 = await safe_words('safe_words_2.txt');

const invite_codes = (process.env.TRANSLATION_INVITE_CODES ?? '')
  .split(',')
  .map((code) => code.trim())
  .filter(Boolean);
let invite_index = 0;

async function make_user(tag, language, is_adult = true) {
  const username = `${tag}_${Date.now().toString(36)}_${Math.floor(Math.random() * 1e6)}`;
  const password = 'trans_pass_123';
  const body = {
    username, password, adult: is_adult, word1: w1[0], word2: w2[0]
  };
  if (language) body.language = language;
  if (invite_index < invite_codes.length) body.invitation_code = invite_codes[invite_index++];
  const acct = await post('createAccount', body);
  if (!acct.data && !acct.token) fail(`createAccount(${tag}): ${JSON.stringify(acct)}`);
  const login = await post('login', { auth: { username, password } });
  const token = login.data?.token ?? login.token;
  const character_id = login.data?.characters?.[0]?.id;
  if (!token || !character_id) fail(`login(${tag}): ${JSON.stringify(login)}`);
  return {
    username, token, character_id, auth: { username, token },
    language: login.data?.language
  };
}

async function request_translations(user, message_id) {
  return data(await post('requestTranslations', {
    auth: user.auth, character_id: user.character_id, message_ids: [message_id], lang: 'fr'
  }));
}

async function wait_for_translation(user, message_id, expected) {
  const deadline = Date.now() + 6000;
  while (Date.now() < deadline) {
    const translated = await request_translations(user, message_id);
    const entry = translated.translations?.[message_id];
    if (entry?.body) {
      if (expected && entry.body !== expected) {
        fail(`unexpected translation body: ${JSON.stringify(entry)}`);
      }
      return entry;
    }
    await sleep(150);
  }
  fail(`translation did not arrive for ${message_id}`);
}

const a = await make_user('trans_a', 'fr');
const b = await make_user('trans_b', 'en');

// The interface language chosen at creation is persisted and is the display
// language (the default chat translation target).
if (a.language !== 'fr') fail(`account language not persisted: ${JSON.stringify(a.language)}`);
const prefs = data(await post('getChatLanguagePrefs', { auth: a.auth, character_id: a.character_id }));
if (prefs.game_language !== 'fr') {
  fail(`getChatLanguagePrefs.game_language expected 'fr', got ${JSON.stringify(prefs.game_language)}`);
}
console.log(`account language persisted and resolved: ${a.language} / ${prefs.game_language}`);

const opened = data(await post('openDirectChat', {
  auth: a.auth, character_id: a.character_id, target_character_id: b.character_id
}));
const chat_id = opened.chat_id;
if (!chat_id) fail(`openDirectChat: ${JSON.stringify(opened)}`);

const send_started = Date.now();
const sent = data(await post('sendChatMessage', {
  auth: a.auth, character_id: a.character_id, chat_id, body: 'hello world', language: 'en'
}));
const send_ms = Date.now() - send_started;
if (!sent.id) fail(`sendChatMessage: ${JSON.stringify(sent)}`);
if (send_ms > 1500) fail(`sendChatMessage blocked for ${send_ms}ms`);
console.log(`sendChatMessage returned in ${send_ms}ms (async translation)`);

// requestTranslations must return cached rows only and enqueue the miss. With a
// 400ms sidecar delay it must not wait for the sidecar.
const request_started = Date.now();
const first = await request_translations(a, sent.id);
const request_ms = Date.now() - request_started;
if (request_ms > 300) {
  fail(`requestTranslations blocked for ${request_ms}ms (expected async enqueue)`);
}
if (first.translations?.[sent.id]?.body) {
  fail(`first request unexpectedly returned a translation: ${JSON.stringify(first)}`);
}
console.log(`requestTranslations returned in ${request_ms}ms; translation queued`);

const entry = await wait_for_translation(a, sent.id, 'fr:hello world');
if (entry.tier !== 'baseline') {
  fail(`expected baseline tier, got ${JSON.stringify(entry.tier)}`);
}
console.log(`asynchronous translation stored: ${entry.body} (tier ${entry.tier})`);

// A second request must be served from the stored library translation.
const again = await request_translations(a, sent.id);
if (!again.translations?.[sent.id]?.body) fail('stored translation not returned on replay');
console.log('cached translation returned on replay');

// Edit-race: a queued translation for the pre-edit version must be dropped.
const raced = data(await post('sendChatMessage', {
  auth: a.auth, character_id: a.character_id, chat_id, body: 'before edit', language: 'en'
}));
if (!raced.id) fail(`sendChatMessage(race): ${JSON.stringify(raced)}`);
await request_translations(a, raced.id);
const edited = data(await post('editChatMessage', {
  auth: a.auth, character_id: a.character_id, message_id: raced.id, body: 'after edit'
}));
if (!edited.version || edited.version < 2) {
  fail(`editChatMessage: ${JSON.stringify(edited)}`);
}
await sleep(2000);
const stale = await request_translations(a, raced.id);
if (stale.translations?.[raced.id]?.body) {
  fail(`stale pre-edit translation was stored: ${JSON.stringify(stale.translations[raced.id])}`);
}
const fresh = await wait_for_translation(a, raced.id, 'fr:after edit');
if (!fresh.body) fail('post-edit translation did not arrive');
console.log('edit-race stale translation dropped; post-edit translation stored');

// ── Premium room-language selection (Phase 2 seam) ──────────────────────────
async function targets(user, chat_id, source_language = 'fr') {
  const response = await post('testTranslationTargets', {
    auth: user.auth, character_id: user.character_id, chat_id, source_language
  });
  if (!Array.isArray(response.data?.languages)) {
    fail(`testTranslationTargets: ${JSON.stringify(response)}`);
  }
  return response.data.languages;
}

function expect_targets(label, actual, expected) {
  const a = [...actual].sort();
  const e = [...expected].sort();
  if (JSON.stringify(a) !== JSON.stringify(e)) {
    fail(`${label}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

// A direct chat targets the other adult's language.
const dm = data(await post('openDirectChat', {
  auth: a.auth, character_id: a.character_id, target_character_id: b.character_id
}));
expect_targets('direct adult target', await targets(a, dm.chat_id, 'fr'), ['en']);

// A minor in the room is excluded.
const minor = await make_user('trans_minor', 'ja', false);
const minor_dm = data(await post('openDirectChat', {
  auth: a.auth, character_id: a.character_id, target_character_id: minor.character_id
}));
expect_targets('minor excluded', await targets(a, minor_dm.chat_id, 'fr'), []);

// An adult with any block involving the author is excluded.
const blocker = await make_user('trans_blocker', 'da');
const blocker_dm = data(await post('openDirectChat', {
  auth: a.auth, character_id: a.character_id, target_character_id: blocker.character_id
}));
expect_targets('pre-block target', await targets(a, blocker_dm.chat_id, 'fr'), ['da']);
const blocked = await post('blockUser', {
  auth: blocker.auth, character_id: blocker.character_id, target_character_id: a.character_id
});
if (blocked.error) fail(`blockUser: ${JSON.stringify(blocked)}`);
expect_targets('blocked member excluded', await targets(a, blocker_dm.chat_id, 'fr'), []);

// A group with nine adult languages is capped to eight, frequency-tied
// languages ordered lexicographically, and the source language removed.
const group = data(await post('createGroupChat', {
  auth: a.auth, character_id: a.character_id, name: 'translation target room'
}));
const group_id = group.chat_id;
if (!group_id) fail(`createGroupChat: ${JSON.stringify(group)}`);
const group_languages = ['en', 'de', 'es', 'it', 'pt', 'nl', 'pl', 'sv'];
const group_members = [b];
for (let i = 0; i < group_languages.length; i++) {
  const language = group_languages[i];
  if (language === 'en') continue;
  group_members.push(await make_user(`trans_room_${i}`, language));
}
// Ninth adult language so the cap actually drops one.
group_members.push(await make_user('trans_room_da', 'da'));
for (const member of group_members) {
  const invited = await post('inviteToGroup', {
    auth: a.auth, character_id: a.character_id, chat_id: group_id,
    target_character_id: member.character_id
  });
  if (invited.error) fail(`inviteToGroup: ${JSON.stringify(invited)}`);
  const accepted = await post('acceptGroupInvite', {
    auth: member.auth, character_id: member.character_id, chat_id: group_id
  });
  if (accepted.error) fail(`acceptGroupInvite: ${JSON.stringify(accepted)}`);
}
expect_targets('group language cap', await targets(a, group_id, 'fr'),
  ['da', 'de', 'en', 'es', 'it', 'nl', 'pl', 'pt']);

console.log('translation_targets: adult, minor, block, source and cap rules verified');

console.log('Translation smoke PASSED');
process.exit(0);
