# `POST /api/getReeveReport`

Returns the manor Reeve's structured overview and exactly one recommendation for
the current fiefdom.

The endpoint runs the same lazy economy tick as `/api/getFiefdom` to bring the
fiefdom up to date, then projects one full day through the real economy engine
in a rolled-back transaction. It computes a deterministic report from that
snapshot. It does **not** persist advice or intent.

## Request

```json
{
  "auth": {
    "username": "player_one",
    "token": "session-token"
  },
  "character_id": 12
}
```

`fiefdom_id` may be supplied instead of `character_id`. The fiefdom must belong
to the authenticated user (the same ownership check as `/api/getFiefdom`).

## Response

```json
{
  "status": "ok",
  "data": {
    "advisor": { "style_tag": "steady" },
    "headline": { "text_id": "reeve_headline_watch", "args": {} },
    "sections": [
      {
        "id": "stores",
        "title": { "text_id": "reeve_section_stores", "args": {} },
        "status": {
          "text_id": "reeve_status_stores_low",
          "args": {
            "resource": { "text_id": "ui_resource_grain" },
            "days": 2
          }
        },
        "severity": "problem",
        "details": [
          {
            "text": {
              "text_id": "reeve_detail_resource_balance",
              "args": {
                "resource": { "text_id": "ui_resource_grain" },
                "stock": 40,
                "net": -12,
                "days": 2
              }
            },
            "severity": "problem",
            "target": { "kind": "resource", "resource": "grain" }
          }
        ],
        "target": { "kind": "economy_panel" }
      }
    ],
    "recommendation": {
      "kind": "action",
      "id": "growth_deficit_grain",
      "category": "growth",
      "severity": "problem",
      "text": {
        "text_id": "reeve_rec_build_stage",
        "args": { "building": { "text_id": "ui_building_villein" } }
      },
      "why": {
        "text_id": "reeve_why_resource_deficit",
        "args": {
          "resource": { "text_id": "ui_resource_grain" },
          "days": 2,
          "net": -12
        }
      },
      "action": {
        "kind": "build",
        "building_type": "villein",
        "x": 12,
        "y": 8
      },
      "target": { "kind": "build_palette", "building_type": "villein" }
    }
  }
}
```

## Text model

The server sends **no display strings**. Every human-readable line is a
`{ text_id, args }` pair:

- `text_id` names a file in `game/text/<lang>/`.
- `args` values are either numbers or `{ "text_id": "..." }` references.
- The client resolves nested text ids before applying `{token}` substitution.

## Sections

Each section has a stable `id`, a `title`, a `status`, a `severity`, optional
`details`, and a `target` for drill-down.

Current section ids:

- `treasury`
- `stores`
- `production`
- `land`
- `water`
- `morale`
- `retinue`
- `trade`

## Recommendation kinds

- `action` — one feasible action, with `action` populated.
- `hold` — the preferred action is infeasible; the report explains why in
  `reason_text_id` and deliberately does **not** fall through to a cheaper
  candidate.
- `none` — nothing is worth doing.

## Action union

```text
{ "kind": "build", "building_type": "villein", "x": 12, "y": 8 }
{ "kind": "upgrade", "building_id": 42 }
{ "kind": "convert", "building_id": 42 }
{ "kind": "build_road_link", "cells": [ { "x": 3, "y": 4 }, ... ] }
{ "kind": "set_reserve", "resource": "grain", "value": 150 }
{ "kind": "set_import", "resource": "iron", "enabled": true }
{ "kind": "set_output_rate", "building_id": 42, "output": "ironwork", "rate": 1.0 }
```

A road link is one logical action with an ordered cell list; the client builds
the cells sequentially and stops on the first failure (partial roads remain and
the next report re-plans).

## Target union

```text
{ "kind": "building_card", "building_id": 42 }
{ "kind": "economy_panel" }
{ "kind": "production_panel" }
{ "kind": "resource", "resource": "grain" }
{ "kind": "build_palette", "building_type": "villein" }
```

## Hold reasons

`reason_text_id` is one of the `reeve_error_*` text ids:

- `reeve_error_insufficient_resources`
- `reeve_error_insufficient_arable_land`
- `reeve_error_insufficient_forest_land`
- `reeve_error_prerequisites_not_met`
- `reeve_error_exclusive_building_exists`
- `reeve_error_max_per_fiefdom_reached`
- `reeve_error_home_base_required`
- `reeve_error_invalid_location`
- `reeve_error_unknown`

The accompanying `text` is a `reeve_rec_hold_*` message; it is informational and
points the player at the relevant card.

## Errors

- `fiefdom_id or character_id required`
- `fiefdom not found`
- `Fiefdom does not belong to this user`
- `needs_auth` when the request is unauthenticated

## Notes

- The report is computed on demand; there is no scheduler and no persisted
  advice.
- Feasibility reuses the same action validators as `/api/Build` plus the same
  import-aware affordability check.
- The `advisor.style_tag` is the officials seam: an appointed official can later
  supply a different style without changing this contract.
