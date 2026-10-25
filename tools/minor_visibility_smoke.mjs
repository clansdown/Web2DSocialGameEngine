#!/usr/bin/env node
/**
 * Minor content-visibility smoke test.
 *
 * Exercises the self-service minor path: safe content catalogs, safe message
 * authoring, reaction filtering, group adult-only enforcement, barony safe-name
 * requirements, avatar age gates, and the removal of the public age override.
 *
 * Usage: node tools/minor_visibility_smoke.mjs --port 3394
 */
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const portIdx = args.indexOf('--port');
const port = portIdx >= 0 ? parseInt(args[portIdx + 1], 10) : 2290;
const base = `http://127.0.0.1:${port}`;
const png_base64 =
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=';

async function words(file) {
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
  console.error('MINOR VISIBILITY SMOKE FAILED:', message);
  process.exit(1);
}

function expect_error(response, code) {
  if (response.error !== code) {
    fail(`expected error ${code}, got ${JSON.stringify(response)}`);
  }
}

const w1 = await words('safe_words_1.txt');
const w2 = await words('safe_words_2.txt');
const username = `minor_${Date.now().toString(36)}`;
const password = 'minor_pass_123';

const account = await post('createAccount', {
  username,
  password,
  adult: false,
  word1: w1[0],
  word2: w2[0]
});
if (account.error) fail(`createAccount: ${JSON.stringify(account)}`);

const login = await post('login', { auth: { username, password } });
const token = login.data?.token ?? login.token;
const character = login.data?.characters?.[0];
if (!token || !character) fail(`login: ${JSON.stringify(login)}`);
const character_id = character.id;
const auth = { username, token };

if (login.data.adult !== false) fail('self-service account should be minor');
if (character.is_adult !== false) fail('self projection should report minor');
if ('avatar_custom_id' in character || 'avatar_storage' in character) {
  fail('login leaked a custom avatar key/storage');
}

const safe = data(await post('getSafeContent', { auth }));
if (safe.is_adult !== false) fail('safe content did not report minor');
if (!safe.messages?.length) fail('no safe messages');
if ((safe.reactions ?? []).some((r) => !r.minor_safe)) fail('minor received an unsafe reaction');
if ((safe.gifs ?? []).some((g) => !g.minor_safe)) fail('minor received an unsafe GIF');

const chats = data(await post('getChats', { character_id, auth }));
const wildlands = chats.wildlands_chat_id;
if (!wildlands) fail('missing wildlands chat');

expect_error(await post('sendChatMessage', {
  character_id, auth, chat_id: wildlands, body: 'custom text'
}), 'minor_custom_content_forbidden');

const sent = await post('sendChatMessage', {
  character_id, auth, chat_id: wildlands, safe_message_id: 'greeting_hello'
});
if (sent.error) fail(`safe send failed: ${JSON.stringify(sent)}`);
if (data(sent).safe_message_id !== 'greeting_hello') fail('safe message id not round-tripped');

const history = data(await post('getChatMessages', {
  character_id, auth, chat_id: wildlands, limit: 20
}));
const mine = (history.messages ?? []).find((m) => m.id === data(sent).id);
if (!mine || mine.body !== 'Hello.' || mine.safe_message_id !== 'greeting_hello') {
  fail(`safe message projection wrong: ${JSON.stringify(mine)}`);
}

expect_error(await post('addChatReaction', {
  character_id, auth, message_id: data(sent).id, reaction_id: 'skull'
}), 'reaction_not_minor_safe');
const reaction = await post('addChatReaction', {
  character_id, auth, message_id: data(sent).id, reaction_id: 'thumbs_up'
});
if (reaction.error) fail(`safe reaction failed: ${JSON.stringify(reaction)}`);

expect_error(await post('createGroupChat', {
  character_id, auth, name: 'Secret club'
}), 'group_adults_only');

expect_error(await post('createBarony', {
  character_id, auth, name: 'Stormhold'
}), 'safe_name_id required');

expect_error(await post('startBaronTrack', {
  character_id, auth, honor_name: 'Stormhold'
}), 'safe_barony_name_id required');

expect_error(await post('updateUserProfile', { auth, adult: true }), 'adult_status_managed');
expect_error(await post('verifyAgeOverride', { auth, code: 'a1b2c3d4' }),
             'Unknown endpoint: verifyAgeOverride');

expect_error(await post('uploadCharacterAvatar', {
  auth, character_id, format: 'png', image_base64: png_base64
}), 'adult_required');

const profile = data(await post('getCharacter', { auth, character_id }));
if ('avatar_custom_id' in profile || 'avatar_storage' in profile) {
  fail('getCharacter leaked a custom avatar key/storage');
}

console.log(`minor_visibility_smoke PASSED (character=${character_id})`);
process.exit(0);
