#!/usr/bin/env node
/**
 * Profile identity smoke test.
 *
 * Covers the custom/safe name editors: safe-name regeneration, custom-name
 * set/clear, safe-name-follows-visible, validation errors, and minor
 * custom-name rejection. The adult account uses the test-only
 * RAVENEST_TEST_ADULT_ACCOUNTS hook.
 *
 * Usage: node tools/profile_smoke.mjs --port 3395
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
  console.error('PROFILE SMOKE FAILED:', message);
  process.exit(1);
}

function expect_ok(response, label) {
  if (response.error) fail(`${label}: ${JSON.stringify(response)}`);
  return data(response);
}

const w1 = await safe_words('safe_words_1.txt');
const w2 = await safe_words('safe_words_2.txt');
const suffix = Date.now().toString(36);

async function create_account(tag, adult) {
  const username = `profile_${tag}_${suffix}`;
  const password = 'profile_pass_123';
  const body = { username, password, word1: w1[0], word2: w2[0] };
  if (adult) body.adult = true;
  const created = await post('createAccount', body);
  expect_ok(created, `createAccount(${tag})`);

  const login = await post('login', { auth: { username, password } });
  const token = login.data?.token ?? login.token;
  const character = login.data?.characters?.[0];
  if (!token || !character) fail(`login(${tag}): ${JSON.stringify(login)}`);
  return {
    username,
    password,
    token,
    character_id: character.id,
    auth: { username, token },
    character
  };
}

// ── Adult: safe name, custom name, clear ──────────────────────────────────────
const adult = await create_account('adult', true);
if (adult.character.is_adult !== true) fail('test adult was not created adult');

const initial_safe = adult.character.safe_display_name;
if (adult.character.display_name !== initial_safe) {
  fail('new character visible name should follow the safe name');
}

const safe_change = expect_ok(await post('updateCharacterProfile', {
  auth: adult.auth,
  character_id: adult.character_id,
  word1: w1[1],
  word2: w2[1]
}), 'adult safe-name change');
if (safe_change.safe_display_name === initial_safe) {
  fail('safe-name change did not change safe_display_name');
}
if (safe_change.display_name !== safe_change.safe_display_name) {
  fail('visible name should follow the safe name when no custom name is set');
}

const custom_name = `SmokeCustom${suffix}`;
const custom_set = expect_ok(await post('updateCharacterProfile', {
  auth: adult.auth,
  character_id: adult.character_id,
  display_name: custom_name
}), 'adult custom-name set');
if (custom_set.display_name !== custom_name) fail('custom name was not set');
if (custom_set.safe_display_name !== safe_change.safe_display_name) {
  fail('setting a custom name must not change the safe name');
}

const custom_cleared = expect_ok(await post('updateCharacterProfile', {
  auth: adult.auth,
  character_id: adult.character_id,
  clear_display_name: true
}), 'adult custom-name clear');
if (custom_cleared.display_name !== custom_cleared.safe_display_name) {
  fail('clearing the custom name should restore the safe name');
}

// A safe-name change under a custom name must not overwrite the custom name.
const custom_again = expect_ok(await post('updateCharacterProfile', {
  auth: adult.auth,
  character_id: adult.character_id,
  display_name: custom_name
}), 'adult custom-name set again');
const safe_under_custom = expect_ok(await post('updateCharacterProfile', {
  auth: adult.auth,
  character_id: adult.character_id,
  word1: w1[2],
  word2: w2[2]
}), 'adult safe-name change under custom name');
if (safe_under_custom.display_name !== custom_again.display_name) {
  fail('safe-name change should not overwrite an existing custom name');
}
if (safe_under_custom.safe_display_name === custom_again.safe_display_name) {
  fail('safe-name change under a custom name did not update the safe name');
}

const reverted = expect_ok(await post('updateCharacterProfile', {
  auth: adult.auth,
  character_id: adult.character_id,
  clear_display_name: true
}), 'adult final custom-name clear');
if (reverted.display_name !== reverted.safe_display_name) {
  fail('final clear did not restore the safe name');
}

// Validation: mutually exclusive fields and incomplete word pair.
const exclusive = await post('updateCharacterProfile', {
  auth: adult.auth,
  character_id: adult.character_id,
  display_name: 'Both',
  clear_display_name: true
});
if (exclusive.error !== 'display_name and clear_display_name are mutually exclusive') {
  fail(`mutual exclusion: ${JSON.stringify(exclusive)}`);
}

const one_word = await post('updateCharacterProfile', {
  auth: adult.auth,
  character_id: adult.character_id,
  word1: w1[0]
});
if (one_word.error !== 'word1 and word2 required to change safe name') {
  fail(`one-word validation: ${JSON.stringify(one_word)}`);
}

const fetched = expect_ok(await post('getCharacter', {
  auth: adult.auth,
  character_id: adult.character_id
}), 'getCharacter(adult)');
if (fetched.display_name !== reverted.display_name ||
    fetched.safe_display_name !== reverted.safe_display_name) {
  fail('getCharacter did not reflect the final adult names');
}

// ── Minor: custom name rejected, safe name allowed ───────────────────────────
const minor = await create_account('minor', false);
if (minor.character.is_adult !== false) fail('minor account should not be adult');

const denied = await post('updateCharacterProfile', {
  auth: minor.auth,
  character_id: minor.character_id,
  display_name: 'MinorCustom'
});
if (denied.error !== 'display_name can only be set if account is adult') {
  fail(`minor custom-name rejection: ${JSON.stringify(denied)}`);
}

const minor_safe = expect_ok(await post('updateCharacterProfile', {
  auth: minor.auth,
  character_id: minor.character_id,
  word1: w1[3],
  word2: w2[3]
}), 'minor safe-name change');
if (minor_safe.display_name !== minor_safe.safe_display_name) {
  fail('minor visible name should follow the safe name');
}
if (minor_safe.safe_display_name === minor.character.safe_display_name) {
  fail('minor safe-name change did not update the safe name');
}

console.log(`profile smoke: adult=${adult.character_id} minor=${minor.character_id}`);
console.log('Profile smoke checks passed');
