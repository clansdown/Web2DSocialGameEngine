# Ravenest RTS — Game Design

**Status:** Design complete (v1 scope). This is the canonical design reference for the
realtime combat game (`/ws/combat`). It was produced through an extended design
collaboration; the numbered **Decision Log** (Appendix A) records every decision with a
one-line rationale and marks the values that are tuning constants.

**Sibling docs** (wire/format contracts — this doc *references* them, it does not
replace them):
- `docs/combat_protocol.md` — WebSocket transport, envelope, message types, replication
- `docs/combat_maps.md` — map file format (`tile_costs`, spawn points, obstacles)
- `docs/combat_rulesets.md` — ruleset config schema
- `docs/retinue_design.md` — the manor-side roster, recruitment, wounds, morale

**Doc-conflict list** (existing files that are now outdated *by this design*; to be
updated during implementation, not in this doc):
- `combat_rulesets.md` — skirmish `win_conditions` currently says `survive_until_end`;
  this design replaces it with **annihilation** (`warband_wiped`); `match_duration_seconds`
  becomes the safety cap; `wounded` death-handling stays.
- `combat_protocol.md` — gains per the **Protocol & Config Delta** section: `facing` on
  unit rows, a `structures` array + events, `attack` carries `result`, the `deploy` phase,
  `deploy_zone` on maps.
- `combat_maps.md` — `obstacles` gain per-obstacle blocking flags; maps gain `deploy_zone`.
- `combat_system.md` (server/docs) — combatant config gains `max_hp`, `attack_range`,
  `attack_interval`, `attack_type`, `turn_rate`, `dodge` triple, `direction_defense` triple.

---

## 1. Vision & Pillars

The player is a knight with a sworn household (retinue) billeted at their manor. The
realtime combat game turns that retinue into a **squad-level real-time tactics** game:
you command a small, *named, persistent* army against enemy war-parties on a battlefield.

**Pillars:**

- **Squad tactics, not base-building RTS.** No barracks, no harvesting, no in-match
  economy. Units map 1:1 onto your persistent retinue members. Combat is the shaping
  pressure; your **available army** is the resource.
- **Units are precious.** No permanent death anywhere — the worst combat outcome is
  infirmary time. This shapes army sizes (small), stakes (real but recoverable), and UI
  (individual soldier presence).
- **No single right answer.** Every system is designed so positioning, composition, and
  micro genuinely trade off: directional defense vs. magic vs. walls vs. AoE coexist as
  distinct answers (see **Multiple Answers Matrix**, §6.4).
- **Config-driven everything.** Units, warbands, enemy behaviors, structures, formations,
  rewards all live in JSON with the linter + docs updated. New content is authoring, not
  code.
- **Server-authoritative.** 10 ticks/s over `/ws/combat`, one worker thread per match,
  RAM-only matches, entity deltas + full snapshots, reconnect resync (existing foundation,
  unchanged).
- **Indirect fire has real geometry.** Flanking, firing lanes, cover, and safe-arcs
  matter — both for your army and the enemy's.

## 2. Game Identity & Scope

**What this game is:** a realtime tactics game where 1–8 co-op players command their
retinues (typically 2–20 soldiers + the knight hero) in **targeted-annihilation**
missions against an enemy warband.

**What it is not (v1 non-goals):**
- No base building, unit production, resources, or tech in-match.
- No permanent unit death (see Pillars).
- No player abilities (the data model reserves the slot).
- No fog of war (full visibility; a light-fog variant is a documented v2 polish, §17).
- No PvP matchmaking (scrimmage stays a later milestone).

## 3. Mission Model — Targeted Annihilation

### 3.1 The opportunity model

Missions are **opportunities that exist in the world**, not containers you fill with
enemies. Diagetically: *the world has warbands forming; you assemble a party and go try
one.*

- A mission **opportunity** is authored in config: a ruleset + a **map** + a **warband**
  template + a named **tier** label + flavor text ("three score orcs and a troll").
- The warband is **fixed at match creation** — joining or leaving **never changes it**
  (Decision 62). There is no party-power scaling machinery.
- Players browse available opportunities and join by code/party (reusing the existing
  lobby-browser pattern). Late joiners change nothing about the fight.
- The player's loadout agency lives on the *other* side: choosing which members to field
  and how to fight them.

### 3.2 Win / lose / draw

- **Win:** the warband is wiped out.
- **Lose:** your side is wiped out.
- **Draw:** the safety cap elapses without the warband wiped (see §3.4).

### 3.3 Full visibility

