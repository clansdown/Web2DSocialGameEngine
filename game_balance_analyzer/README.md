# game_balance_analyzer

A standalone C++23 / CMake tool that analyzes Ravenest's gameplay balance from
the JSON configs in `game/config/`. The goal is to catch significantly
off-balance content (cost-vs-power outliers, stage regressions, arbitrage
opportunities, unwinnable difficulty) without thousands of hours of manual
playtesting.

It provides **static balance analysis** for all four game systems — manor, tower
defense, RTS combat, and weeding — plus a **strategy simulation** for the manor
economy that reports how many candidate strategies land within 1%/2%/3%/4%/5%/10%
of the optimal outcome.

## Building

```bash
cd game_balance_analyzer
chmod +x build_analyzer.sh     # only needed once
./build_analyzer.sh            # configure + build (default)
./build_analyzer.sh -q         # quiet
./build_analyzer.sh -v         # verbose
./build_analyzer.sh -c         # clean + fresh configure
./build_analyzer.sh --help
```

If you prefer not to mark it executable, run it with `bash build_analyzer.sh ...`
instead. (`build.sh` is a thin wrapper that delegates to `build_analyzer.sh`.)

The binary is produced at `build/game_balance_analyzer`.

Exit codes: `0` success, `1` build/configure failure.

The build reuses the nlohmann/json header already vendored at
`../server/vendor/nlohmann`, so no network access is required.

## Usage

The config directory is auto-resolved when `--config-dir` is omitted: the tool
walks up from the current working directory to find `<root>/game/config`, so the
binary can be run from anywhere inside the repository (repo root,
`game_balance_analyzer/`, its `build/` dir, etc.).

```bash
# Run from the repo root — config is found automatically
./game_balance_analyzer/build/game_balance_analyzer manor

# Run all static analyses (default; from game_balance_analyzer/)
./build/game_balance_analyzer

# Run specific modules
./build/game_balance_analyzer manor td
./build/game_balance_analyzer rts weeding

# Point at the config directory explicitly (overrides auto-resolution)
./build/game_balance_analyzer --config-dir ../game/config all

# Run the manor strategy simulation too
./build/game_balance_analyzer --sim all

# Control the simulation
./build/game_balance_analyzer --sim --days 30 --iterations 2000 manor

# Reproduce a previous run exactly (the seed is printed on every --sim run)
./build/game_balance_analyzer --sim --seed 123456789012345 manor

# Tune the production-network report (per-stage ratios, build-out estimate)
./build/game_balance_analyzer --scales 5 --build-slots 3 --day-seconds 60 manor

# Machine-readable output
./build/game_balance_analyzer --sim --json --out report.json all
```

### Options

| Option | Description |
|--------|-------------|
| `--config-dir <path>` | Config root. Default: auto-resolved by walking up from the CWD to find `<root>/game/config` (falls back to `../game/config`) |
| `--sim` | Run the manor strategy simulation only (suppresses the static analysis and production-network report) |
| `--iterations N` | Repeats per heuristic AND Monte-Carlo sample count (default 2000) |
| `--days N` | Simulation horizon in days (default 30) |
| `--seed N` | RNG seed for the simulation (default: random) |
| `--scales N` | Production-network anchor scales (default 5) |
| `--build-slots N` | Concurrent construction slots in the build-time estimate (default 3) |
| `--day-seconds N` | Construction seconds per simulated day (default 60) |
| `--stages N` | Production-network stages to analyze, 1..N (default: all) |
| `--quiet` | Suppress in-place sim progress feedback on stderr |
| `--json` | Emit JSON instead of text |
| `--out <file>` | Write output to a file |
| `-h`, `--help` | Show help |

Value-taking flags accept either `--flag value` or `--flag=value`
(e.g. `--stages 2` or `--stages=2`).

### Modules

- `manor` — static economy analysis + production-network report + units report; with `--sim`, runs only the strategy simulation
- `td` — tower defense static analysis
- `rts` — combat static analysis
- `weeding` — weeding static analysis
- `all` — every module (default)

