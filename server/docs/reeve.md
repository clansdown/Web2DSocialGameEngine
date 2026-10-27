# Reeve Advisor

The Reeve is the manor's advisor. It gives the player an overview of the
economy and exactly **one** reasonable next action, so manorial
micro-management stays optional.

V1 is intentionally not an optimizer. It uses a deterministic, style-weighted
candidate set and never persists advice.

## Design invariants

1. The report is a pure function of the current snapshot (fiefdom + economy
   ledger + config). Recomputing it from the same state produces the same
   report.
2. Exactly one action per recommendation. A road link is one logical action
   (an ordered cell list); it is not several recommendations.
3. Feasibility uses the real action validators. Build/upgrade/convert go
   through `ActionRegistry::validate`; build affordability additionally uses
   the import-aware `Validation::hasEnoughResources`.
4. If the preferred candidate is infeasible, the Reeve holds. It does not fall
   through to a cheaper candidate.
5. Roads are optional morale investments, not build requirements.
6. A speculative road is anchored by a dangling road end. When a road end has
   no adjacent building, `find_building_site` prefers a footprint beside it, so
   the building intent follows through on the next report.
7. The server emits no display text — only text ids, numbers, and text-id
   references.

## Files

- `server/ReeveConfig.{hpp,cpp}` — parse `game/config/reeve.json`.
- `server/ReeveAdvisor.{hpp,cpp}` — snapshot, sections, candidates, selection,
  feasibility, report DTO.
- `server/ReevePlacement.{hpp,cpp}` — site search, corridor reservation,
  road-link BFS, dangling-road anchor.
- `server/ReeveHandlers.{hpp,cpp}` — `getReeveReport`.
- `game/config/reeve.json` — thresholds, producer chains, styles.
- `api/getReeveReport.md` — endpoint contract.

## Economy snapshot

`/api/getReeveReport` runs the same lazy economy tick as `/api/getFiefdom` to
bring the fiefdom up to date, then projects one full day through the real
economy engine inside a transaction that is rolled back. That gives the Reeve a
stable per-day ledger and per-building input status **without advancing the
fiefdom**. The correct per-day period also avoids short-tick fractional-import
artifacts (imports are imported in whole units). The economy report now includes
`building_status`:

- per completed building: id, type, level, water-powered state;
- per output: resource, rate, amount/day, input satisfaction;
- per input: required/supplied per day and satisfaction.

The economy report also carries `demand` (the requested amount before
stock/import gating), so the Reeve can distinguish "consumed from stock" from
"cannot be supplied at all". The same data powers the building card's
input/output block.

## Sections

The report always has a treasury section and then the sections with meaningful
state: stores, production, land, water, morale, retinue, trade. Each section
has a stable id, a text-id status, a severity, optional details (each with a
drill-down target), and a section target.

## Candidate set

Each candidate has an id, category, severity, recommendation text, why text,
action, target, and an optional `intent_peer_id` for a paired road candidate.

Categories and the V1 rules:

- **Safety** — a tracked resource with low days of cover (critical/watch), and
  unfunded retinue members. Unfunded retinue prefers lowering a reserve to
  raise coin; with no sellable stock it becomes a money hold.
- **Continuity** — a building output whose input satisfaction is below the
  watch threshold. The first fix is enabling imports for the missing input;
  otherwise the grow ladder builds/upgrades the input producer.
- **Constraint** — arable/forest land below the watch threshold. The action is
  to upgrade the manor house (when it is below max level).
- **Growth** — the largest resource deficit with a configured producer chain,
  plus a separate heavy-import candidate. The grow ladder is:
  1. build out the current top stage up to `breadth_target` (breadth);
  2. upgrade the current top stage (depth);
  3. build the next stage, or convert a maxed current stage (tier);
  4. fall back to another current-stage building when no upgrade/tier exists.
- **Connect** — an existing producing building with no adjacent road, when the
  fiefdom has a road-morale source, gets a road link. A growth build that is
  not road-adjacent gets a paired speculative road candidate with the same
  `intent_peer_id`.
- **Polish** — switched-off outputs, surplus exports that could be kept by a
  higher reserve, and maxed buildings that can convert to their next stage.

`game/config/reeve.json` maps resources to their producer chains. The linter
cross-checks every building id against `fiefdom_building_types.json`.

## Selection and styles

Candidate bands are checked in order:

1. Safety candidates with `critical` or `problem` severity.
2. Continuity candidates with `problem` severity.
3. Everything else.

Within the first non-empty band, score = severity weight × category weight ×
action weight. Severity weights are 1000/100/10/1. Road candidates linked to a
building intent are multiplied by `road_first_weight` (`> 1` favors road first,
`< 1` favors the building first). Ties are broken by category order, candidate
id, and target.

The shipped style is `steady`. Future styles (`pro_growth`, `pro_upgrade`,
`self_sufficiency`) are additional config presets; the engine does not change.
Style-specific wording is deferred.

## Feasibility and holds

- Build: `ActionRegistry::validate("build", ...)` plus
  `Validation::hasEnoughResources` (import-aware, fungible money).
- Upgrade/convert: the registry validators, which already check resources.
- Road link: every cell is validated as a road build; the total cost is checked
  once.
- Reserve/import: resource name and value checks.
- Output rate: the building's config output, unlock level, and 0..1 range.

A failed feasibility check becomes a hold with a `reason_text_id` and a
`reeve_rec_hold_*` message. The recommendation card points at the treasury or
the relevant card.

## Placement and roads

- The site scan starts at the manor centre and searches outward to
  `hard_limits.max_site_scan_radius`.
- Reserved corridors are `x % corridor_spacing == 0 || y % corridor_spacing ==
  0`. Buildings prefer non-corridor sites; roads may use corridors.
- Dangling road ends are tried first, then sites are scored by road adjacency,
  corridor overlap, and distance from the manor. Ties break by distance, y, x.
- Road links use a deterministic BFS from the nearest existing road (or the
  manor's perimeter if no road exists) to the target footprint. The path avoids
  occupied cells, river cells, and walls, prefers corridor cells, and is capped
  by the style's `max_road_link` and the hard cap.
- The client builds a road link cell-by-cell in order and stops on the first
  failure; partial roads remain and the next report re-plans.

## Text and UI

The server sends `{ text_id, args }`. Args are numbers or `{ text_id }`
references. `client/src/lib/reeve_text.ts` collects ids, resolves nested
references, and substitutes tokens. `ReeveText.svelte` renders Markdown; the
`ReevePanel` renders the headline, sections, details, and one recommendation.

The panel supports:

- **Take suggestion** — executes the action through the normal endpoints;
- **Place manually** — preloads the suggested building ghost or shows the
  road-link path for confirmation;
- drill-down clicks on sections/details to the economy/production panel or a
  building card.

The building card reads `economy_report.building_status` and shows outputs,
per-output rates, input supply, and water-power warnings.

## Officials seam

The report header is `{ "advisor": { "style_tag": "steady" } }`. An appointed
official can later supply a name, portrait, and style without changing the
report/action contract. V1 uses a neutral interim Reeve and config-selected
style.
