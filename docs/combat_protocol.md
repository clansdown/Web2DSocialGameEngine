# Realtime Combat Protocol

The combat game (PvE 1–8 players, future PvP 2–64) runs over a WebSocket at
**`/ws/combat`** on the same port as the REST API (2290). This document is the
contract between `server/combat/` and `client/src/combat/`.

## Transport

- WebSocket (RFC 6455) via uWebSockets' `app.ws()`, same listener as HTTP.
- Production: nginx terminates TLS and proxies with the WebSocket upgrade
  headers (see `server/README.md` Deployment). Vite dev proxy forwards
  `/ws` with `ws: true`.
- **Auth**: the FIRST message from the client must be `auth` with the same
  session token the REST API uses. The server validates it and only then
  subscribes the socket to the match's pub/sub topics.
- **Reconnect**: on disconnect the client reconnects with exponential backoff
  (1s → 10s), re-authenticates, and the server resends a full `match_state`
  snapshot, so the client never misses state.

## Envelope (JSON codec)

Every message is a JSON object:

```json
{ "v": 1, "fmt": "json", "type": "match_update", "match_id": "m42", "tick": 137, "payload": { ... } }
```

- `v` — protocol version (1)
- `fmt` — codec id (`"json"`; `"binary"` reserved)
- `type` — message type (below)
- `match_id` — the match this message belongs to
- `tick` — server tick sequence; every state message carries a strictly
  increasing tick, which the client uses for ordering and gap detection
- `payload` — type-specific object

## Message Types

### Client → Server

| Type | Payload | Purpose |
|---|---|---|
| `auth` | `{username, token, character_id, match_id}` | First message; validates and subscribes |
| `ready` | `{}` | Signals lobby readiness; **roster select + deploy ready** flow per rts_design.md §4 |
| `roster` | `{unit_ids[]}` | Roster select in the lobby: which fieldable members to field (knight always fielded, D19). Allowed until `ready` |
| `command` | `{cmd, ...}` | Game decision. Command vocabulary (rts_design.md §9): `move` (`unit_ids`, `target_x`, `target_y`), `attack_move` (`unit_ids`, `target_x`, `target_y`), `patrol` (`unit_ids`, `target_x`, `target_y`), `attack` (`unit_ids`, `target_unit_id` or `ground_x`/`ground_y` for a Lob on AoE units), `stop`, `hold`, `stance` (`aggressive\|defensive\|hold`), `formation` (`formation_id`, `rotation`), `construct` (`unit_ids`, `structure_type`, `x`, `y`), `deploy` (deploy-phase placement: `unit_ids`, `x`, `y`, `facing`), plus legacy `ability` (`ability_id`) reserved |
| `chat` | `{text}` | Team chat (≤ 500 chars); relayed to the sender's team topic |
| `voice` | `{to_player, signal_type, data}` | WebRTC signaling relay (offer/answer/ice) to a teammate |
| `request_state` | `{}` | Requests a full `match_state` snapshot (gap recovery) |
| `leave` | `{}` | Forfeits the match and closes the socket |

### Server → Client