## Architecture

Clean separation by game, mirroring the server's config layout. Each module is
self-contained and produces a `balance_report` of findings.

```
src/
├── main.cpp                 CLI: subcommands + flags, dispatch, output
├── config_loader.hpp/.cpp   RAII JSON loader; validates paths stay in config dir
├── report.hpp/.cpp          balance_report: findings with severity, text/JSON render
├── statistics.hpp/.cpp      optimality_distribution: within-X%-of-optimal histogram
├── manor/
│   ├── manor_model.hpp/.cpp      building_type + building_registry (stage chains)
│   ├── manor_economy.hpp/.cpp    independent economy simulation (mirrors server tick)
│   ├── manor_analyzer.hpp/.cpp   static manor balance analysis
│   ├── manor_strategy.hpp/.cpp   weighted-policy + Monte-Carlo strategy simulation
│   └── network.hpp/.cpp          production-network ratio analysis (per stage)
├── td/td_analyzer.hpp/.cpp       tower/unit/mob/wave static analysis
├── rts/rts_analyzer.hpp/.cpp     combatant/hero static analysis
└── weeding/weeding_analyzer.hpp/.cpp  plant/tool static analysis
vendor/
├── pcg_random.hpp   pcg-cpp (M.E. O'Neill, MIT/Apache-2.0) — PCG64 RNG
└── pcg_extras.hpp   support code for pcg_random.hpp (same license)
```

### Design principles

- **RAII**: `config_loader` owns the resolved config directory and cache;
  resources are managed automatically. No manual `new`/`delete`.
- **Independent clean models**: the tool re-implements the game rules rather
  than including server internals, keeping it standalone and readable. It must
  be kept in sync with the server when game rules change.
- **snake_case** throughout, per the project's AGENTS.md conventions.
- **No hardcoded user-facing text**: all output is structural (JSON) or
  generated from the analyzer's own labels, not the game's translation system.

## The strategy simulation (`--sim`)

The manor economy is simulated day-by-day using an independent re-implementation
of the server's economy tick (`updateStateSince`): buildings produce outputs,
consume inputs (gated by satisfaction), pay `daily_cost`, receive chain-aware
modifiers, and import/export around per-resource reserves. Modifiers follow the
server's `modifier_id` non-stacking rule: within a `modifier_id` group each
target output is boosted at most once (the strongest source — level, then
multiplier — covers the population; weaker sources cover overflow), while
different `modifier_id`s (e.g. `bread_baking` × `flour_milling`) stack
multiplicatively.

Every strategy is driven by one adaptive **policy**: each simulated day it builds
**all currently-feasible** buildings (in a loop) until nothing more can be built,
then runs the economy tick. A building is feasible when its level-1 build cost is
coverable from current stock plus imports affordable with the current fungible
gold/silver_pence wallet (gold and pence are one wallet at 240 pence/gold), its
`max_per_fiefdom` cap is not yet reached, its `manor_level` requirement is met,
**and** its `arable_acres`/`forest_acres` fit within the manor's remaining
land (arable and forest totals both scale with the current manor level).

The manor house itself is a **first-class action**: the policy also offers
`upgrade_manor` as a weighted candidate whenever the manor can be afforded at its
next level (weight `1.0` by default; a heuristic can tune it via
`weights["upgrade_manor"]`, `0` disables it). Upgrading spends the home_base's
next-level cost (gold/wood/beams/boards/iron/ironwork; stone was removed from
the economy), raises the manor level, and
unlocks more arable/forest land and higher-stage buildings — so stage-3 types
(manor ≥ 6) only appear after the sim levels the manor. Monte-Carlo draws a fresh
random weight for the upgrade like any building.

Each run starts from the fiefdom's **starting resources** read from
`game/config/economy.json` `starting_resources` (the same single source of truth
the server uses when creating a fiefdom — currently 5 gold and 0 of everything
else). The simulation does **not** hard-code starting balances, so it always
mirrors the live game.

Two strategy families are evaluated:

