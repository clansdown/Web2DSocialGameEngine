# Resource Production Calculations

## Fixed 1-Day Period

All production and consumption share a single fixed **1-day period**. A resource spec's `amount`
is the quantity produced (or consumed) **per day**.

```
produced = amount × (elapsed_seconds / 86400)
```

Fractional elapsed days are used directly — nothing is floored to whole cycles, so any elapsed
time beyond a tiny minimum (≈1 second, to avoid rounding errors) yields a proportional amount.

## Example

A building with `grain: { amount: 10 }`:

| Elapsed | Amount Produced |
|---------|-----------------|
| 12 hours | 5.0 |
| 1 day | 10.0 |
| 2.5 days | 25.0 |

## Config File Format

Buildings define production in `fiefdom_building_types.json`:

```json
{
  "farm": {
    "grain": {
      "amount": 10
    }
  }
}
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| amount | number | Quantity produced/consumed per 1-day period |

> The legacy `amount_multiplier`, `periodicity`, and `periodicity_multiplier` per-cycle fields are
> removed. Building-to-building production boosts live in the `modifiers` array.

## Production Update Flow

1. Server receives a time update for a fiefdom
2. Calculate elapsed days = (now - last_update_time) / 86400
3. For each fiefdom:
   a. Load all buildings with level > 0
   b. For each building, check if it has resource production
   c. Calculate produced amount = amount × elapsed_days (fractional, no flooring)
   d. Apply input gating, then update fiefdom resources
   e. Record production diff for audit
4. Mark fiefdoms as updated
5. Return production summary

## Edge Cases

- Building with level 0 (under construction): No production
- `amount = 0` or missing: Produces nothing
- Very large elapsed time: Production grows linearly (no exponential blowup)
- Elapsed time below ~1 second: Skipped to avoid rounding errors

## Consumption

Consumption shares the same 1-day period and fractional-day scaling:
- Building `inputs` (`fiefdom_building_types.json`) — per-day, input-gated
- Building `daily_cost` — per-day flat rate
- `economy.json` `population_costs` — per-day per population unit
- `player_combatants.json` `upkeep` — per-day per stationed combatant

## Per-Output Gating

Buildings using the `outputs` array produce each output independently. Every
output has its **own** inputs and is scaled by its own satisfaction ratio:

```
output_ratio = min over the output's inputs of (supplied / required)
produced     = output.amount × days_elapsed × modifier × player_rate × output_ratio
```

- `min_level`: outputs below the building's level are inactive (nothing consumed/produced).
- `player_rate`: 0..1 per output, set via `/api/setBuildingOutputRate`; scales both the
  output amount and that output's inputs. Rate 0 disables the output.
- Legacy flat `<resource>: { amount }` + building-level `inputs` is normalized to a single
  output per resource with shared inputs, so existing buildings behave identically.

## Penny Market (imports & exports)

`economy.json` `import_prices` values are either:
- **Plain number** — a gold price. Shortfalls auto-import from the fiefdom's gold,
  and excess above reserve auto-sells to gold at the export price.
- **Money object** `{ gold, shillings, pence }` — a penny-market resource paid in
  silver pence. Imports deduct from `fiefdoms.silver_pence`; excess above reserve
  auto-sells to `silver_pence` at the export price.

The per-unit **export price** (what excess above the reserve sells for) resolves
with precedence:
1. `export_prices[resource]` — an explicit sell price (plain number = gold, money
   object = pence). No multiplier applied.
2. `export_sell_multipliers[resource]` — a per-resource ratio of the import price.
3. Global `export_sell_multiplier` (default 0.5) — ratio of the import price.

Example — grain (money-form): imported at 1 shilling (12 pence), sold at 6 pence:

```
import: silver_pence -= ceil-ish units × 12
sell:   silver_pence += llround(excess × 12 × 0.5)   // 6 pence per unit
```

The economy report exposes `net_silver` (pence exports − pence imports) and, for
grain, an `exported["grain"] = { amount, pence }` entry instead of `gold`.