| Type | Payload | Purpose |
|---|---|---|
| `welcome` | `{player_id, team, mode, match_code, ruleset_id, ruleset, map, players[], retinue[], deploy_zones[], banner}` | Sent once after auth, before the first state. `map` carries `deploy_zones` (per-team deploy rects, rts_design.md §4.4). Each `retinue[]` row carries its **`cosmetic`** loadout, and `banner` is the player's equipped banner — cosmetics ride this join-time snapshot, never tick deltas (rts_design.md §15.3) |
| `match_state` | state payload, `full: true` | Full snapshot: initial join, reconnect, `request_state`, and every 25 ticks (2.5 s) as a sync guard |
| `match_update` | state payload, `full: false` | Partial update: dirty entity rows (units + structures) + removals + events |
| `match_ended` | `{winner, reason, xp_by_member[], level_ups[], casualties[], rewards_stub{}, seed, state_hash}` | Battle over. `winner`: **1 = players win, 0 = players lose, -1 = draw** (rts_design.md §13.3). Each casualty: `{member_id, owner, end_hp}` (`end_hp` is the member's HP% when they fell — the wound severity under `wounded` rulesets). `xp_by_member` maps member_id → XP granted; `level_ups` is `[{member_id, level_ups}]`; `rewards_stub` carries morale flags + empty gold/equipment/cosmetics slots. `seed` + `state_hash` support the determinism replay check (D27; see `tools/replay_combat.sh`) |
| `chat` | `{from, from_name, team, text, timestamp}` | Team chat delivery |
| `voice` | `{from, from_name, signal_type, data}` | WebRTC signaling delivery |
| `error` | `{error}` | Protocol/validation error; socket may be closed |

### State payload (match_state / match_update)

```json
{
  "full": true,
  "phase": "deploy",
  "countdown": 3.4,
  "deploy_time": 12.0,
  "battle_time": 0,
  "units": [
    { "id": 1, "owner": 5, "name": "Sir Wolf", "unit_class": "knight",
      "level": 1, "x": 0.125, "y": 0.5, "facing": 3.14, "hp": 100, "max_hp": 100, "status": "alive",
      "attack_type": "melee", "def_front": 1.0, "def_side": 0.6, "def_back": 0.3,
      "dodge_front": 0, "dodge_side": 0, "dodge_back": 0, "radius": 0.035 }
  ],
  "structures": [
    { "id": 901, "type": "wall", "owner": 5, "team": 1, "x": 0.4, "y": 0.5,
      "hp": 120, "max_hp": 400, "state": "building", "garrisoned_unit_id": 0 }
  ],
  "removed": [],
  "events": [ { "type": "unit_spawned", "payload": { "unit_id": 1 } } ]
}
```

- `units` — **entity-level deltas**: for `match_update` only units whose state
  changed this tick; each row is that unit's COMPLETE state, so the client
  overwrites its local copy (idempotent, no merge logic). Unit rows carry
  `facing` (radians) per rts_design.md §17.1, the static selection-card stats
  (`attack_type`, `def_*`, `dodge_*`), and **`radius`** — the normalized body
  size (per-class config `size`) the client uses to scale the drawn sprite/token
  (rts_design.md §5.6), independent of the source image dimensions.
- `structures` — the parallel structure array (`match_update` carries only
  dirty structures): `{id, type, owner, team, x, y, hp, max_hp, state,
  garrisoned_unit_id}`. Structures are RAM-only; they spawn under the
  `construct` command, ramp HP through the `building` state, and are removed
  on destruction (rts_design.md §7).
- `phase` — `lobby → deploy → countdown → battle → ended`. The **deploy**
  phase (rts_design.md §13.1) accepts `deploy` commands; `deploy_time` counts
  down the auto-deploy timer.
- `removed` — ids of permanently-removed units/structures (erased server-side;
  client deletes).
- `events` — transient notifications (`unit_spawned`, `unit_died`,
  `unit_retreated`, `unit_garrisoned`, `unit_expelled`, `structure_spawned`,
  `structure_built`, `structure_destroyed`, `structure_attack`, `build_aborted`,
  `attack`, `hold_fire`, `ability`, `system`). `attack` events carry **`result:
  landed | dodged | blocked`** (+ blocking structure id), the damage, the arc
  landed on, and for AoE attacks the center + radius + per-target damage list
  (rts_design.md §17.1). `unit_retreated` marks a forfeiting player's units
  withdrawing unwounded (§3.5); `build_aborted` marks a cancelled channel whose
  progress reset (§7.2).

## State Replication Model

- **Server-authoritative.** Clients send decisions; the server simulates at
  **10 ticks/second** and broadcasts. A tick is 100 ms — command latency is at
  most one tick.
- **Deltas are generated by the game logic, not diffed.** The simulation keeps
  a change recorder (dirty-unit set + event outbox) that it writes to as it
  mutates state; the broadcaster serializes exactly that. Idle units cost
  nothing.
- **Full snapshots every 25 ticks** bound any drift. A client that sees a tick
  gap (non-contiguous) sends `request_state` to resync.
- **Client interpolation**: entities are rendered at requestAnimationFrame
  speed, lerping between received positions — no snapping at 10 Hz.
- **Threading (server)**: matches simulate on worker threads, but uWS is
  single-threaded. Broadcasts are posted onto the uWS loop with `Loop::defer`
  (the only thread-safe uWS entry point) and published from the loop thread
  (`App::publish` on the match/team/player topics). SQLite writes are deferred to
  the loop thread the same way. Workers are stopped and joined before the App is
  destroyed on shutdown.

## Codec Pluggability

All logic builds wire-neutral `combat_message` structs; a `combat_codec`
serializes them. `json_codec` is default (hand-rolled fast serializer for the
hot state path — see `server/combat/CombatCodecJson.cpp`); `binary_codec` is a
compiling placeholder. The envelope `fmt` field lets a receiver dispatch.
Selection: server constant / `--combat-codec` flag; per-connection negotiation
(auth declares `formats`, welcome echoes the pick) is a future option.

## Topics

uWS pub/sub topics per match:

| Topic | Subscribers | Content |
|---|---|---|
| `match:<id>` | all players | state + end messages |
| `match:<id>:team:<n>` | one team | team chat |
| `match:<id>:player:<n>` | one player | voice signaling (targeted) |

The simulation thread only publishes to `match:<id>`; chat and voice are
relayed on the loop thread without touching the simulation.

## Server Architecture Notes

- **RAM-only matches**: a match lives entirely in memory; nothing is written
  to SQLite during play. After the battle, casualties are persisted by
  deferring onto the uWS loop thread (`Loop::defer`) so the database stays
  single-owner.
- **One worker thread per battle** from a bounded pool (`--combat-sim-threads`,
  default = CPU count capped at 64). Lobby phases consume no worker.
- The loop thread only enqueues commands into the match's mutex-guarded queue;
  it never waits on the simulation.

## Voice Chat (WebRTC)

Voice uses the browser WebRTC standard (mesh topology): the server relays only
signaling (`voice` messages) and media flows player-to-player. STUN is a
placeholder — production needs a TURN relay for symmetric-NAT players
(configure in `client/src/combat/CombatVoice.svelte`).
