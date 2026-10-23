#!/usr/bin/env node
/**
 * Combat WebSocket smoke test — Milestone 1 verification.
 *
 * Exercises the full PvE loop against a running server:
 *   createAccount → login → combatGetConfigs → combatCreate
 *   → WS auth → ready → countdown → battle (send a move command, wait for
 *   combat/attack events) → match_ended.
 *
 * Usage: node tools/combat_smoke.mjs --port 3390
 * (see tools/combat_smoke.sh for the server-harness wrapper)
 */
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const portIdx = args.indexOf('--port');
const port = portIdx >= 0 ? parseInt(args[portIdx + 1], 10) : 2290;
const oppIdx = args.indexOf('--opportunity');
const opportunity_id = oppIdx >= 0 ? args[oppIdx + 1] : 'goblin_raiders';
const aoe_check = args.includes('--aoe-check');
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

const out = [];
function log(...parts) {
  out.push(parts.join(' '));
}

// Node 22+ ships a global WebSocket; fall back to the `ws` package if needed.
let WebSocketImpl = globalThis.WebSocket;
if (!WebSocketImpl) {
  const wsmod = await import('ws');
  WebSocketImpl = wsmod.default;
}

try {
  const w1 = await safe_words('safe_words_1.txt');
  const w2 = await safe_words('safe_words_2.txt');
  const username = `smoke_${Date.now().toString(36)}`;
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
  log(`account=${username} character_id=${character_id}`);

  const cfg = await post('combatGetConfigs', {
    auth: { username, token },
    character_id
  });
  const rule_ids = (cfg.data?.rulesets ?? []).map((r) => r.id).join(',');
  const map_ids = (cfg.data?.maps ?? []).map((m) => m.id).join(',');
  const opp_ids = (cfg.data?.opportunities ?? []).map((o) => o.id).join(',');
  log(`combatGetConfigs rulesets=[${rule_ids}] maps=[${map_ids}] opportunities=[${opp_ids}]`);
  if (!rule_ids.includes('skirmish') || !map_ids.includes('meadow')) {
    throw new Error(`unexpected configs: ${JSON.stringify(cfg)}`);
  }

  const created = await post('combatCreate', {
    auth: { username, token },
    character_id,
    mode: 'pve',
    opportunity_id
  });
  const match_id = created.data?.match_id;
  if (!match_id) throw new Error(`combatCreate failed: ${JSON.stringify(created)}`);
  log(`combatCreate match_id=${match_id} code=${created.data?.match_code}`);

  const ws = new WebSocketImpl(wsUrl);
  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = () => reject(new Error('websocket connect failed'));
  });

  // First message MUST be auth (docs/combat_protocol.md); the server only then
  // subscribes the socket and sends welcome + a full state snapshot.
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

  let authed = false;
  let member_ids = [];
  let sent_deploy = false;
  let sent_construct = false;
  let sent_construct_seen = false;
  let structure_built_seen = 0;
  let structures_seen = false;
  let sent_move = false;
  let player_ids = new Set();
  let aoe_events = 0;
  let hold_fire_events = 0;
  let enemy_seen = 0;
  let attack_events = 0;
  let unit_moved = false;
  let started_at = Date.now();

  const result = await new Promise((resolve) => {
    const timer = setTimeout(
      () => {
        ws.close();
        resolve({ timed_out: true });
      },
      90000
    );

    ws.onclose = () => {
      clearTimeout(timer);
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
        log(`welcome team=${msg.payload.team} retinue=${msg.payload.retinue.length}`);
        authed = true;
        member_ids = msg.payload.retinue.map((m) => m.member_id);
        // Lobby roster select (default all — this IS the roster) then ready.
        ws.send(
          JSON.stringify({
            v: 1,
            fmt: 'json',
            type: 'roster',
            match_id,
            tick: 0,
            payload: { unit_ids: member_ids }
          })
        );
        ws.send(
          JSON.stringify({ v: 1, fmt: 'json', type: 'ready', match_id, tick: 0, payload: {} })
        );
        return;
      }
      if (msg.type === 'match_state' || msg.type === 'match_update') {
        const p = msg.payload;
        if (Array.isArray(p.structures) && p.structures.length > 0) structures_seen = true;
        for (const u of p.units) {
          if (!u.is_enemy) player_ids.add(u.id);
          else enemy_seen = Math.max(enemy_seen, 1);
        }
        for (const e of p.events) {
          if (e.type === 'attack') {
            attack_events++;
            if (attack_events === 1) log(`first attack event: ${JSON.stringify(e.payload)}`);
            if (e.payload?.aoe === true) aoe_events++;
          }
          if (e.type === 'hold_fire') hold_fire_events++;
          if (e.type === 'structure_spawned' && !sent_construct_seen) {
            log(`structure_spawned: ${JSON.stringify(e.payload)}`);
            sent_construct_seen = true;
          }
          if (e.type === 'structure_built') structure_built_seen++;
        }
        // Deploy phase: place the squad in the team's zone, then deploy-ready.
        if (p.phase === 'deploy' && !sent_deploy) {
          const ids = member_ids.length ? member_ids : [...player_ids];
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
          ws.send(
            JSON.stringify({ v: 1, fmt: 'json', type: 'ready', match_id, tick: 0, payload: {} })
          );
          sent_deploy = true;
          log(`deployed ${ids.length} unit(s) and set deploy-ready`);
          return;
        }
        // Track movement: any player unit away from its spawn proves the move command landed.
        if (p.phase === 'battle' && !sent_construct && player_ids.size > 0) {
          // M3: channel a barricade (solid immediately, builds up). Place it in
          // the player's own half but well clear of the deploy zone so the
          // server's 0.06 min-build-distance from units cannot trip on the
          // builder's own spawn drift (team-1 deploy zone is
          // x∈[0.03,0.23], y∈[0.35,0.65]; (0.35, 0.62) is >0.12 away).
          const ids = [...player_ids].slice(0, 1);
          ws.send(
            JSON.stringify({
              v: 1,
              fmt: 'json',
              type: 'command',
              match_id,
              tick: 0,
              payload: { cmd: 'construct', unit_ids: ids, structure_type: 'barricade', x: 0.35, y: 0.62 }
            })
          );
          sent_construct = true;
          log(`sent construct (barricade at 0.18,0.4) for ${ids.length} unit(s)`);
          return;
        }
        // Send the move once the construct is initiated (or after a hard 12s
        // fallback) so the fight always proceeds.
        if (p.phase === 'battle' && sent_construct && !sent_move && player_ids.size > 0 &&
            (sent_construct_seen || (p.battle_time ?? 0) > 12)) {
          const ids = [...player_ids].slice(0, 4);
          ws.send(
            JSON.stringify({
              v: 1,
              fmt: 'json',
              type: 'command',
              match_id,
              tick: 0,
              payload: { cmd: 'move', unit_ids: ids, target_x: 0.85, target_y: 0.5 }
            })
          );
          sent_move = true;
          log(`sent move for ${ids.length} player unit(s)`);
        }
        if (sent_move) {
          for (const u of p.units) {
            if (!u.is_enemy && u.x > 0.28) {
              unit_moved = true;
              break;
            }
          }
        }
        return;
      }
      if (msg.type === 'match_ended') {
        log(
          `MATCH_ENDED winner=${msg.payload.winner} reason=${msg.payload.reason} ` +
            `xp_by_member=${JSON.stringify(msg.payload.xp_by_member ?? [])} ` +
            `casualties=${msg.payload.casualties?.length ?? 0} ` +
            `seed=${msg.payload.seed ?? '?'} state_hash=${msg.payload.state_hash ?? '?'} ` +
            `elapsed_seconds=${((Date.now() - started_at) / 1000).toFixed(1)}`
        );
        clearTimeout(timer);
        ws.close();
        resolve({ ended: true, payload: msg.payload });
      }
    };

    ws.onerror = () => {
      clearTimeout(timer);
      resolve({ ws_error: true });
    };
  });

  log(
    `phases spoken: deploy=${sent_deploy} construct=${sent_construct} built_events=${structure_built_seen} ` +
      `structures_seen=${structures_seen} battle=${sent_move} enemies_seen=${enemy_seen} ` +
      `attack_events=${attack_events} aoe_events=${aoe_events} hold_fire_events=${hold_fire_events} ` +
      `unit_moved=${unit_moved} ` +
      `result=${Object.keys(result)[0]} (opportunity=${opportunity_id})`
  );

  const xp_reported =
    result.ended && Array.isArray(result.payload.xp_by_member) && result.payload.xp_by_member.length > 0;

  // Verify XP actually persisted to retinue_members (retinue_member.to_json
  // now carries `xp`; the persistence is deferred to the loop thread, so give
  // it a moment).
  let persisted_xp = 0;
  if (xp_reported) {
    await new Promise((r) => setTimeout(r, 1000));
    const retinue = await post('getRetinue', { auth: { username, token }, character_id });
    const members = retinue.data?.members ?? retinue.data?.retinue ?? [];
    for (const m of members) {
      if (m.id === 1 || m.member_id === 1) {
        persisted_xp = m.xp ?? 0;
        break;
      }
    }
    log(`persisted xp of member 1: ${persisted_xp}`);
  }

  const ok = sent_deploy && sent_construct && sent_construct_seen && structures_seen &&
    sent_move && enemy_seen > 0 && attack_events > 0 &&
    (!aoe_check || aoe_events > 0 || hold_fire_events > 0) &&
    result.ended && xp_reported && persisted_xp >= 60;
  // Winner -1 (draw) is possible with a tiny roster vs the goblin warband; any
  // terminal state is a pass — we only require the full loop to have run.
  console.log(out.join('\n'));
  if (!ok) {
    console.error('SMOKE FAILED: battle loop did not reach an end state with combat.');
    process.exit(1);
  }
  process.exit(0);
} catch (e) {
  console.error('SMOKE FAILED:', e.message);
  console.error(out.join('\n'));
  process.exit(1);
}