#!/usr/bin/env node
/**
 * Translation smoke test — verifies the chat translation seam against a running
 * server backed by the passthrough sidecar (see tools/translation_smoke.sh).
 *
 * Flow: createAccount ×2 → login ×2 → openDirectChat → send (source "en") →
 * requestTranslations(target "fr") → assert a stored translation is returned.
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

const w1 = await safe_words('safe_words_1.txt');
const w2 = await safe_words('safe_words_2.txt');

async function make_user(tag, language) {
  const username = `${tag}_${Date.now().toString(36)}_${Math.floor(Math.random() * 1e6)}`;
  const password = 'trans_pass_123';
  const body = {
    username, password, adult: false, word1: w1[0], word2: w2[0]
  };
  if (language) body.language = language;
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

const sent = data(await post('sendChatMessage', {
  auth: a.auth, character_id: a.character_id, chat_id, body: 'hello world', language: 'en'
}));
if (!sent.id) fail(`sendChatMessage: ${JSON.stringify(sent)}`);

const translated = data(await post('requestTranslations', {
  auth: a.auth, character_id: a.character_id, message_ids: [sent.id], lang: 'fr'
}));
const entry = translated.translations?.[sent.id];
if (!entry || !entry.body) {
  fail(`requestTranslations produced no translation: ${JSON.stringify(translated)}`);
}
if (entry.body !== 'fr:hello world') {
  fail(`unexpected translation body: ${JSON.stringify(entry)}`);
}

// A second request must be served from the stored library translation.
const again = data(await post('requestTranslations', {
  auth: a.auth, character_id: a.character_id, message_ids: [sent.id], lang: 'fr'
}));
if (!again.translations?.[sent.id]?.body) fail('stored translation not returned on replay');

console.log(`translation produced and stored: ${entry.body}`);
console.log('Translation smoke PASSED');
process.exit(0);
