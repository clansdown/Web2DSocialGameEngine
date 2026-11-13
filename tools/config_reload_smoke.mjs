#!/usr/bin/env node
/**
 * Production-safe config reload smoke test.
 *
 * Runs against a server started by tools/config_reload_smoke.sh with a
 * writable copy of game/config. Exercises:
 *   - Wolf Marche level 1 uses the authored spawn schedule (only dire rats)
 *   - getGameInfo includes the tower-defense configs (getAllConfigs key fix)
 *   - a changed building config is served after SIGHUP
 *   - a malformed building config aborts the reload and keeps the old value
 *   - a changed dynamic spawn schedule is served after SIGHUP
 *   - a malformed spawn schedule keeps the previous schedule active
 *   - an in-place combat map edit is reloaded
 *
 * Usage: node tools/config_reload_smoke.mjs --port N --pid SERVER_PID --content DIR
 */
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const args = process.argv.slice(2);
function arg(name, fallback) {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : fallback;
}
const port = parseInt(arg('--port', '2290'), 10);
const serverPid = parseInt(arg('--pid', '0'), 10);
const contentDir = arg('--content', '');
const logPath = arg('--log', '');
const base = `http://127.0.0.1:${port}`;

if (!contentDir || !serverPid || !logPath) {
  throw new Error('--content, --pid, and --log are required');
}

