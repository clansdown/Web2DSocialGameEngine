#!/usr/bin/env node
/**
 * PvE mission offerings smoke test.
 *
 * Exercises the auto-generated offering flow against a running server:
 *   createAccount → login → getCombatOfferings → combatCreate(offering_id)
 * and asserts the offering round-trips (id echoed, danger band present) and
 * that a malformed id is rejected.
 *
 * Usage: node tools/offerings_smoke.mjs --port 3391
 * (see tools/offerings_smoke.sh for the server-harness wrapper)
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

try {
  const w1 = await safe_words('safe_words_1.txt');
  const w2 = await safe_words('safe_words_2.txt');
  const username = `offer_${Date.now().toString(36)}`;
  const password = 'smoke_pass_123';

  const acct = await post('createAccount', {
    username,
    password,
    adult: false,
    word1: w1[0],
    word2: w2[0]
  });
  if (!acct.data && !acct.token) {
    throw new Error(`createAccount failed: ${JSON.stringify(acct)}`);
  }

  const login = await post('login', { auth: { username, password } });
  const token = login.data?.token ?? login.token;
  if (!token) throw new Error(`login failed: ${JSON.stringify(login)}`);
  const character = login.data?.characters?.[0];
  if (!character) throw new Error(`no characters: ${JSON.stringify(login)}`);
  const character_id = character.id;
  const auth = { username, token };

  const board = await post('getCombatOfferings', { auth, character_id });
  const tiers = board.data?.tiers ?? [];
  const offerings = board.data?.offerings ?? [];
  const tier_ids = tiers.map((t) => t.id);
  if (!tier_ids.includes('solo')) {
    throw new Error(`no solo tier: ${JSON.stringify(board)}`);
  }
  const solo = offerings.filter((o) => o.tier === 'solo');
  if (solo.length === 0) {
    throw new Error(`no solo offerings: ${JSON.stringify(board)}`);
  }
  // A new fiefdom is not in a barony, so only the solo tier is offered.
  if (offerings.some((o) => o.tier !== 'solo')) {
    throw new Error(`non-solo tier without a barony: ${JSON.stringify(board)}`);
  }
  const offering = solo[0];
  if (!offering.id || !offering.danger_band || typeof offering.xp_base !== 'number') {
    throw new Error(`malformed offering: ${JSON.stringify(offering)}`);
  }

  const created = await post('combatCreate', {
    auth,
    character_id,
    mode: 'pve',
    offering_id: offering.id
  });
  if (!created.data?.match_id) {
    throw new Error(`combatCreate failed: ${JSON.stringify(created)}`);
  }
  if (created.data.offering_id !== offering.id) {
    throw new Error(`offering_id not echoed: ${JSON.stringify(created)}`);
  }
  if (!created.data.danger_band) {
    throw new Error(`no danger_band in create response: ${JSON.stringify(created)}`);
  }

  // A malformed offering id is rejected.
  const bad = await post('combatCreate', {
    auth,
    character_id,
    mode: 'pve',
    offering_id: 'not-a-real-offering'
  });
  if (!bad.error) {
    throw new Error(`malformed offering accepted: ${JSON.stringify(bad)}`);
  }

  console.log(
    `offerings_smoke PASSED (tiers=${tier_ids.join(',')} solo=${solo.length} ` +
      `picked=${offering.id} danger=${offering.danger_band} xp=${offering.xp_base})`
  );
} catch (e) {
  console.error(`offerings_smoke FAILED: ${e.message}`);
  process.exit(2);
}