1. **Heuristics** — defined in `game/config/analyzer_manor_strategies.json`.
   Each heuristic is a set of **weights** over building choices plus optional
   **minimum ratios** between buildings. At each build decision the policy picks
   among currently-feasible buildings proportionally to the weights; a ratio
   `A -> { B: n }` means a second `A` is not built until `count(A)*n <= count(B)`
   (e.g. one blacksmith needs five peasants before a second blacksmith is allowed).
   Each heuristic is run `--iterations` times (seeded) to gather statistics.
2. **Monte-Carlo** — `--iterations` random strategies. Each run draws a fresh
   random weight for every candidate building (uniform `[0,1]`) and follows that
   weighted policy; at each build decision the weights are re-normalized over the
   currently-feasible buildings (never picking from buildings the player could not
   afford at that moment).

Per-heuristic **descriptive statistics** (mean, median, p10/p90, min/max, stddev
of score) are reported, alongside the combined **optimality distribution** for the
whole population.

While the simulation runs it prints in-place progress to **stderr** (`\r` +
clear-line), one line per heuristic and for Monte-Carlo. Pass `--quiet` to
suppress it. Progress is only emitted when `--iterations > 1`.

### Randomness & reproducibility

Both the heuristics and Monte-Carlo use randomness (weighted/uniform feasibility
picks). They are driven by a **PCG64** generator (the `pcg64` engine from M.E.
O'Neill's `pcg-cpp` library, vendored under `vendor/`; PCG is a modern,
high-quality successor to MT19937).

- By default the engine is seeded from `std::random_device` (a fresh random seed
  each run). Every `--sim` run prints the effective seed to stderr and to the
  output/JSON, so the exact run can be replayed.
- Pass `--seed N` to reproduce a previous run exactly (same config, same seed →
  identical results). `--iterations` controls the repeats per heuristic and the
  Monte-Carlo sample count.

### Known simplifications

The simulation is a summary model, not a pixel-perfect replica of the server:

- `manor_level` prerequisites are **enforced**; the sim can level the manor house
  via the `upgrade_manor` action (default weight 1.0; see above). The day-by-day
  trace reports the current level in each day's buildings section as
  `home_base=<level>` — the manor house is always a single instance, so its level
  substitutes for a constant count of 1 — matching the `[ml=N]` day marker.
- Water-powered buildings and infrastructure (road, races, pond, chapel/church)
  are not buildable by a policy.
- Modifier composition is aggregated multiplicatively per source, and each source
  boosts at most `max_targets` target instances (matching the network report's
  ideal-targets model); target choices are first-come in build order.
- Per-output production rates (`output_rates`) default to 1.0.

These simplifications affect absolute numbers but preserve relative ordering,
which is what the balance comparisons care about. Re-run after server rule
changes.

### Known finding: Monte-Carlo can dominate via the build/export loop

The sim does not model construction time or build-slot limits — a policy can
build as many affordable buildings per day as its income allows. Combined with
import-enabled affordability (shortfalls paid from gold/silver_pence), the
feasibility-gated Monte-Carlo tends to spam-build hundreds of buildings financed
by penny-market exports, scoring far above the (weighted) directed heuristics.
This is **not a simulation bug** — it is a genuine config-balance signal that
exports are highly profitable and building is cheap. Treat the absolute Monte
Carlo optimum cautiously; the directed heuristics are the more conservative
comparison. If construction-time/budget throttling is wanted, that is a future
extension.

## Static analysis highlights

- **Manor**: input-aware per-building daily net (import/export priced), payback
  period, import/export arbitrage detection, prerequisite reachability,
  manor-level gating sanity, the **production-network report** (see below), and
  the **units report** (see below).
- **TD**: tower/unit DPS-per-gold, upgrade efficiency, mob HP/reward scaling,
  and wave-template difficulty scaling (total HP per difficulty).
- **RTS**: combatant cost-vs-power curves, upkeep efficiency, hero skill value,
  and damage-type coverage.
- **Weeding**: plant HP vs per-tool damage (actions to clear), tool aggregate
  effectiveness, and high-spread smother-crop risk.

