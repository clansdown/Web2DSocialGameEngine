#!/usr/bin/env node
/**
 * Avatar storage smoke test (local backend).
 *
 * Exercises createAccount(adult) → login avatar fields → upload → age-gated GET
 * → cooldown / invalid-format errors. The same flow works against R2; the
 * public URL assertion is selected by the AVATAR_EXPECT_STORAGE env var.
 *
 * Usage: node tools/avatar_smoke.mjs --port 3393
 */
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const portIdx = args.indexOf('--port');
const port = portIdx >= 0 ? parseInt(args[portIdx + 1], 10) : 2290;
const base = `http://127.0.0.1:${port}`;
const expected_storage = process.env.AVATAR_EXPECT_STORAGE ?? 'local';
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
  adult: true,
  override_code: 'a1b2c3d4',
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
if (login.data.adult !== true) fail('account did not become adult');
if (character.avatar_storage !== expected_storage) {
  fail(`login avatar_storage=${character.avatar_storage}, expected ${expected_storage}`);
}

const upload = await post('uploadCharacterAvatar', {
  auth,
  character_id,
  format: 'png',
  image_base64: png_base64
});
const uploaded = data(upload);
if (upload.error || !uploaded.avatar_custom_id) {
  fail(`upload: ${JSON.stringify(upload)}`);
}
if (!/^\d+\/[0-9a-f]{64}\.png$/.test(uploaded.avatar_custom_id)) {
  fail(`avatar_custom_id is not content-addressed: ${uploaded.avatar_custom_id}`);
}
if (uploaded.avatar_storage !== expected_storage) {
  fail(`upload avatar_storage=${uploaded.avatar_storage}, expected ${expected_storage}`);
}
if (expected_storage === 'local') {
  if (uploaded.avatar_url !== `/api/avatar/${character_id}`) {
    fail(`local avatar_url=${uploaded.avatar_url}`);
  }
} else if (!/^https?:\/\/.+\/avatars\//.test(uploaded.avatar_url)) {
  fail(`r2 avatar_url is not a public asset URL: ${uploaded.avatar_url}`);
}

const fetched = await fetch(
  `${base}/api/avatar/${character_id}?username=${encodeURIComponent(username)}` +
    `&token=${encodeURIComponent(token)}`
);
if (fetched.status !== 200) fail(`avatar GET status ${fetched.status}`);
if (expected_storage === 'local') {
  if (!String(fetched.headers.get('content-type')).startsWith('image/png')) {
    fail(`avatar content-type ${fetched.headers.get('content-type')}`);
  }
  const bytes = new Uint8Array(await fetched.arrayBuffer());
  if (bytes.length < 8 || bytes[0] !== 0x89 || bytes[1] !== 0x50) {
    fail('avatar GET did not return the uploaded PNG');
  }
}

const unauth = await fetch(`${base}/api/avatar/${character_id}`);
if (unauth.status !== 404) fail(`unauthenticated avatar GET status ${unauth.status}`);

const profile = data(await post('getCharacter', { auth, character_id }));
if (profile.avatar_storage !== expected_storage) {
  fail(`getCharacter avatar_storage=${profile.avatar_storage}`);
}
if (profile.avatar_custom_id !== uploaded.avatar_custom_id) {
  fail('getCharacter did not return the uploaded key');
}

const cooldown = await post('uploadCharacterAvatar', {
  auth,
  character_id,
  format: 'png',
  image_base64: png_base64
});
if (!cooldown.error || !cooldown.error.includes('once every 24 hours')) {
  fail(`second upload should be cooldown-limited: ${JSON.stringify(cooldown)}`);
}

const remove = await post('removeCustomAvatar', { auth, character_id });
if (!remove.error || !remove.error.includes('once every 24 hours')) {
  fail(`remove should be cooldown-limited: ${JSON.stringify(remove)}`);
}

const invalid = await post('uploadCharacterAvatar', {
  auth,
  character_id,
  format: 'gif',
  image_base64: png_base64
});
if (!invalid.error || !invalid.error.includes('Unsupported image format')) {
  fail(`invalid format should be rejected: ${JSON.stringify(invalid)}`);
}

console.log(
  `avatar_smoke PASSED (character=${character_id} storage=${expected_storage} ` +
    `key=${uploaded.avatar_custom_id})`
);
process.exit(0);
