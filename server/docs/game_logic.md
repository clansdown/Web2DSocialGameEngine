# Game Logic Architecture

## Core Systems

### Action Registry
Centralized registry for all game actions with validation and execution.

### Time-Based State Updates
On-demand resource production and state progression based on elapsed time.

## Action Execution Flow

1. Client sends action request via `/api/executeAction`
2. `ActionRegistry` routes to appropriate handler
3. `validate()` checks game rules and returns error if invalid
4. `execute()`:
   - Starts database transaction
   - Re-validates to prevent race conditions
   - Deducts resources with diff tracking
   - Creates/modifies database records
   - Commits transaction or rolls back on error
   - Returns `ActionResult` with detailed diffs

## Time Update Logic

1. Calculate hours elapsed since `last_update_time`
2. For each fiefdom needing update:
   - For each building, calculate production cycles
   - Apply resource changes in transaction
   - Record production changes
3. Optionally recalculate morale
4. Mark fiefdoms as updated
5. Return production summary with diffs

## Building States

Buildings have the following states:

| State | level | construction_start_ts | action_tag |
|-------|-------|----------------------|------------|
| Under construction | 0 | timestamp > 0 | "" |
| Operational | > 0 | timestamp > 0 | "idle" or action |
| In action | > 0 | timestamp > 0 | e.g., "training" |

## Action Categories

### Production Actions
- `build`: Construct new buildings (starts at level 0)
- `build_wall`: Construct defensive walls

### Training Actions (STUB)
- `train_troops`: Create combatants at barracks

### Research Actions (STUB)
- `research_magic`: Unlock magical abilities
- `research_tech`: Unlock technologies

## Resource Types

| Type | Description |
|------|-------------|
| gold | Currency |
| grain | Food |
| wood | Building material |
| steel | Military equipment |
| bronze | Artifacts |
| stone | Fortification |
| leather | Equipment |
| mana | Magic research |

## Error Handling

All actions return `ActionResult` with:
- `status`: OK, FAIL, or PARTIAL
- `error_code`: Machine-readable error code
- `error_message`: Human-readable error message
- `side_effects`: Detailed diffs of all changes
- `action_timestamp`: When action was executed

## Transaction Safety

Actions use SQLite transactions to ensure atomicity:
- BEGIN TRANSACTION
- Validate and execute
- COMMIT on success
- ROLLBACK on failure