# Resource Production Calculations

## Periodicity Formula

Production amounts scale exponentially with time based on building configuration.

Given:
- Periodicity (hours): P
- Amount per cycle: A
- Amount multiplier per cycle: M
- Elapsed hours: H

Cycles = floor(H / P)

Total Amount = A × M^(cycles-1) + A × M^(cycles-2) + ... + A × M^0

This is the sum of a geometric series:
Total = A × (M^cycles - 1) / (M - 1)    (when M ≠ 1)
Total = A × cycles                        (when M = 1)

## Example

Farm with periodicity 60 hours, amount 10, multiplier 1.2:

| Hours | Cycles | Formula | Amount Produced |
|-------|--------|---------|-----------------|
| 60    | 1      | 10 × 1.2^0 | 10 |
| 120   | 2      | 10 × (1 + 1.2) | 22 |
| 180   | 3      | 10 × (1 + 1.2 + 1.44) | 36.4 |
| 3600  | 60     | 10 × (1.2^60 - 1) / 0.2 | ~114,400 |

Note: 1 hour = 3600 seconds.

## Config File Format

Buildings define production in `fiefdom_building_types.json`:

```json
{
  "farm": {
    "grain": {
      "amount": 10,
      "amount_multiplier": 1.2,
      "periodicity": 60,
      "periodicity_multiplier": 1.05
    }
  }
}
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| amount | number | Base resource per production cycle |
| amount_multiplier | number | Growth factor per additional cycle (default: 1.0) |
| periodicity | number | Hours between production cycles |
| periodicity_multiplier | number | Speed increase per building level (default: 1.0) |

## Production Update Flow

1. Server receives time update request with `last_update_time`
2. Calculate elapsed hours = (now - last) / 3600
3. For each fiefdom:
   a. Load all buildings with level > 0
   b. For each building, check if it has resource production
   c. Calculate cycles completed: floor(elapsed / periodicity)
   d. If cycles > 0:
      - Calculate total production using geometric series formula
      - Update fiefdom resources
      - Record production diff for audit
4. Mark fiefdoms as updated
5. Return production summary

## Edge Cases

- Building with level 0 (under construction): No production
- Periodicity = 0: Building produces continuously
- amount_multiplier = 0: Only first cycle produces
- Very large elapsed time: Result grows exponentially

## Optimization Notes

Production calculation for large time spans should:
- Use logarithmic formulas to avoid overflow
- Clamp to reasonable maximums
- Consider batch processing for many fiefdoms