The player sees the whole map, the warband, and every enemy from the start. No fog in
v1. (Against pick-offs, the warband's focused battle-state reacts — see §11.)

### 3.4 Safety cap

There is **no expected match timer**. A long configurable **safety cap** (e.g. 20 min,
tuning) guarantees termination:

- Reaching the cap in an annihilation mission without the warband wiped = **draw** —
  neither victory nor defeat morale, base XP without the victory multiplier (Decision 66).
- Turtling behind walls cannot win: the mission is "annihilate," and the cap refuses to
  reward stalemates (Decision 55).
- Scrimmage (PvP) keeps its own most-survivors resolution later.

### 3.5 Co-op

- 1–8 players on one team. Party joins via the opportunity/match code.
- Team composition is not prescaled: five players can gang up on a single goblin — and
  earn nearly nothing for it (XP is warband-derived, §14.1).
- Forfeit: a leaver's units **withdraw unwounded**; the team continues without them; their
  **structures persist as team-owned** (block both sides; garrisoned archers eject
  unwounded). The leaver records a defeat.

## 4. Player Journey

```
Combat entry → Mission pick → Roster select → Lobby → Deploy → Countdown → Battle → Results
```

### 4.1 Mission pick
Ruleset + **map** + **difficulty tier** + warband preview (composition, tier, danger
estimate as static flavor text). Choosing an opportunity creates the lobby; the warband is
locked from this moment (Decision 62).

### 4.2 Roster select
After seeing the enemy, the player toggles which **fieldable** members they bring
(defaults all). The **knight is always fielded** (can't be deselected). Party scaling
doesn't exist, so loadout is pure tactical choice — not a difficulty slider.

### 4.3 Lobby
Host shares the match code; friends join; per-player ready. The lobby browser surfaces
open opportunities (returns to prominence from the existing scaffold).

### 4.4 Deploy (~25 s, tuning)
Players place their squad inside their team's **deploy zone** (a per-team region authored
on the map). Placement supports a **facing toggle/rotate gesture**; default facing =
**toward the enemy camp** (so the first volley never lands on your own back arcs).
Individual **Ready** ends the phase early when all players are ready; otherwise
auto-deploy on timer. No structures can be built during deploy/countdown (structures are
in-match time, not setup).

### 4.5 Countdown → Battle
5 s countdown, then the battle runs until wipe / cap / surrender.

### 4.6 Results
Victory/defeat/draw banner + reason; per-member **XP gained + level-ups**; wounds list
(members sent to recover); a rewards stub (morale now, gold/equipment tables empty).

## 5. Units & Combat Model

### 5.1 Combatant config (extended)

Both `player_combatants.json` and `enemy_combatants.json` gain **optional** per-level
fields (all with sensible defaults so existing entries still load):

| Field | Type | Default | Purpose |
|---|---|---|---|
| `max_hp` | number (per-level) | derived from class | in-match health pool (see HP bridge) |
| `attack_range` | number (per-level) | class default | engagement reach in normalized units |
| `attack_interval` | number (per-level) | class default | seconds between attack resolutions |
| `attack_type` | string | highest damage stat | `melee` \| `ranged` \| `magical` (per combatant) |
| `turn_rate` | number (per-unit) | global constant | radians/sec rotate-toward-target speed |
| `direction_defense` | `{front, side, back}` (flat, per-unit) | `{1.0, 0.6, 0.35}` | arc multipliers on the per-type base |
| `dodge` | `{front, side, back}` (flat, per-unit) | `{0, 0, 0}` | dodge chance per arc, 0..1 |

Per-level arrays use the existing interpolation/extrapolation rules and `combatants.hpp`
accessors (with the single-element guards).

**Magic** is blocked by true walls by default (Decision 60), with a per-attack
`penetrates_walls` flag for authoring piercing spells later.

### 5.2 HP bridge

- Roster health is a persistent **percentage** (0–100, recovery model).
- In-match `max_hp` is a real value per level.
- **Match HP = class `max_hp` × roster health%** (75% roster → 75% of max_hp).
- A wound sets roster health to the member's **end-of-combat HP%** (already implemented in
  `retinue_db::apply_wounds`).

One linear percentage scale; no double-mapping. Members fieldable at ≥ `min_deploy_hp`
(50%) start the battle hurt.

### 5.3 Movement speed

`movement_speed` is a multiplier on a config **base speed of 0.04 normalized-units/s**
(tuning): spearman (1.0) ≈ 25 s full-map cross; goblin (1.5) ≈ 17 s; troll (0.6) ≈ 42 s.
Marches feel deliberate; engagements resolve within seconds of entering range.

### 5.4 Facing, turn, and directional defense

**(a) Arcs.** Every unit has three defense arcs determined by its **facing**:
- **Front** = ±90° around the facing heading (180° sector)
- **Sides** = the two 45° bands between front and back
- **Back** = ±45° around the rear (90° sector)

Front is *huge* (half the circle) and back is *narrow* — getting to the back is a
positioning achievement, which is the point of flanking.

**(b) Config shape.** `defense` arrays remain the per-level, per-type **frontal base**
(`{melee, ranged, magical}` per level). A flat, per-unit `direction_defense` multiplier
set scales it:
```
effective_defense[type][arc] = defense[type][level] × direction_defense[arc]
```
A plate knight: `{1.0, 0.6, 0.3}`. A squirrelly goblin: `{0.9, 0.85, 0.85}` — it barely
cares about direction.

