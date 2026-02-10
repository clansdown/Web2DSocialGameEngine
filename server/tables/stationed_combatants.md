# stationed_combatants Table

Stores combatant instances stationed in fiefdoms with current level.

## Schema

```sql
CREATE TABLE stationed_combatants (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    combatant_config_id TEXT NOT NULL,
    level INTEGER NOT NULL DEFAULT 1,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique record identifier |
| fiefdom_id | INTEGER | NOT NULL FK | Owning fiefdom (fiefdoms.id) |
| combatant_config_id | TEXT | NOT NULL | Config key from player_combatants.json (e.g., "spearman") |
| level | INTEGER | NOT NULL DEFAULT 1 | Combatant current level |

## Indexes

- Index on `fiefdom_id` for combatant lookups

## Relationships

- Many-to-one with `fiefdoms` via `fiefdom_id` foreign key
- Many-to-one with config combatants via `combatant_config_id`

## Notes

- `combatant_config_id` references keys in `config/player_combatants.json`
- Level determines stats and morale boost from combatant config
- Morale calculation: look up combatant by config_id, get morale_boost array, index by level

## Usage Example

```cpp
std::vector<StationedCombatant> combatants = fetchStationedCombatants(fiefdom_id);

for (const auto& combatant : combatants) {
    auto combatant_opt = CombatantRegistry::getInstance().getPlayerCombatant(combatant.combatant_config_id);
    if (combatant_opt && !combatant_opt->morale_boost.empty()) {
        int idx = std::min(combatant.level - 1, static_cast<int>(combatant_opt->morale_boost.size()) - 1);
        double morale_contribution = combatant_opt->morale_boost[idx];
    }
}
```