async function post(path, body) {
  const r = await fetch(`${base}/api/${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  return r.json();
}

function read_json(rel) {
  return JSON.parse(readFileSync(join(contentDir, 'config', rel), 'utf8'));
}
function write_text(rel, text) {
  writeFileSync(join(contentDir, 'config', rel), text);
}
function write_json(rel, data) {
  writeFileSync(join(contentDir, 'config', rel), JSON.stringify(data, null, 4));
}
function send_hup() {
  process.kill(serverPid, 'SIGHUP');
}
function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function marker_count(pattern) {
  const text = readFileSync(logPath, 'utf8');
  return (text.match(pattern) || []).length;
}

async function wait_for_marker(pattern, label, timeoutMs = 12000) {
  const before = marker_count(pattern);
  send_hup();
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (marker_count(pattern) > before) return;
    await sleep(100);
  }
  throw new Error(`${label}: reload marker not observed`);
}

function wait_for_reload() {
  return wait_for_marker(/\[GameConfigCache\] Reload complete:/g, 'reload');
}
function wait_for_abort() {
  return wait_for_marker(/\[GameConfigCache\] Reload aborted:/g, 'abort');
}

async function wait_for(label, fn, predicate, timeoutMs = 12000) {
  const deadline = Date.now() + timeoutMs;
  let last;
  while (Date.now() < deadline) {
    last = await fn();
    if (predicate(last)) return last;
    await sleep(250);
  }
  throw new Error(`${label} did not converge; last=${JSON.stringify(last)}`);
}

async function create_account(prefix) {
  const words1 = readFileSync(join(contentDir, 'config', 'safe_words_1.txt'), 'utf8')
    .split('\n').map((s) => s.trim()).filter(Boolean);
  const words2 = readFileSync(join(contentDir, 'config', 'safe_words_2.txt'), 'utf8')
    .split('\n').map((s) => s.trim()).filter(Boolean);
  const username = `${prefix}_${Date.now().toString(36)}_${Math.floor(Math.random() * 1e6)}`;
  const password = 'smoke_pass_123';
  const acct = await post('createAccount', {
    username,
    password,
    word1: words1[0],
    word2: words2[0]
  });
  if (!acct.data && !acct.token) {
    throw new Error(`createAccount failed: ${JSON.stringify(acct)}`);
  }
  const login = await post('login', { auth: { username, password } });
  const token = login.data?.token ?? login.token;
  const character = login.data?.characters?.[0];
  if (!token || !character) {
    throw new Error(`login failed: ${JSON.stringify(login)}`);
  }
  return { auth: { username, token }, character_id: character.id };
}

async function td_round(account) {
  return post('tdRound', {
    auth: account.auth,
    character_id: account.character_id,
    mini_game: 'tower_defense'
  });
}

function assert_dire_rats_only(round) {
  const schedule = round.data?.spawn_schedule;
  if (!Array.isArray(schedule) || schedule.length === 0) {
    throw new Error(`no spawn schedule: ${JSON.stringify(round)}`);
  }
  const ids = new Set();
  for (const entry of schedule) {
    ids.add(entry.enemy_id);
    if (entry.enemy_id !== 'dire_rat') {
      throw new Error(`non-dire-rat spawn: ${JSON.stringify(entry)}`);
    }
  }
  return schedule.map((e) => e.count);
}

try {
  const account = await create_account('cfg_reload');

  // 1. Dynamic schedule is loaded (this used to fall back to wave templates).
  const first = await td_round(account);
  if (first.data?.display_name_key !== 'campaign_wolf_marche') {
    throw new Error(`wrong schedule: ${JSON.stringify(first)}`);
  }
  const counts = assert_dire_rats_only(first);
  if (JSON.stringify(counts) !== JSON.stringify([16, 24, 20])) {
    throw new Error(`unexpected round-1 counts: ${JSON.stringify(counts)}`);
  }

  // 2. getGameInfo now carries the tower-defense configs.
  const info = await post('getGameInfo', { auth: account.auth });
  const mobs = info.data?.configs?.tower_defense_mobs;
  if (!mobs || !Array.isArray(mobs.mobs) || mobs.mobs.length === 0) {
    throw new Error(`getGameInfo missing tower_defense_mobs: ${JSON.stringify(info).slice(0, 400)}`);
  }

  // 3. A changed building config is served after SIGHUP.
  const buildingsRel = 'fiefdom_building_types.json';
  const pristineBuildings = read_json(buildingsRel);
  const originalBuildings = JSON.parse(JSON.stringify(pristineBuildings));
  const villein = originalBuildings.find((entry) => entry.villein)?.villein;
  if (!villein || villein.arable_acres !== 15) {
    throw new Error(`unexpected villein config: ${JSON.stringify(villein)}`);
  }
  villein.arable_acres = 16;
  write_json(buildingsRel, originalBuildings);
  await wait_for_reload();

  const building_configs = () =>
    post('getBuildingConfigs', { auth: account.auth, character_id: account.character_id });
  await wait_for(
    'building config reload',
    building_configs,
    (r) => r.data?.villein?.arable_acres === 16
  );

  // 4. A malformed config aborts the whole reload.
  write_text(buildingsRel, '{ this is not json');
  await wait_for_abort();
  await sleep(200);
  const after_bad_building = await building_configs();
  if (after_bad_building.data?.villein?.arable_acres !== 16) {
    throw new Error(
      `malformed config did not keep the previous state: ${JSON.stringify(after_bad_building).slice(0, 400)}`
    );
  }
  write_json(buildingsRel, pristineBuildings);
  await wait_for_reload();
  await wait_for(
    'building config restore',
    building_configs,
    (r) => r.data?.villein?.arable_acres === 15
  );

  // 5. A changed dynamic spawn schedule is served after SIGHUP.
  const scheduleRel = 'tower_defense/spawn_schedules/wolf_marche.json';
  const pristineSchedule = read_json(scheduleRel);
  const originalSchedule = JSON.parse(JSON.stringify(pristineSchedule));
  originalSchedule.levels['1'].rounds[0][0].count = 7;
  write_json(scheduleRel, originalSchedule);
  await wait_for_reload();

  const account_after = await create_account('cfg_reload_after');
  const changed = await wait_for(
    'spawn schedule reload',
    () => td_round(account_after),
    (r) => r.data?.spawn_schedule?.[0]?.count === 7
  );
  assert_dire_rats_only(changed);

  // 6. A malformed spawn schedule keeps the previous schedule active.
  write_text(scheduleRel, '{ broken');
  await wait_for_abort();
  await sleep(200);
  const account_bad = await create_account('cfg_reload_bad');
  const still_old = await td_round(account_bad);
  if (still_old.data?.spawn_schedule?.[0]?.count !== 7) {
    throw new Error(
      `malformed schedule did not keep the previous state: ${JSON.stringify(still_old).slice(0, 400)}`
    );
  }
  write_json(scheduleRel, pristineSchedule);
  await wait_for_reload();

  // 7. An in-place combat map edit is reloaded.
  const combatMapRel = 'combat/maps/meadow.json';
  const pristineCombatMap = read_json(combatMapRel);
  const originalCombatMap = JSON.parse(JSON.stringify(pristineCombatMap));
  originalCombatMap.name = 'meadow_reloaded';
  write_json(combatMapRel, originalCombatMap);
  await wait_for_reload();

  const combat_configs = () => post('combatGetConfigs', { auth: account.auth });
  const reloaded = await wait_for(
    'combat map reload',
    combat_configs,
    (r) => (r.data?.maps ?? []).some((m) => m.file === 'meadow.json' && m.id === 'meadow_reloaded')
  );
  if (!reloaded.data?.maps?.some((m) => m.file === 'meadow.json' && m.id === 'meadow_reloaded')) {
    throw new Error('combat map was not reloaded');
  }

  write_json(combatMapRel, pristineCombatMap);

  console.log('config_reload_smoke PASSED');
  process.exit(0);
} catch (e) {
  console.error(`config_reload_smoke FAILED: ${e.message}`);
  process.exit(2);
}