**(c) Facing updates.** Units face their movement heading while moving. When attacking,
they **rotate toward the target at their `turn_rate`** (per-unit, default = global
constant). A unit struck from multiple sides must choose one facing, exposing its back to
the rest.

**(d) Dodge arcs.** `dodge` is a flat per-unit triple; the **defender's value for the arc
the attack lands on** is what rolls (a skirmisher's back dodge may be near-zero — running
away is genuinely exposed).

**(e) Flanking behaviors** (see §11) use the arcs: goblins surround to expose backs; orcs
hold formation to present fronts.

### 5.5 Combat resolution

1. The unit attacks with its `attack_type` (defender mitigates by that same type).
2. **Mitigation:** `final = damage × 100 / (100 + effective_defense[type][arc])`
   (diminishing; defense 10 ≈ 9% reduction, 25 ≈ 20%). No zero-damage hard walls.
3. **Dodge:** a separate roll against `dodge[arc]`; a dodged hit deals 0.
4. **Cosmetic projectile / hitscan:** damage + dodge resolve at attack-resolution instant
   on the server; the **client draws the arrow** and shows the HP change *when the arrow
   visually lands* via a **pending-damage ledger** (decision 8).

### 5.6 Melee rings are physical

Collision is **everything hard** (Decision 12): units block each other. Only as many
melee as physically fit around a target engage it — no artificial attack slots, no
death-ball focus-fire instant kills. Friendly units block friendlies too; pathing flows
around (see §8).

### 5.7 Ranged: units never block, structures do

- **Unit bodies never block ranged** — archers fire over their own melee's shoulders
  (Decision 45).
- **Structures** carry a `blocks_ranged` flag; a blocking structure between attacker and
  target stops the shot (the attack resolves `blocked`, arrow visual hits the structure).
- **Magic** is blocked by true walls by default, respecting the property pair (§6.4).

## 6. AoE, the Alchemist, and Indirect Fire

### 6.1 AoE as a damage class

Area-of-effect damage **ignores arcs entirely** ("standing in a pool of fire hits you
from all sides") and **does not roll dodge**: it deals uniform damage to **every unit and
structure** in radius — including structures in `building` state — with a flat mitigation
per target.

**Wall shelter:** a `blocks_ranged` wall intersecting the line from the AoE epicenter to a
target **fully shelters** that target from the splash. Fire-through structures do not
shelter. Natural terrain obstacles follow their own flags (§8.4).

### 6.2 The alchemist

A new recruit-gated player combatant (and an enemy variant, §11.5), modeled on the
tower-defense alchemist: *"Hurls alchemical flasks that create damaging area effects on
the ground."*

- **Splash only** — the flask damages via the AoE splash, there is no direct non-splash
  hit.
- Splash resolves vs the target's **magical defense**; **no dodge; no arcs** (§6.1).
- **Blast size:** splash radius ≈ **1.5 tiles (a 3×3 man-grid, ~0.09 normalized, tuning)** —
  devastating on a tight stack, a shrug against spread formations.
- Slow attack rate (≈2.9 s interval), short–mid range, level-scaled damage like every
  class.

### 6.3 Friendly fire — ON, but default-safe

Splash damages **everyone** in the radius (units and structures, both sides). Because
your units are named and precious, three mechanisms keep self-harm a *choice*, not an
accident:

1. **Safe-arc auto-aim (over-throw).** Auto-acquisition seeks a landing point that keeps
   the target in the blast while **clearing every friendly** (units **and** friendly
   structures) — shifting away from the nearest friendly up to **cap = splash radius**,
   bounded by **max attack range**. The attacker's **own body counts as a friendly** (no
   self-splash). **Fire-through cover (barricades/spikes) is cleared**, not avoided — the
   *cover-lob* lands past your own barricade and may clip its near edge; **walls and
   towers stay protected** (Decision 57).