## The production-network report

The `manor` module emits a per-stage production-network analysis that
coordinates building inputs, outputs, and export pricing — replacing the old
standalone stage-regression check. It is produced when running `manor` without
`--sim` (in `--sim` mode only the strategy simulation is output).

For **each stage** of the building chains (independently; chains shorter than
the stage contribute their top building), the module:

1. **Balance solve** — finds the minimal integer building counts where every
   internally-producible resource is in surplus after satisfying all internal
   consumption (`outputs − inputs − daily_cost ≥ 0`). Modifier buildings
   (`miller`/`windmill`/`baker`) are part of the set and use an
   *ideal-targets* assumption: each modifier source boosts up to `max_targets`
   target instances. Sources are grouped by `modifier_id` — within a group each
   target is boosted at most once (a miller and windmill never stack on the same
   grain output), so the strongest source covers the population and weaker ones
   cover overflow only.
2. **Anchor scaling** — identifies the "final-goods" producer (the least-numerous
   building whose outputs are not consumed by other set buildings, e.g. the
   blacksmith) as the anchor, and iterates k = 1..`--scales` anchor instances,
   scaling every other count by `ceil(k × ratio)`. At each k it reports counts
   (absolute and per-anchor ratio; the manor house appears as `home_base lvl10`
   — its assumed level, not a count), net resources/day, net gold/day (surplus
   at export price, plus gold output), total build cost, and a **parallel-aware
   build-time estimate** (`--build-slots` concurrent constructions from
   `construction_times`, `--day-seconds` per day, gold cost reported
   separately; `manor_level` requirements are assumed satisfied at a
   fully-upgraded (level-10) manor house).
3. **Warnings** — `max_per_fiefdom` caps, water-power capacity vs mill-pond
    capacity, and unresolvable internal deficits.

**Arable land** is modeled as a constraint. Each stage/scale reports
`arable_acres` used (Σ `arable_acres` × count) against `arable_total` (the
level-10 manor maximum, 1000 acres) and warns when the set exceeds it.

JSON output lives under `manor_network` (per-stage `scales` + a `summary` of
the k=1 stage progression). Each scale's JSON sets `counts.home_base` to the
assumed manor level (10) — with no `ratios.home_base` — and includes an explicit
`manor_level` field.

## The manor units report

The `manor` module also emits a **units report** (JSON key `manor_units`) that
scores every building type at **every level** `1..max_level` in isolation,
assuming **all inputs and maintenance costs are imported** (nothing is supplied
locally). It answers "what is this building worth per day if I buy everything
and sell what it makes?".

For each building × level it reports, in **gold** and **silver pence** (penny
market vs gold market, at 240 pence/gold):

1. **Gross (saleable surplus)** — production beyond what the building consumes
   for its own upkeep, valued at **export price**. Production is calculated
   before daily costs, so a building's own output first covers its own
   `daily_cost` (that portion nets to zero).
2. **− Input cost** — production inputs at **import price**. Inputs gate
   production: importing them enables full output.
3. **− Upkeep (external daily cost)** — the `daily_cost` **not** covered by the
   building's own production, at **import price**.
4. **Net** = gross − inputs − external upkeep.
5. **Build cost** (separate) — cumulative gold-normalized cost to reach the
   level, and **payback days** = build cost / net (gold equivalent); "never"
   when net ≤ 0.

No modifiers are applied (each building is scored at its base output). Pure
infrastructure (`road`, `head_race`, `tail_race`, `mill_pond`) is skipped, and
buildings that produce nothing (e.g. `home_base`, `chapel`, `church`,
`parish_church`, `miller`, `windmill`) are summarized as a list rather than
listed per level. Peasant households are a building class (not a tracked
population resource), so no population is modeled.

## Extending

To add a check to a module, add a finding in the corresponding `*_analyzer.cpp`
using `report.add_info/warning/critical`. To add a new game module, create a
`src/<game>/` directory, implement a `analyze()` returning `balance_report`,
wire it into `main.cpp`, and add the `.cpp` to `CMakeLists.txt`.
