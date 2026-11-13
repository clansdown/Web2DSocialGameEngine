#!/usr/bin/env node
/**
 * Concurrent config-read/reload stress driver.
 *
 * Hammers config-reading endpoints from several concurrent clients while the
 * parent process sends SIGHUP every 100 ms. Intended to run against a
 * ThreadSanitizer build (see tools/config_reload_stress.sh); any race in the
 * snapshot swap or map caches is reported by TSan.
 *
 * Usage: node tools/config_reload_stress.mjs --port N --pid PID --content DIR [--seconds 6]
 */
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const args = process.argv.slice(2);
function arg(name, fallback) {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : fallback;
}
const port = parseInt(arg('--port', '2290'), 10);
const serverPid = parseInt(arg('--pid', '0'), 10);
const contentDir = arg('--content', '');
const seconds = parseInt(arg('--seconds', '6'), 10);
const base = `http://127.0.0.1:${port}`;

if (!contentDir || !serverPid) throw new Error('--content and --pid are required');

async function post(path, body) {
  const r = await fetch(`${base}/api/${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  return r.json();
}
function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

const words1 = readFileSync(join(contentDir, 'config', 'safe_words_1.txt'), 'utf8')
  .split('\n').map((s) => s.trim()).filter(Boolean);
const words2 = readFileSync(join(contentDir, 'config', 'safe_words_2.txt'), 'utf8')
  .split('\n').map((s) => s.trim()).filter(Boolean);

const username = `cfg_stress_${Date.now().toString(36)}`;
const password = 'smoke_pass_123';
const acct = await post('createAccount', {
  username, password, word1: words1[0], word2: words2[0]
});
if (!acct.data && !acct.token) throw new Error(`createAccount failed: ${JSON.stringify(acct)}`);
const login = await post('login', { auth: { username, password } });
const token = login.data?.token ?? login.token;
const character = login.data?.characters?.[0];
if (!token || !character) throw new Error(`login failed: ${JSON.stringify(login)}`);
const auth = { username, token };
const character_id = character.id;

const deadline = Date.now() + seconds * 1000;
let failures = 0;

async function worker(id) {
  while (Date.now() < deadline) {
    const a = await post('getBuildingConfigs', { auth, character_id });
    if (a.error || a.needs_auth) failures++;
    const b = await post('getGameInfo', { auth });
    if (b.error || b.needs_auth) failures++;
    if (id % 2 === 0) {
      const c = await post('combatGetConfigs', { auth });
      if (c.error || c.needs_auth) failures++;
    }
  }
}

async function reloader() {
  while (Date.now() < deadline) {
    process.kill(serverPid, 'SIGHUP');
    await sleep(100);
  }
}

const workers = [];
for (let i = 0; i < 8; ++i) workers.push(worker(i));
workers.push(reloader());
await Promise.all(workers);

try {
  process.kill(serverPid, 0);
} catch {
  throw new Error('server exited during the stress run');
}
if (failures > 0) {
  throw new Error(`${failures} config request(s) failed during reload`);
}

console.log(`config_reload_stress PASSED (${seconds}s, 8 readers, 10 HUP/s)`);
process.exit(0);
