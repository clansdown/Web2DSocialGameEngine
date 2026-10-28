#!/usr/bin/env node
/**
 * Retinue name-localization smoke test.
 *
 * Verifies that recruit names come from the account-language pool, that each
 * offer carries name_language, that hire validation uses that pool, that
 * existing hired names stay fixed after a language change, and that a missing
 * language pool falls back to English.
 *
 * Usage: node tools/retinue_names_smoke.mjs --port 3401 --db <game.db>
 */

import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const portIdx = args.indexOf('--port');
const dbIdx = args.indexOf('--db');
const port = portIdx >= 0 ? parseInt(args[portIdx + 1], 10) : 2290;
const dbPath = dbIdx >= 0 ? args[dbIdx + 1] : '';
const base = `http://127.0.0.1:${port}`;

async function post(path, body) {
  const response = await fetch(`${base}/api/${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  return response.json();
}

function data(response) {
  if (response.error) {
    throw new Error(`API error: ${JSON.stringify(response)}`);
  }
  if (!response.data) {
    throw new Error(`missing response data: ${JSON.stringify(response)}`);
  }
  return response.data;
}

function safe_words(file) {
  return readFileSync(new URL(`../game/config/${file}`, import.meta.url), 'utf8')
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean);
}

function sql(statement) {
  if (!dbPath) throw new Error('--db is required');
  return execFileSync('sqlite3', [dbPath, statement], { encoding: 'utf8' }).trim();
}

function assert_offer_language(candidates, language, prefix, label) {
  if (!Array.isArray(candidates) || candidates.length === 0) {
    throw new Error(`${label}: no candidates returned`);
  }
  for (const candidate of candidates) {
    if (candidate.name_language !== language) {
      throw new Error(
        `${label}: expected name_language=${language}, got ` +
        `${JSON.stringify(candidate)}`
      );
    }
    if (!candidate.display_name.startsWith(prefix)) {
      throw new Error(
        `${label}: expected name prefix ${JSON.stringify(prefix)}, got ` +
        `${JSON.stringify(candidate.display_name)}`
      );
    }
  }
}

function find_member(retinue, member_id) {
  return (retinue.members ?? []).find((member) => member.id === member_id);
}

try {
  const w1 = safe_words('safe_words_1.txt');
  const w2 = safe_words('safe_words_2.txt');
  const username = `retinue_names_${Date.now().toString(36)}`;
  const password = 'smoke_pass_123';

  const account = await post('createAccount', {
    username,
    password,
    adult: false,
    language: 'es',
    word1: w1[0],
    word2: w2[0]
  });
  if (!account.data && !account.token) {
    throw new Error(`createAccount failed: ${JSON.stringify(account)}`);
  }

  const login = data(await post('login', { auth: { username, password } }));
  const token = login.token;
  const character = login.characters?.[0];
  if (!token || !character) {
    throw new Error(`login failed: ${JSON.stringify(login)}`);
  }
  const character_id = character.id;
  const auth = { username, token };

  // New accounts have no fiefdom until the land-patent flow. Seed one with a
  // manor level that unlocks the archer/spearman market and ample resources so
  // the hire fee is never the reason this smoke fails.
  sql(
    `INSERT INTO fiefdoms ` +
    `(owner_id, x, y, gold, silver_pence, grain, wood, steel, bronze, leather, mana, ` +
    `charcoal, iron, ironwork, fancy_ironwork, beams, boards, wall_count, morale, ` +
    `last_update_time, manor_level) ` +
    `VALUES (${character_id}, 0, 0, 100000, 10000000, 10000, 10000, 10000, 10000, ` +
    `10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 0, 0.0, ` +
    `strftime('%s','now'), 1);`
  );

  const first = data(await post('listRecruitCandidates', { auth, character_id }));
  assert_offer_language(first.candidates, 'es', 'ES ', 'initial es market');

  const offer = first.candidates[0];
  const hire = data(await post('hireRecruit', {
    auth,
    character_id,
    unit_class: offer.unit_class,
    display_name: offer.display_name,
    name_language: offer.name_language,
    gender: offer.gender,
    headshot_id: offer.headshot_id,
    level: offer.level,
    hour_bucket: offer.hour_bucket
  }));
  const hired = hire.member;
  if (!hired?.display_name?.startsWith('ES ')) {
    throw new Error(`hired name is not Spanish: ${JSON.stringify(hired)}`);
  }

  const retinue = data(await post('getRetinue', { auth, character_id }));
  const stored = find_member(retinue, hired.id);
  if (!stored || stored.display_name !== hired.display_name) {
    throw new Error(`hired name did not persist: ${JSON.stringify(stored)}`);
  }

  data(await post('updateUserProfile', { auth, language: 'en' }));
  const english = data(await post('listRecruitCandidates', { auth, character_id }));
  assert_offer_language(english.candidates, 'en', '', 'english market');
  for (const candidate of english.candidates) {
    if (candidate.display_name.startsWith('ES ')) {
      throw new Error(
        `english market leaked Spanish name: ${JSON.stringify(candidate)}`
      );
    }
  }

  const retinue_after = data(await post('getRetinue', { auth, character_id }));
  const stored_after = find_member(retinue_after, hired.id);
  if (!stored_after || stored_after.display_name !== hired.display_name) {
    throw new Error(
      `existing hired name changed after language switch: ` +
      `${JSON.stringify(stored_after)}`
    );
  }

  data(await post('updateUserProfile', { auth, language: 'xx' }));
  const fallback = data(await post('listRecruitCandidates', { auth, character_id }));
  assert_offer_language(fallback.candidates, 'en', '', 'missing-language fallback');
  for (const candidate of fallback.candidates) {
    if (candidate.display_name.startsWith('ES ')) {
      throw new Error(
        `missing-language fallback leaked Spanish name: ` +
        `${JSON.stringify(candidate)}`
      );
    }
  }

  console.log(
    `retinue_names_smoke PASSED (offers=${first.candidates.length}, ` +
    `hired=${hired.display_name}, fallback=${fallback.candidates.length})`
  );
} catch (error) {
  console.error(`retinue_names_smoke FAILED: ${error.message}`);
  process.exit(2);
}
