#!/usr/bin/env node
/**
 * Graceful-shutdown ordering test — companion to tools/combat_shutdown_test.sh.
 *
 * Brings up a real PvE battle (create → WS auth → roster → ready → deploy) and
 * then just holds the connection open while periodically issuing commands. The
 * harness starts the server with --shutdown-after-ms, so the server initiates a
 * graceful fall-through while this battle is live; we pass if the server closes
 * our socket cleanly (no crash) and fail on a connect/timeout error.
 *
 * Usage: node tools/combat_shutdown_test.mjs --port 3455
 * Exit: 0 = server closed us (expected), 2 = failure/timeout.
 */
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const portIdx = args.indexOf('--port');
const port = portIdx >= 0 ? parseInt(args[portIdx + 1], 10) : 2290;
const base = `http://127.0.0.1:${port}`;
const wsUrl = `ws://127.0.0.1:${port}/ws/combat`;

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

let WebSocketImpl = globalThis.WebSocket;
if (!WebSocketImpl) {
  const wsmod = await import('ws');
  WebSocketImpl = wsmod.default;
}

try {
  const w1 = await safe_words('safe_words_1.txt');
  const w2 = await safe_words('safe_words_2.txt');
  const username = `shutdown_${Date.now().toString(36)}`;
  const password = 'shutdown_pass_123';

  const acct = await post('createAccount', {
    username,
    password,
    adult: false,
    word1: w1[0],
    word2: w2[0]
  });
  if (!acct.data && !acct.token) throw new Error(`createAccount failed: ${JSON.stringify(acct)}`);

  const login = await post('login', { auth: { username, password } });
  const token = login.data?.token ?? login.token;
  const character = login.data?.characters?.[0];
  if (!token || !character) throw new Error(`login failed: ${JSON.stringify(login)}`);
  const character_id = character.id;

  const created = await post('combatCreate', {
    auth: { username, token },
    character_id,
    mode: 'pve',
    opportunity_id: 'goblin_raiders'
  });
  const match_id = created.data?.match_id;
  if (!match_id) throw new Error(`combatCreate failed: ${JSON.stringify(created)}`);

  const ws = new WebSocketImpl(wsUrl);
  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = () => reject(new Error('websocket connect failed'));
  });

  ws.send(
    JSON.stringify({
      v: 1,
      fmt: 'json',
      type: 'auth',
      match_id,
      tick: 0,
      payload: { username, token, character_id, match_id }
    })
  );

  let member_ids = [];
  let deployed = false;
  let in_battle = false;

  const result = await new Promise((resolve) => {
    const deadline = setTimeout(() => {
      resolve({ timed_out: true });
    }, 30000);

    ws.onclose = () => {
      clearTimeout(deadline);
      resolve({ closed: true });
    };

    ws.onmessage = (ev) => {
      let msg;
      try {
        msg = JSON.parse(ev.data);
      } catch {
        return;
      }
      if (msg.type === 'welcome') {
        member_ids = msg.payload.retinue.map((m) => m.member_id);
        ws.send(JSON.stringify({ v: 1, fmt: 'json', type: 'ready', match_id, tick: 0, payload: {} }));
        return;
      }
      if (msg.type !== 'match_state' && msg.type !== 'match_update') return;
      const p = msg.payload;
      if (p.phase === 'deploy' && !deployed) {
        const ids = member_ids.length ? member_ids : p.units.filter((u) => !u.is_enemy).map((u) => u.id);
        for (let i = 0; i < ids.length; i++) {
          ws.send(
            JSON.stringify({
              v: 1,
              fmt: 'json',
              type: 'command',
              match_id,
              tick: 0,
              payload: {
                cmd: 'deploy',
                unit_ids: [ids[i]],
                x: 0.14 + (i % 3) * 0.03,
                y: 0.45 + Math.floor(i / 3) * 0.03,
                facing: 3.05
              }
            })
          );
        }
        ws.send(JSON.stringify({ v: 1, fmt: 'json', type: 'ready', match_id, tick: 0, payload: {} }));
        deployed = true;
        return;
      }
      if (p.phase === 'battle' && !in_battle) {
        in_battle = true;
        console.log('battle started; holding connection for server shutdown');
        const me = p.units.find((u) => !u.is_enemy);
        if (me) {
          ws.send(
            JSON.stringify({
              v: 1,
              fmt: 'json',
              type: 'command',
              match_id,
              tick: 0,
              payload: { cmd: 'move', unit_ids: [me.id], target_x: 0.5, target_y: 0.5 }
            })
          );
        }
      }
    };
  });

  if (result.closed) {
    console.log('server closed the combat socket (graceful shutdown observed)');
    process.exit(0);
  }
  console.error('shutdown test: timed out waiting for the server to close the socket');
  process.exit(2);
} catch (err) {
  console.error(`shutdown test failed: ${err?.message ?? err}`);
  process.exit(2);
}
