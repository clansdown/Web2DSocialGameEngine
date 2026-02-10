# Time-Based Update Flow

## Trigger Conditions

Client calls `/api/updateState` with `last_update_time` or endpoint retrieves stored `last_update_time` per fiefdom.

## Update Process

1. **Validate timestamp**
   - Check `last_update_time` is not in future
   - If future, return error or skip update

2. **Calculate elapsed time**
   ```
   hours_elapsed = (now - last_update_time) / 3600.0
   ```
   - Now is current epoch seconds
   - 3600 converts seconds to hours
   - Result is floating point for fractional hours

3. **Skip tiny updates**
   - If `hours_elapsed < 0.001` (~3.6 seconds), skip update
   - Prevents unnecessary database writes

4. **Query fiefdoms needing updates**
   ```sql
   SELECT id FROM fiefdoms WHERE last_update_time < (now - hours_elapsed * 3600)
   ```

5. **Process each fiefdom**
   a. Load fiefdom data
   b. Get all buildings with level > 0
   c. For each building:
      - Get building type config
      - Check if building produces resources
      - If yes, calculate production cycles
      - Calculate total resource produced
      - Update fiefdom resource column
   d. Calculate total production for fiefdom
   e. Recalculate morale if needed
   f. Mark fiefdom as updated

6. **Record updates**
   - Update `fiefdom.last_update_time` to now
   - Return production summary with diffs

## Production Calculation

For each building with resource production:
```cpp
cycles = floor(hours_elapsed / periodicity);

if (cycles > 0) {
    // Geometric series sum
    total = amount * (pow(multiplier, cycles) - 1) / (multiplier - 1);
    
    // Or simpler case (multiplier == 1)
    total = amount * cycles;
}
```

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

## Future Enhancements

- Background processing for large updates
- Partial updates for timeouts
- Compression of production history
- Analytics for production trends