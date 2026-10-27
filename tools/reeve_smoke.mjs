#!/usr/bin/env node
/**
 * Reeve advisor smoke test.
 *
 * Exercises the full advisory loop against a running server:
 *   createAccount → login → build/seed a manor → getReeveReport
 *   → execute the emitted action → force an unaffordable state → assert a hold.
 *
 * It also verifies every text id referenced by the report exists in
 * game/text/en, and that no raw advisor English is returned.
 *
 * Usage: node tools/reeve_smoke.mjs --port 3399 --db test_run/smoke_3399/game.db
 */

import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';

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

async function safe_words(file) {
  return readFileSync(new URL(`../game/config/${file}`, import.meta.url), 'utf8')
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean);
}

function starting_resources() {
  const config = JSON.parse(
    readFileSync(new URL('../game/config/economy.json', import.meta.url), 'utf8')
  );
  return config.starting_resources ?? {};
}

function numeric(value) {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

function sql(statement) {
  if (!dbPath) throw new Error('--db is required for the seeded smoke flow');
  return execFileSync('sqlite3', [dbPath, statement], { encoding: 'utf8' }).trim();
}

function collect_text_ids(value, ids = new Set()) {
  if (Array.isArray(value)) {
    for (const entry of value) collect_text_ids(entry, ids);
    return ids;
  }
  if (value === null || typeof value !== 'object') return ids;
  const record = value;
  if (typeof record.text_id === 'string' && record.text_id) ids.add(record.text_id);
  if (record.args && typeof record.args === 'object') {
    for (const arg of Object.values(record.args)) {
      if (arg && typeof arg === 'object' && typeof arg.text_id === 'string') {
        ids.add(arg.text_id);
      }
    }
  }
  for (const [key, child] of Object.entries(record)) {
    if (key !== 'text_id') collect_text_ids(child, ids);
  }
  return ids;
}

function assert_text_ids(report) {
  for (const id of collect_text_ids(report)) {
    const file = new URL(`../game/text/en/${id}.txt`, import.meta.url);
    if (!existsSync(file)) {
      throw new Error(`missing text file for ${id}`);
    }
  }
}

function assert_no_raw_advice(report) {
  const raw = JSON.stringify(report);
  for (const phrase of ['would produce', 'only ran at', 'You imported', 'Your manor exported']) {
    if (raw.includes(phrase)) {
      throw new Error(`raw English advice leaked: ${phrase}`);
    }
  }
  if (Object.prototype.hasOwnProperty.call(report, 'recommendations')) {
    throw new Error('legacy recommendations field is still present');
  }
}

async function execute_action(report, auth, fiefdom_id, character_id) {
  const action = report.recommendation?.action;
  if (!action) return;
  if (action.kind === 'build') {
    const response = await post('Build', {
      auth, fiefdom_id, character_id,
      action: 'create',
      building_type: action.building_type,
      x: action.x,
      y: action.y
    });
    if (response.error) throw new Error(`recommended build failed: ${JSON.stringify(response)}`);
    return;
  }
  if (action.kind === 'upgrade' || action.kind === 'convert') {
    const response = await post('Build', {
      auth, fiefdom_id, character_id,
      action: action.kind,
      building_id: action.building_id
    });
    if (response.error) throw new Error(`recommended ${action.kind} failed: ${JSON.stringify(response)}`);
    return;
  }
  if (action.kind === 'build_road_link') {
    for (const cell of action.cells) {
      const response = await post('Build', {
        auth, fiefdom_id, character_id,
        action: 'create',
        building_type: 'road',
        x: cell.x,
        y: cell.y
      });
      if (response.error) throw new Error(`recommended road link failed: ${JSON.stringify(response)}`);
    }
    return;
  }
  if (action.kind === 'set_reserve') {
    const response = await post('setFiefdomReserve', {
      auth, fiefdom_id,
      resource: action.resource,
      reserve: action.value
    });
    if (response.error) throw new Error(`recommended reserve failed: ${JSON.stringify(response)}`);
    return;
  }
  if (action.kind === 'set_import') {
    const response = await post('setFiefdomImport', {
      auth, fiefdom_id,
      resource: action.resource,
      auto_import: action.enabled
    });
    if (response.error) throw new Error(`recommended import failed: ${JSON.stringify(response)}`);
    return;
  }
  if (action.kind === 'set_output_rate') {
    const response = await post('setBuildingOutputRate', {
      auth,
      building_id: action.building_id,
      output: action.output,
      rate: action.rate
    });
    if (response.error) throw new Error(`recommended output rate failed: ${JSON.stringify(response)}`);
  }
}

try {
  const w1 = await safe_words('safe_words_1.txt');
  const w2 = await safe_words('safe_words_2.txt');
  const username = `reeve_${Date.now().toString(36)}`;
  const password = 'smoke_pass_123';

  const acct = await post('createAccount', {
    username, password, adult: false, word1: w1[0], word2: w2[0]
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

  // New accounts do not have a fiefdom until the player finishes the land-patent
  // flow. Seed one directly so the Reeve smoke test exercises the manor economy.
  const existing_fiefdom = sql(
    `SELECT id FROM fiefdoms WHERE owner_id = ${character_id} LIMIT 1;`
  ).trim();
  if (!existing_fiefdom) {
    const starting = starting_resources();
    sql(
      `INSERT INTO fiefdoms ` +
      `(owner_id, x, y, gold, silver_pence, grain, wood, steel, bronze, stone, leather, mana, charcoal, iron, ironwork, fancy_ironwork, beams, boards, wall_count, morale, last_update_time, manor_level) ` +
      `VALUES (${character_id}, 0, 0, ${numeric(starting.gold)}, ${numeric(starting.silver_pence)}, ` +
      `${numeric(starting.grain)}, ${numeric(starting.wood)}, ${numeric(starting.steel)}, ` +
      `${numeric(starting.bronze)}, ${numeric(starting.stone)}, ${numeric(starting.leather)}, ` +
      `${numeric(starting.mana)}, ${numeric(starting.charcoal)}, ${numeric(starting.iron)}, ` +
      `${numeric(starting.ironwork)}, ${numeric(starting.fancy_ironwork)}, ${numeric(starting.beams)}, ` +
      `${numeric(starting.boards)}, 0, 0.0, strftime('%s','now'), 0);`
    );
  }

  const fiefdom = await post('getFiefdom', {
    auth, character_id, include_buildings: true
  });
  if (!fiefdom.data?.id) throw new Error(`no fiefdom: ${JSON.stringify(fiefdom)}`);
  const fiefdom_id = fiefdom.data.id;

  // Ensure a completed manor house. New accounts have no server-side manor
  // until the client auto-places one, so build it here (free) and complete it
  // directly to avoid a 60s construction wait.
  const home = await post('Build', {
    auth, fiefdom_id, character_id,
    action: 'create', building_type: 'home_base', x: 0, y: 0
  });
  if (home.error && !/home_base_exists/.test(home.error)) {
    throw new Error(`home_base build failed: ${JSON.stringify(home)}`);
  }
  sql(`UPDATE fiefdom_buildings SET level = 1, construction_start_ts = 0 WHERE fiefdom_id = ${fiefdom_id} AND name = 'home_base';`);
  sql(`UPDATE fiefdoms SET manor_level = 1 WHERE id = ${fiefdom_id};`);

  // Seed one grain-producing villein with its output switched off, so the
  // projection sees a real grain deficit without relying on construction time.
  sql(
    `INSERT INTO fiefdom_buildings ` +
    `(fiefdom_id, name, level, x, y, construction_start_ts, last_updated, action_start_ts, action_tag, pond_type, output_rates) ` +
    `VALUES (${fiefdom_id}, 'villein', 1, 8, 8, 0, strftime('%s','now'), 0, '', '', '{"grain":0}');`
  );
  sql(`UPDATE fiefdoms SET grain = 0 WHERE id = ${fiefdom_id};`);

  const first = await post('getReeveReport', { auth, character_id });
  if (!first.data) throw new Error(`getReeveReport failed: ${JSON.stringify(first)}`);
  if (first.data.advisor?.style_tag !== 'steady') {
    throw new Error(`unexpected style: ${JSON.stringify(first.data.advisor)}`);
  }
  if (!first.data.headline?.text_id?.startsWith('reeve_')) {
    throw new Error(`headline is not a text id: ${JSON.stringify(first.data.headline)}`);
  }
  const sectionIds = (first.data.sections ?? []).map((section) => section.id);
  for (const required of ['stores', 'production', 'land']) {
    if (!sectionIds.includes(required)) {
      throw new Error(`missing section ${required}: ${JSON.stringify(sectionIds)}`);
    }
  }
  const kind = first.data.recommendation?.kind;
  if (!['action', 'hold', 'none'].includes(kind)) {
    throw new Error(`unexpected recommendation kind: ${JSON.stringify(first.data.recommendation)}`);
  }
  assert_text_ids(first.data);
  assert_no_raw_advice(first.data);

  // Execute the recommendation when the Reeve offered an action.
  if (kind === 'action') {
    await execute_action(first.data, auth, fiefdom_id, character_id);
  }

  // Force an unsatisfiable state: every villein's grain output off, grain
  // stock empty, and no money to import. The preferred growth build is now
  // unaffordable, which must produce a hold rather than a cheaper fallback.
  sql(`UPDATE fiefdom_buildings SET output_rates = '{"grain":0}' WHERE fiefdom_id = ${fiefdom_id} AND name = 'villein';`);
  sql(`UPDATE fiefdoms SET grain = 0, gold = 0, silver_pence = 0 WHERE id = ${fiefdom_id};`);

  const held = await post('getReeveReport', { auth, character_id });
  if (!held.data) throw new Error(`second getReeveReport failed: ${JSON.stringify(held)}`);
  assert_text_ids(held.data);
  assert_no_raw_advice(held.data);
  if (held.data.recommendation?.kind !== 'hold') {
    throw new Error(`expected hold, got ${JSON.stringify(held.data.recommendation)}`);
  }
  if (!held.data.recommendation.reason_text_id?.startsWith('reeve_error_')) {
    throw new Error(`hold missing reason text id: ${JSON.stringify(held.data.recommendation)}`);
  }

  console.log(
    `reeve_smoke PASSED (sections=${sectionIds.join(',')} first=${kind} ` +
    `hold=${held.data.recommendation.reason_text_id})`
  );
} catch (error) {
  console.error(`reeve_smoke FAILED: ${error.message}`);
  process.exit(2);
}
