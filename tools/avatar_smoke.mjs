#!/usr/bin/env node
/**
 * Avatar smoke test — minor-safe path.
 *
 * Creates a minor account (the only self-service path now that the public age
 * override is gone), verifies the safe-pool picker, the 24h cooldown, and that
 * custom uploads/removals are adult-gated. Adult custom uploads are covered
 * separately after a customer-service/admin adult-status change.
 *
 * Usage: node tools/avatar_smoke.mjs --port 3393
 */
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const portIdx = args.indexOf('--port');
const port = portIdx >= 0 ? parseInt(args[portIdx + 1], 10) : 2290;
const base = `http://127.0.0.1:${port}`;
const png_base64 =
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=';

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
  console.error('AVATAR SMOKE FAILED:', message);
  process.exit(1);
}

const w1 = await safe_words('safe_words_1.txt');
const w2 = await safe_words('safe_words_2.txt');
const username = `avatar_${Date.now().toString(36)}`;
const password = 'avatar_pass_123';

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
if (login.data.adult !== false) fail('new self-service account should be minor');
if (character.is_adult !== false) fail('character self projection should be minor');
if ('avatar_custom_id' in character || 'avatar_storage' in character) {
  fail('login must not expose custom avatar keys/storage');
}

const pool = data(await post('getAvatarPool', { auth, gender: character.sex ?? 'male' }));
const safe = pool.safe?.[0];
if (!safe?.id) fail(`getAvatarPool: ${JSON.stringify(pool)}`);

const set = await post('setCharacterAvatar', { auth, character_id, safe_id: safe.id });
if (set.error) fail(`setCharacterAvatar: ${JSON.stringify(set)}`);

const profile = data(await post('getCharacter', { auth, character_id }));
if (profile.avatar_url !== data(set).avatar_url) {
  fail(`getCharacter avatar_url=${profile.avatar_url} != ${data(set).avatar_url}`);
}
if ('avatar_custom_id' in profile || 'avatar_storage' in profile) {
  fail('getCharacter must not expose custom avatar keys/storage');
}

const cooldown = await post('setCharacterAvatar', { auth, character_id, safe_id: safe.id });
if (!cooldown.error || !cooldown.error.includes('once every 24 hours')) {
  fail(`second safe pick should be cooldown-limited: ${JSON.stringify(cooldown)}`);
}

const upload = await post('uploadCharacterAvatar', {
  auth,
  character_id,
  format: 'png',
  image_base64: png_base64
});
if (upload.error !== 'adult_required') {
  fail(`minor upload should be adult_required: ${JSON.stringify(upload)}`);
}

const remove = await post('removeCustomAvatar', { auth, character_id });
if (remove.error !== 'adult_required') {
  fail(`minor remove should be adult_required: ${JSON.stringify(remove)}`);
}

console.log(`avatar_smoke PASSED (minor character=${character_id} safe=${safe.id})`);
process.exit(0);