2. **Hold fire.** If no safe center exists (enemies fully swarmed by your own melee), the
   unit **holds fire** and emits a **status event** ("no safe shot — pull the melee or
   take a risky shot") until a safe arc exists (Decision 53).
3. **Risky explicit shots.** The **Attack order accepts a ground point** ("Lob the flask
   HERE") for AoE units; the flask lands exactly where pointed *regardless of what's in
   the blast*, with a **friendly-fire highlight** painted on the client (blast circle +
   any friendly caught glows red). The player sees the risk and owns it.

**Control affordance:** the command card carries a **Lob** command; in smart-tap mode,
ground-taps route to Lob while an alchemist is selected (and RMB-on-ground does the same
in mouse mode). This prevents "attack the ground" from being ambiguous with Move.

### 6.4 Multiple answers matrix

Four ways to hurt a walled-up shield line, no single dominant one:

| Threat | Beats | Is countered by |
|---|---|---|
| Melee charge | soft targets, exposed flanks | walls, hard bodies, rings |
| Physical ranged | anything in a firing lane | true walls (`blocks_ranged`) |
| Magic (directed) | armor — arcs apply, but magic bypasses *facing* only when authored | true walls by default; `penetrates_walls` spells later |
| AoE / flask splash | tight stacks, armor walls (uniform, no dodge) | spread, **true-wall shelter**, tiny blast |

**Magic/wall property pair** (Decision 56): structures carry `blocks_physical_ranged` and
`blocks_magical_ranged` flags (a *magic wall* blocks all magic); attacks carry
`penetrates_walls`. v1 defaults: physical blocks everything ranged; magic is blocked by
walls unless authored to pierce.

## 7. Temporary Structures

Structures are **deployable combat equipment** — not base building. They only exist
in-match, only during the battle phase, and vanish at match end. **No manor persistence.**

### 7.1 The v1 set

| Structure | Footprint | `blocks_ranged` | Role |
|---|---|---|---|
| **Barricade** | 1-wide | false (fire-through) | strong fire-through cover; gated by cap |
| **Spikes** | small | false (fire-through) | budget blocker — cheaper, faster, lower HP (no damage-on-cross; nothing crosses, bodies are hard) |
| **Wall** | 1-wide | true | true block; shelters from AoE; movement + ranged |
| **Archer tower** | tower cell | false (arrow-slit: movement blocked, ranged passes in *and* out) | garrison + buff (below) |

All structures have **uniform armor** — no facing, no arcs, no dodge — and take splash
damage like any target.

### 7.2 Build — channeled

- **Construct** is an order for any selected unit: builders move to the site and
  **channel** (busy + vulnerable) over a short build time.
- **Up to N builders** channel simultaneously, each adding build rate (the exact count is
  a value, ~2 tuning).
- The structure's **HP ramps through construction**; damage taken **persists** (no
  snap-to-full on completion).
- Interrupting (builder killed, or a **new explicit order cancels the channel**) **aborts
  the build** and resets progress. A structure reaching 0 HP mid-build is destroyed and
  the builders are freed.
- **Structures block movement the instant construction starts** (solid immediately); map
  placement validation prevents walling in your own squad.

### 7.3 Limits & placement

- **Per-player config caps** per type (e.g. barricade 8, tower 3 — values are tuning).
- Placement on **any open valid tile** (not on units/structures/obstacles); **no
  team-zone lock** — build anywhere reachable.
- **No team-wide cap**: the natural limiters are builder *time* and the escalating
  channel commitment (Decision 59). Bigger co-op parties fight on sized maps (§3.1/§13).

### 7.4 Archer tower garrison

- Capacity **1 archer**; moving an archer onto the tower **auto-garrisons**.
- The garrisoned archer is **mechanically immune to melee** (enemies must destroy the
  tower to reach it), but **ranged and magical can still target the archer directly** —
  arrow-slit semantics.
- Garrisons get config `+damage%` and `+range%` buffs.
- Tower destroyed → the archer is **expelled at their current HP**, standing at the tower
  footprint (the structure vacated the tile — no "no free space" case).
- A garrisoned archer that dies in the tower leaves it empty (buff off, no expulsion,
  re-garrisonable).
- Building-state towers cannot garrison.

## 8. Movement, Pathfinding, Collision

### 8.1 Pathfinding

- **Player units:** per-unit **A\*** over the `tile_costs` grid, waypoint following,
  re-planned lazily (on order change or stuck detection).
- **Enemy groups:** shared **flow fields** so a warband moves cohesively.
- **Planner collision split:** A\* plans over **static geometry only** (terrain +
  structures); living units are resolved by the collision pass, never treated as impassable
  walls for planning. Structures are **true impassable path obstacles**.

### 8.2 Collision — everything hard

Units and structures are hard bodies: no overlap, movement blocked. The sim resolves
shoves/nudges so armies flow without passing through each other. Physical melee rings
fall out of this (§5.6). Friendly units block friendlies too.

### 8.3 Formations

- **Persistent movement modifier** (Decision 42): a formation stays active (shaping
  subsequent Move / Attack-Move orders) until toggled off or a non-formation order.
- Shapes: **Line / Column / Wedge / Circle**, a **rotation handle**, **hotkeys**
  (Ctrl+1..9) co-exist with the palette.
- Anchor + relative offsets; units face travel direction and reform while moving.
- **Gate degradation:** if a formation-pathed move hits a too-narrow passage, units
  degrade to free pathing and **reform at the destination** — no hard-body deadlock.

### 8.4 Terrain blocking — per kind

(Map format evolves per §16.) Natural features block per-kind:
- **Impassable `tile_costs` cells (0)** — block **movement only** (water, a ditch:
  arrows still fly over).
- **`obstacles`** — carry per-obstacle flags (`blocks_movement`, `blocks_ranged`,
  `blocks_aoe`): a boulder blocks everything (default solid), a hedge blocks movement
  only. The existing meadow boulder becomes a solid block by default.

## 9. Orders & Command

Full command vocabulary with **queued orders** (shift-click appends; a new explicit order
replaces). Single executor, order queue per unit.

| Order | Behavior |
|---|---|
| **Move** | Path to a point. **Never auto-engages, even when hit** — kiting and retreat work (Decision 41; stance only applies when idle). |
| **Attack-Move** | Move along a path, engage hostiles en route. |
| **Attack** | Focus-fire a specific target (melee chase within pursuit range). For AoE units, **Attack accepts a ground point** = Lob. |
| **Stop** | Cancel orders; stand; engage in aggro radius. |
| **Hold** | Stand firm; engage within range but never leave the square. |
| **Patrol** | Loop/ping-pong waypoints, engaging along the path. |
| **Stance** | `aggressive` / `defensive` / `hold` — changes acquisition range & pursuit tolerance (see §10). |
| **Formation** | Persistent movement modifier (§8.3). |
| **Construct** | Channeled build of a structure (§7.2). |

If a unit is **channeling**, any new explicit order cancels the channel (build aborts);
queues only run after a channel completes.

## 10. Acquisition & Unit AI (player side)

- **Idle (defensive stance):** auto-acquire and engage hostiles within an acquisition
  radius; don't chase far.
- **Aggressive stance:** engage while moving; wider acquisition, longer pursuit.
- **Hold stance:** engage only within attack range; never move off the spot.
- **Move order:** never auto-engages (§9). Cancel with explicit Stop/Attack.
- **Retargeting:** when the current target dies or disengages, reacquire automatically per
  stance.
- **Ranged LOS:** a shot requires an unobstructed line when a `blocks_ranged` structure (or
  per-kind terrain, §8.4) sits between; a commanded target with no line causes an
  attack-move-to-gain-LOS behavior; otherwise retarget a visible hostile.

## 11. Enemy AI — Behavior-Profile System + Warband Battle State

### 11.1 Unit-level fight styles (per class, config)

| Class | Style | Signature trait |
|---|---|---|
| Goblin | Swarm | Fast, reckless charge, surrounds to expose backs, no leash, fragile |
| Bandit | Skirmisher | Keeps ranged distance, flees melee, repositions |
| Orc | Stout | Slow advance, focus-fire discipline, high defense, retreats at low HP |
| Troll | Juggernaut | Huge HP/damage, ignores flee, priority magnet |
| Dark mage | Backline | Max range behind a screen, retreats when approached, focuses enemy backline |
| Flask-lobber | Artillery | Safe-arc AoE lobbing behind a screen (alchemist's enemy variant) |

### 11.2 Group-level battle logic (per warband group)

- `tactic` — `charge` / `hold` / `skirmish` / `guard` / `reinforce` (rush to the alarm)
- `aggro_model` — `local` (only this group) / `call_to_arms` (nearby groups switch to
  wartime behavior) / `global` (everyone knows)
- `engagement` — target-priority (backline/casters first), ranged spacing, focus-fire
- `retreat` — fall-back threshold vs fight-to-the-death
- `siege` — flag: attack a `blocks_ranged` structure that fully blocks the group's
  approach rather than wandering

### 11.3 Warband battle state

The warband **knows it's in a battle**. Once any contact happens, idle groups switch to
their configured wartime behavior and alert allies per `aggro_model` — so you can't
systematically pick off isolated campers. Idle groups still operate in their camp
(near the authored spawn/camp area); reaching them deliberately is the mission.

### 11.4 Flanking synergy

Goblin "surround" naturally **opens attack lanes** for the warband's flask-lobber — the
same profile system feeds both the surround *and* the lobber's safe centers.

### 11.5 The enemy flask-lobber

A warband artillery unit using the same safe-center/over-throw logic as the player's
alchemist (it clears its own warband's units, holds fire when no safe arc). This presents
the **wall-shelter counter** to co-op parties: spread, shelter, or push melee contact to
suppress it.

## 12. Controls & UX

### 12.1 Pointer scheme — classic + touch parity

**Desktop:** LMB select (+ box + shift-add), RMB context command (move/attack/Lob),
MMB or M2-drag pan, wheel zoom, WASD/arrows, minimap click-jump.

**Touch (first-class), two user-toggleable modes:**
- **Smart-tap mode:** tap-select; tap-enemy = attack, tap-ground = move (Lob for
  alchemists); long-press-drag pan; pinch zoom; tap minimap jump; on-screen drag = box.
- **Context-panel mode:** tap-select, then a panel offers move/attack/hold/patrol before
  acting.

**Both modes always show the command bar** (below), so touch never depends on
right-click.

### 12.2 Selection

Full toolbox: click, drag-box, shift/ctrl-click add, double-click select all of type on
screen, **control groups Ctrl+1..9** (touch: long-press unit to assign; group button on
the card).

### 12.3 Camera

Full kit: pan, edge scroll (toggleable), zoom (clamped), keyboard scroll, minimap jump.

### 12.4 HUD — classic bottom command card

- **Bottom card:** selection info (name, class, level, HP, **front/side/back defense &
  dodge**) + order buttons (including **Construct**, **Lob**, **Formation**) + stance.
- **Top bar:** phase/timer, team counts, connection status.
- **Right rail:** chat / voice / leave.
- **Minimap:** bottom-right.

### 12.5 In-world feedback

Procedural class sprites (team colors + class silhouette; shield/sword shapes), name
labels, selection rings, HP bars, **facing chevrons**, attack-order markers, **damage
numbers timed to arrow landing** (rear-arc hits tinted), friendly-fire highlights on risky
explicit shots, and a **hold-fire reason icon** (e.g. "no safe arc") so the alchemist's
silence is legible.

### 12.6 Queue affordance

Shift-click queues on desktop; **touch** gets a **queue-toggle pill** that stays set for
the next orders (equality of capability).

## 13. Match Flow Engine

### 13.1 Phases

`lobby → deploy → countdown → battle → ended`

- **Lobby:** opportunity fixed at create; roster select; joins by code; ready.
- **Deploy:** placement in the team deploy zone; default facing toward the enemy camp;
  rotation handle; individual Ready early-exits (all-ready) else auto-deploy on timer.
- **Countdown (5 s)** then **battle**.
- **Battle** ends on warband wipe (win), player side wipe (loss), safety cap (draw), or
  forfeit.

### 13.2 Disconnect / forfeit

- **Lobby:** disconnect removes the player.
- **Deploy:** disconnected squad **stays** (placed or auto-deployed at phase end);
  reconnect resumes the deploy view; a `leave` forfeits.
- **Battle:** `leave` withdraws the player's units unwounded (§3.5).

### 13.3 Winner semantics (environment-owner model)

Enemy units are owned by the **environment** (player id 0, team 2) in a **negative id
namespace** so the client can distinguish "retinue unit" from "warband unit" at a glance.
Winner values for annihilation: **1 = players win, 0 = players lose, -1 = draw**.

## 14. XP, Rewards, Morale

### 14.1 XP — fixed per-member, warband-derived

- Each fielded member earns **base XP computed from the warband's size and composition**
  (strength-derived or authored per warband; tuning).
- Roster size and party size **do not divide it** — bringing your whole roster is never
  punished, and five players ganging up on one goblin each earn the goblin's tiny XP
  (Decision 63). The skill is choosing a warband you can beat that still pays.
- **Victory multiplies** the base; **draw = base only** (no multiplier); no kill/assist
  tracking — completion-only (Decision 21, refined by 63).
- Level-up at threshold; cap at class `max_level`. **Knight XP levels the knight
  independently in the RTS** (character level untouched; Decision 43).
- **XP applies only after match persistence** — snapshot stats are fixed for the match;
  no mid-match level-ups.

### 14.2 Rewards pipeline

**Pipeline live, gold/equipment tables empty in v1** (Decision 22): morale runs now; gold
spoils and gear-grant slots exist in config values defaulted to zero/empty so the machine
ships before it's fed. No half-fed features when gold/gear arrive.

### 14.3 Morale

`fiefdoms.last_victory_ts` / `last_defeat_ts` set on win/loss respectively. A **draw sets
neither** (explicit, symmetrical). These feed the existing household-morale decay terms.

## 15. Server Architecture

### 15.1 Tick composition (per 100 ms tick, in order)

1. **Drain commands** → validate & assign orders (move/AM/attack/stop/hold/patrol/stance/
   formation/construct).
2. **Formation refresh** → recompute per-unit waypoint targets from the anchor.
3. **Enemy AI decisions** → per-profile aggro/targeting/reinforce/retreat/siege; warband
   battle state.
4. **Targeting & acquisition (both sides)** → acquire/retarget per stance + profile.
5. **Movement** → path-follow (A\*/flow-field waypoints) with **hard-body collision
   resolve**; structure/terrain as obstacles.
6. **Combat resolve** → cooldowns, hitscan damage + dodge, arc lookup, AoE splash +
   wall-shelter, deaths/wounds events, cosmetic-arrow events (`attack` with `result`).
7. **Respawns / cleanup** (rulesets that have them) + **end-condition check**.

### 15.2 Determinism

A **seeded PRNG per match** (created at match start) drives all dodge and AI randomness —
battles are reproducible by rerunning the same seed (debugging, balance, tests). The
client never needs to be deterministic.

### 15.3 Entities

- `combat_unit` — extended: base stats (max_hp, damage/defense, speed, range, interval,
  arc multipliers, dodge, turn_rate), runtime (order queue, waypoints, target id,
  cooldown, stance, facing, formation slot, enemy flag).
- `combat_structure` — id, owner/team, type, x/y, footprint, hp/max_hp, state
  (building|active|destroyed), garrison slot, blocking flags.
- Enemies: environment owner (0, team 2), negative id namespace.

### 15.4 Threading & persistence

Unchanged foundation: one worker thread per match, loop thread only enqueues commands,
SQLite writes deferred via `Loop::defer` after battle ends (wounds, XP, morale).

## 16. Protocol & Config Delta

### 16.1 Wire changes

- Unit rows gain **`facing`**.
- New parallel **`structures`** array: `{id, type, owner, team, x, y, hp, max_hp, state,
  garrisoned_unit_id}` with dirty-tracking deltas + full snapshots; `structure_spawned /
  _built / _destroyed` events.
- **`attack` events** carry **`result: landed | dodged | blocked`** (+ blocking structure
  id for the arrow-to-wall visual), damage, arc landed on, and for AoE: center + radius +
  per-target damage list so the delayed-HP ledger stays in sync.
- New **`deploy`** phase on `match_state`.
- `match_ended` gains XP + morale + reward-stub payload.

### 16.2 Config & data files

| File | Change |
|---|---|
| `player_combatants.json` / `enemy_combatants.json` | + `max_hp`, `attack_range`, `attack_interval`, `attack_type`, `turn_rate`, `direction_defense`, `dodge` |
| `combat/warbands.json` (new) | opportunity templates: map + warband composition + tier + XP base + flavor text |
| `combat/` profiles (or inline) | per-class fight styles + per-group tactic/aggro/engagement/retreat/siege |
| `combat/rulesets.json` | skirmish: `win_conditions=["warband_wiped"]`, duration = safety cap (draw at cap) |
| `combat/maps/*.json` | + per-team `deploy_zone`; per-obstacle blocking flags |
| `combat/structures.json` (new) | barricade/spikes/wall/tower definitions |
| `combat/formations.json` (new) | shape offsets, rotation, movement params |
| `combat/rewards.json` (new) | pipeline slots (morale / xp / gold / equipment) |
| `retinue_members` table | + `xp` column (migration), + `level` reads for cap |
| `check_configs.py`, `server/docs/combat_system.md`, docs referenced in §Doc-conflict-list | updated validation + docs |

## 17. Deferred

Abilities (slot reserved in the data model); light-fog visibility (camp-visible,
units-hidden, v2 polish); PvP matchmaking + scrimmage; fog of war; server-simulated
projectiles; kill/assist XP; hand-authored art (procedural sprites behind the same sprite
API); cover bonuses for barricades; Demolish command; per-unit battle stats dossier;
`penetrates_walls` spells; touch control-group polish beyond the long-press affordance;
TURN relay for voice; passive member XP/leveling systems outside combat.

## 18. Appendix A — Decision Log

Each entry: **# — decision** (rationale). Values marked *(tuning)* are deliberately
spelled-out but not balanced.

### Identity & scope
1. **Squad real-time tactics** — command a small named retinue; no base building or
   in-match production (builds on the existing foundation).
2. **PvE loop = targeted annihilation** — wipe an authored warband; no survival timers.
3. **No player abilities in v1** — stats-only differentiation; the order/data model
   reserves the slot.
4. **Extend combatant config** (optional per-level `max_hp`, `attack_range`,
   `attack_interval`, `attack_type`, `turn_rate`, `direction_defense`, `dodge`) — classes
   become genuinely different; linter/docs updated.
5. *(superseded by 62)* warband config base + scaling — replaced by the opportunity model.

### Warband & mission
6. **Enemy AI = profile system + warband battle state** — per-class styles + per-group
   axes + focused wartime reaction.
7. **Full command spec incl. formations** — the player's tactical vocabulary.
8. **Cosmetic projectile / hitscan** — server damage+dodge at resolution; client-drawn
   arrow; delayed-HP ledger keeps bar and arrow in agreement.
9. **Linear HP bridge** — match HP = class max_hp × roster health%. One scale, no
   double-mapping; consequences visible in both worlds.
10. **Base speed 0.04 n/s** × movement_speed — deliberate marches, fast enough engagements.
11. **Dodge per-arc** (flat per-unit triple) — supersedes the earlier flat per-level
    dodge; the back value encodes "exposed while fleeing." *(tuning)*
12. **Everything hard** — units + structures block; physical melee rings.
13. **A\* for players, flow fields for enemy groups** — robust pathing on 16×16+ and
    cohesive warbands.
14. **Classic pointer + touch parity** — the muscle-memory standard, mobile first-class.
15. **Full selection** incl. control groups.
16. **Full camera kit** incl. minimap jump.
17. **Toggleable touch mode** — smart-tap + command bar, or context action panel.
18. **Classic bottom command card** HUD.
19. **Roster toggles after mission pick** — see the enemy, then choose your squad.
20. **Timed deployment + per-player Ready** — placement agency, never lingering.
21. **XP completion-only** — no kill tracking; *(refined by 63)*.
22. **Reward pipeline live, tables empty** — morale + XP now; gold/gear slots zeroed.
23. **Formations: palette + rotation + hotkeys** — all co-exist.
24. **Procedural class sprites** — no art dependency; real art drops behind the same API.
25. **Environment owner + negative ids for enemies** — clean team math + client typing.
26. **No timer; safety cap** — guarantees termination; *(refined by 55)*.
27. **Seeded PRNG per match** — reproducible battles.
28. **Queued orders v1** — shift-click queue; full command spec.

### Directional defense
29. **`direction_defense` {front,side,back} flat per unit** — multipliers over the
    per-level per-type defense base.
30. **Arc geometry: front ±90°, sides 45° each, back ±45°** — flanking is a positional
    achievement.
31. **Facing + turn rate toward target** (per-unit `turn_rate`) — adjacent attackers
    force a facing choice.
32. **Dodge arcs** — defender's arc value rolls. *(tuning values)*
33. **Flanking behaviors in enemy profiles** — goblins surround, orcs present fronts.

### Structures
34. **Temporary structures = deployable equipment** — in-match only, physical-only v1.
35. **Channeled build** — builder busy/vulnerable, HP ramps, interrupt aborts.
36. **Archer tower = melee-immunity only** — capacity 1, ranged/magic can hit the
    garrison, +damage/+range buff, expel on destroy.
37. **Per-player caps; any valid tile; no team lock** — player agency + bounded spam.
38. **Physical-only v1** — cover bonuses + Demolish later.

### Review pass 1
39. **Ranged blocking is a per-structure property** — not a global rule.
40. **Arc multipliers on all three directed types, not AoE** — magic bypasses facing
    only via authored `penetrates_walls`; AoE is uniform.
41. **Move never auto-engages** — kiting/retreat work; stance governs when idle.
42. **Formation = persistent modifier + gate degradation** — no hard-body deadlock.
43. **Knight XP levels independently in the RTS** — character level untouched.
44. **Full visibility v1** — light-fog is v2 polish on the same engine.

### AoE & indirect fire
45. **Units never block ranged** — only structures do.
46. **v1 structures: barricade, spikes, wall, tower** — four distinct roles.
47. **AoE exists as a damage class; alchemist is the source** (player + enemy variant).
48. **AoE: ignores arcs, no dodge, hits units+structures incl. building, flat
    mitigation, true-wall shelter** — "fire hits from all sides," walls shelter.
49. **Alchemist splash vs magical defense** — no new stat keys; mages/alchemists share
    the weakly-armored niche.
50. **Friendly fire ON but default-safe** — over-throw + hold-fire + highlight.
51. **Attack-ground (Lob)** — auto-acquire + explicit placement.
52. **Enemy flask-lobber in warbands** — presents the wall-shelter counter.
53. **No safe shot → hold fire** — zero accidental friendly fire, with a status event.
54. **Over-throw treats structures as friendly** — *(refined by 57)*.

### Review pass 2
55. **Safety cap = draw in annihilation** — turtling can't win.
56. **Magic/wall property pair** — per-structure `blocks_*` × per-attack
    `penetrates_walls`.
57. **Over-throw clears fire-through cover** — the cover-lob is possible; walls/towers
    stay protected.
58. **Structures solid immediately** — physical from frame one; placement validation
    prevents walling in your own squad.
59. **Per-player caps only, no team-wide** — build-time + map size are the natural
    limiters. *(tuning)*
60. **v1 magic blocked by true walls by default** — `penetrates_walls` authorable later.
61. **Spikes = budget blocker** — cheaper/smaller/lower-HP barricade, no damage-on-cross.

### Final seam pass
62. **Warband fixed at create — no party scaling** — the opportunity model; solves
    scaling timing and map/party coupling by definition.
63. **XP = fixed per-member, computed from warband size & composition** — no roster/party
    division, no small-roster exploit, no gang-up farming.
64. **Maps ship inside the opportunity** — picked with the mission; no size-band
    mechanics.
65. **Terrain blocks per-kind** — impassable cells = movement only; per-obstacle flags
    for ranged/AoE; default solid; ditch-vs-boulder authoring.
66. **Draw = base XP, no victory multiplier, no morale timestamp** — participation isn't
    wasted, draws never farm progression.

### Baked-in rulings (no open decision)
- Spawn/deploy facing defaults **toward the enemy camp** (+ rotate gesture).
- Forfeit: units withdraw unwounded; structures persist team-owned; garrisoned archers
  eject unwounded.
- Multi-builder: up to ~2 channel, each adds build rate; all dead → abort.
- Collision: A\* over static geometry; resolver shoves bodies; structures are true
  obstacles.
- Attacker's own body counts as a friendly for over-throw (no self-splash).
- `attack` events carry `result: landed | dodged | blocked` (+ blocking structure id).
- Garrison death leaves tower empty/buff off; re-garrisonable.
- Any new explicit order cancels a channel; queues run after completion.
- No dodge in the AoE path (even for garrisoned archers).
- Knight always fielded (roster toggles exclude the knight).
- XP levels apply only after match persistence (no mid-match level-ups).
- Deploy disconnect: squad stays, auto-deploys on phase end.
- Draw morale: neither timestamp.

### Tuning constants (spelled values, not balanced)
- Base speed 0.04 n/s; default `direction_defense` `{1.0, 0.6, 0.35}`; splash radius
  ≈ 1.5 tiles; safety cap ≈ 20 min; deploy ~25 s; build ~2 builders; per-player caps
  ≈ barricade 8 / tower 3; default `turn_rate`; XP thresholds/table; warband XP bases.