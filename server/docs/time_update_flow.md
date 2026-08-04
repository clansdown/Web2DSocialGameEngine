# Time-Based Update Flow

## Trigger Conditions

Client calls `/api/getFiefdom` (or other fiefdom endpoints) which invokes the economy update via
`GameLogic::updateStateSince` using the fiefdom's stored `last_update_time`.

## Update Process

1. **Validate timestamp**
   - Check `last_update_time` is not in future
   - If future, return error or skip update

2. **Calculate elapsed time**
   ```
   hours_elapsed = (now - last_update_time) / 3600.0
   days_elapsed  = hours_elapsed / 24.0
   ```
   - Now is current epoch seconds
   - Result is floating point for fractional days

3. **Skip tiny updates**
   - If `hours_elapsed < 1/3600` (~1 second), skip update
   - Prevents unnecessary database writes and rounding errors

4. **Query fiefdoms needing updates**

5. **Process each fiefdom**
   a. Load fiefdom data
   b. Get all buildings with level > 0
   c. For each building:
      - Get building type config
      - Compute production: `amount × days_elapsed` (fractional, no flooring)
      - Compute inputs, `daily_cost`, population costs, and combatant upkeep the same way
      - Apply input-gated output scaling, imports, and sell-above-reserve
      - Update fiefdom resource columns
   d. Recalculate morale if needed
   e. Mark fiefdom as updated

6. **Record updates**
   - Update `fiefdom.last_update_time` to now
   - Return production summary with diffs

## Production Calculation

For each building with resource production:
```cpp
double days = (now - last_update_time) / 86400.0;   // fractional days
if (days <= 0) return 0.0;
produced = amount * days;
```

No flooring and no geometric compounding — production is a flat per-day rate.

## Return Value

```json
{
  "new_timestamp": 1757520000,
  "time_hours_elapsed": 24.5,
  "production_updates_applied": 150,
  "productions": [
    {
      "resource_type": "gold",
      "amount_produced": 125.5,
      "source_type": "building",
      "source_id": 5,
      "fiefdom_id": 1
    }
  ],
  "fiefdoms_updated": 3
}
```

## Performance Considerations

- Batch process fiefdoms in single query
- Use transactions for atomic updates
- Consider pagination for large numbers of fiefdoms
- Cache building configs to avoid repeated lookups

## Race Condition Prevention

- Use last_update_time as optimistic lock
- Check "WHERE last_update_time = ?" during update
- If rows affected = 0, another process updated first
- Retry or return conflict error
