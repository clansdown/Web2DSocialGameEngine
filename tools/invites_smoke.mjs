#!/usr/bin/env node
/**
 * Invitation/registration smoke scenarios.
 *
 * The shell harness starts a server per scenario against one scratch DB:
 *   off -> invite_only -> global_code, then a rate-limit scenario.
 *
 * Usage: node tools/invites_smoke.mjs --port 3397 --db-dir DIR --state FILE \
 *        --scenario off|invite_only|global|rate --global-code CODE
 */
import { execFileSync } from 'node:child_process';
import {
  existsSync,
  readFileSync,
  writeFileSync
} from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
function arg(name, fallback) {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : fallback;
}

const port = parseInt(arg('--port', '2290'), 10);
const db_dir = arg('--db-dir', '');
const state_path = arg('--state', '');
const scenario = arg('--scenario', '');
const global_code = arg('--global-code', 'SMOKE-GLOBAL-9Q7X');
const base = `http://127.0.0.1:${port}`;
const staff_admin = fileURLToPath(new URL('../server/build/staff_admin', import.meta.url));

if (!db_dir || !state_path || !scenario) {
  console.error('invites smoke: --db-dir, --state, and --scenario are required');
  process.exit(2);
}

let create_counter = 0;

async function post(pathname, body, headers = {}) {
  const response = await fetch(`${base}/api/${pathname}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...headers },
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
  console.error(`INVITES SMOKE FAILED (${scenario}):`, message);
  process.exit(2);
}

function expect(condition, message) {
  if (!condition) fail(message);
}

function safe_words(file) {
  return readFileSync(new URL(`../game/config/${file}`, import.meta.url), 'utf8')
    .split('\n')
    .map((s) => s.trim())
    .filter(Boolean);
}

const words1 = safe_words('safe_words_1.txt');
const words2 = safe_words('safe_words_2.txt');

function load_state() {
  if (!existsSync(state_path)) fail(`state file missing: ${state_path}`);
  return JSON.parse(readFileSync(state_path, 'utf8'));
}

function save_state(state) {
  writeFileSync(state_path, JSON.stringify(state, null, 2));
}

function account_body(username, invitation_code) {
  return {
    username,
    password: 'invite_smoke_pass_123',
    adult: false,
    word1: words1[0],
    word2: words2[0],
    ...(invitation_code ? { invitation_code } : {})
  };
}

async function attempt_create(prefix, invitation_code) {
  const username = `${prefix}_${Date.now().toString(36)}_${create_counter++}`;
  return post('createAccount', account_body(username, invitation_code));
}

async function create(prefix, suffix, invitation_code) {
  const username = `${prefix}_${suffix}_${create_counter++}`;
  const created = await post('createAccount', account_body(username, invitation_code));
  if (created.error || !created.data?.token) {
    fail(`createAccount ${prefix}: ${JSON.stringify(created)}`);
  }
  return {
    username,
    password: 'invite_smoke_pass_123',
    character_id: created.data.characters?.[0]?.id,
    created
  };
}

async function login(account) {
  const result = await post('login', {
    auth: { username: account.username, password: account.password }
  });
  const token = result.data?.token;
  const character_id = result.data?.characters?.[0]?.id;
  if (!token || !character_id) {
    fail(`login ${account.username}: ${JSON.stringify(result)}`);
  }
  return {
    username: account.username,
    password: account.password,
    character_id,
    token,
    auth: { username: account.username, token }
  };
}

async function scenario_off() {
  const config = data(await post('getRegistrationConfig', {}));
  expect(config.mode === 'off', `expected off, got ${JSON.stringify(config)}`);
  expect(config.invite_required === false, 'off must not require a code');

  const created = await create('staff', Date.now().toString(36));
  expect(created.created.data?.token, 'off account creation failed');
  // `off` must ignore any submitted code rather than consuming/validating it.
  const player = await create('player', Date.now().toString(36), 'BOGUS-CODE-XYZ');

  execFileSync(
    staff_admin,
    ['--db-dir', db_dir, 'grant', created.username, 'superadmin'],
    { stdio: 'pipe' }
  );

  expect(
    !existsSync(path.join(db_dir, 'invites.db')),
    'off mode must not create invites.db'
  );

  save_state({
    suffix: Date.now().toString(36),
    staff: { username: created.username, password: created.password },
    player: {
      username: player.username,
      password: player.password,
      character_id: player.character_id
    }
  });
  console.log('off: account creation works and invites.db is absent');
}

async function scenario_invite_only() {
  const state = load_state();
  const staff = await login(state.staff);
  const player = await login(state.player);

  const config = data(await post('getRegistrationConfig', {}));
  expect(config.mode === 'invite_only', `expected invite_only, got ${JSON.stringify(config)}`);
  expect(config.invite_required === true, 'invite_only must require a code');

  const no_code = await attempt_create('nocode');
  expect(
    no_code.error === 'invitation_code_required',
    `missing code should be rejected, got ${JSON.stringify(no_code)}`
  );

  const context = data(await post('staffGetContext', { auth: staff.auth }));
  expect(context.invitations_enabled === true, 'staff context invitations_enabled false');
  expect(
    context.capabilities?.includes('registration.invites.generate'),
    'staff lacks registration.invites.generate'
  );
  expect(
    context.capabilities?.includes('registration.invites.revoke'),
    'staff lacks registration.invites.revoke'
  );

  const staff_code = data(await post('staffCreateInvitationCode', {
    auth: staff.auth,
    reason: 'smoke invite'
  }));
  expect(staff_code.code, `staff code generation failed: ${JSON.stringify(staff_code)}`);

  const invited = await create('invited', state.suffix, staff_code.code);
  expect(invited.created.data?.token, 'staff-generated code did not create an account');

  const reuse = await attempt_create('reuse', staff_code.code);
  expect(
    reuse.error === 'invalid_invitation_code',
    `reusing a code should fail, got ${JSON.stringify(reuse)}`
  );
  console.log('invite_only: staff code is single-use');

  const status = data(await post('getInvitationStatus', { auth: player.auth }));
  expect(status.enabled === true, 'player invitations not enabled');
  expect(status.can_generate === true, 'player cannot generate despite allowance');
  expect(status.daily_limit === 2 && status.lifetime_limit === 5, 'unexpected player limits');
  expect(status.codes?.length === 0, 'new player should have no codes');

  const p1 = data(await post('createInvitationCode', { auth: player.auth }));
  const p2 = data(await post('createInvitationCode', { auth: player.auth }));
  expect(p1.code && p2.code, 'player code generation failed');
  const p3 = await post('createInvitationCode', { auth: player.auth });
  expect(
    p3.error === 'invite_daily_limit_reached',
    `daily limit not enforced: ${JSON.stringify(p3)}`
  );

  const player_invited = await create('playercode', state.suffix, p1.code);
  expect(player_invited.created.data?.token, 'player-generated code did not work');

  const player_state = data(await post('getPlayerState', {
    auth: player.auth,
    character_id: player.character_id
  }));
  expect(player_state.invitations?.enabled === true, 'getPlayerState invitations missing');
  expect(
    player_state.invitations?.daily_remaining === 0,
    `expected daily_remaining 0, got ${JSON.stringify(player_state.invitations)}`
  );

  const revoke = data(await post('staffRevokeInvitationCode', {
    auth: staff.auth,
    code_id: p2.invitation.id,
    reason: 'smoke revoke'
  }));
  expect(revoke.action_id, `revoke failed: ${JSON.stringify(revoke)}`);
  const preflight = data(await post('staffPreflightUndo', {
    auth: staff.auth,
    action_id: revoke.action_id
  }));
  expect(preflight.status === 'available', `revoke undo unavailable: ${JSON.stringify(preflight)}`);

  const undo = data(await post('staffUndo', {
    auth: staff.auth,
    action_id: revoke.action_id,
    reason: 'smoke undo revoke'
  }));
  expect(undo.code_id === p2.invitation.id, `staffUndo returned ${JSON.stringify(undo)}`);
  const after_undo = data(await post('staffListInvitationCodes', {
    auth: staff.auth,
    query: p2.invitation.id
  }));
  expect(
    after_undo.codes?.length === 1 && after_undo.codes[0].revoked === false,
    `revoke undo did not clear revoked_at: ${JSON.stringify(after_undo)}`
  );
  console.log('invite_only: player limits + staff revoke/undo');

  const undo_code = data(await post('staffCreateInvitationCode', {
    auth: staff.auth,
    reason: 'smoke undo generate'
  }));
  const generate_action = undo_code.action_id;
  const pre_generate = data(await post('staffPreflightUndo', {
    auth: staff.auth,
    action_id: generate_action
  }));
  expect(
    pre_generate.status === 'available',
    `generate undo unavailable: ${JSON.stringify(pre_generate)}`
  );
  await post('staffUndo', {
    auth: staff.auth,
    action_id: generate_action,
    reason: 'smoke undo generate'
  });
  const gone = data(await post('staffListInvitationCodes', {
    auth: staff.auth,
    query: undo_code.invitation.id
  }));
  expect(gone.codes?.length === 0, 'undo generate did not delete the unused code');

  const journal = data(await post('staffListJournal', {
    auth: staff.auth,
    action_type: 'registration.invite_generate',
    limit: 100
  }));
  const journal_blob = JSON.stringify(journal);
  expect(
    !journal_blob.includes(staff_code.code.replace(/-/g, '')) &&
      !journal_blob.includes(staff_code.code),
    'plaintext invite code leaked into the staff journal'
  );
  console.log('invite_only: journal/undo contain no plaintext code');

  const global_individual = data(await post('staffCreateInvitationCode', {
    auth: staff.auth,
    reason: 'global compatibility test'
  }));
  expect(global_individual.code, 'failed to create global-compat code');
  state.global_individual_code = global_individual.code;
  save_state(state);
}

async function scenario_global() {
  const state = load_state();
  const staff = await login(state.staff);
  const player = await login(state.player);
  expect(state.global_individual_code, 'missing individual code from invite_only scenario');

  const config = data(await post('getRegistrationConfig', {}));
  expect(config.mode === 'global_code', `expected global_code, got ${JSON.stringify(config)}`);
  expect(config.invite_required === true, 'global_code must require a code');

  const wrong = await attempt_create('wrongglobal', 'NOT-THE-CODE');
  expect(
    wrong.error === 'invalid_invitation_code',
    `wrong global code should fail, got ${JSON.stringify(wrong)}`
  );

  const via_global = await create('viaglobal', state.suffix, global_code);
  expect(via_global.created.data?.token, 'global code did not create an account');

  const via_individual = await create(
    'viaindividual',
    state.suffix,
    state.global_individual_code
  );
  expect(
    via_individual.created.data?.token,
    'global_code mode rejected a still-valid individual code'
  );

  const staff_generate = await post('staffCreateInvitationCode', {
    auth: staff.auth,
    reason: 'should be disabled'
  });
  expect(
    staff_generate.error === 'invite_generation_disabled',
    `staff generation should be disabled in global_code: ${JSON.stringify(staff_generate)}`
  );

  const player_generate = await post('createInvitationCode', { auth: player.auth });
  expect(
    player_generate.error === 'invite_generation_disabled',
    `player generation should be disabled in global_code: ${JSON.stringify(player_generate)}`
  );
  const player_state = data(await post('getPlayerState', {
    auth: player.auth,
    character_id: player.character_id
  }));
  expect(
    player_state.invitations?.enabled === false,
    `hub invitations should be hidden in global_code: ${JSON.stringify(player_state.invitations)}`
  );

  const game_info = await post('getGameInfo', { auth: staff.auth });
  expect(
    !JSON.stringify(game_info).includes(global_code),
    'getGameInfo leaked the global registration code'
  );
  expect(
    !Object.prototype.hasOwnProperty.call(data(game_info), 'registration'),
    'getGameInfo exposed the registration config object'
  );
  console.log('global_code: shared + individual codes work; config is not exposed');
}

async function scenario_rate() {
  async function attempt(ip) {
    return post('createAccount', {}, { 'X-Forwarded-For': ip });
  }

  const a = await attempt('203.0.113.1');
  expect(a.error !== 'rate_limited', `first attempt was rate limited: ${JSON.stringify(a)}`);
  const b = await attempt('203.0.113.1');
  expect(
    b.error === 'rate_limited',
    `same network second attempt should be rate limited: ${JSON.stringify(b)}`
  );
  expect(
    typeof data(b).retry_after_seconds === 'number',
    'rate_limited did not include retry_after_seconds'
  );

  const c = await attempt('203.0.113.2');
  expect(c.error !== 'rate_limited', 'second network should be allowed');
  const d = await attempt('203.0.113.3');
  expect(d.error !== 'rate_limited', 'third network should be allowed');

  const e = await attempt('203.0.113.1');
  expect(
    e.error !== 'rate_limited',
    'least-recently-used network should have been evicted from max_entries=2'
  );
  console.log('rate: fixed window + LRU eviction verified');
}

if (scenario === 'off') {
  await scenario_off();
} else if (scenario === 'invite_only') {
  await scenario_invite_only();
} else if (scenario === 'global') {
  await scenario_global();
} else if (scenario === 'rate') {
  await scenario_rate();
} else {
  fail(`unknown scenario ${scenario}`);
}
