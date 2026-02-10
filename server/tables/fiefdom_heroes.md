# fiefdom_heroes Table

Stores hero instances owned by fiefdoms with current level.

## Schema

```sql
CREATE TABLE fiefdom_heroes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    hero_config_id TEXT NOT NULL,
    level INTEGER NOT NULL DEFAULT 1,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique record identifier |
| fiefdom_id | INTEGER | NOT NULL FK | Owning fiefdom (fiefdoms.id) |
| hero_config_id | TEXT | NOT NULL | Config key from heroes.json (e.g., "knight_hero") |
| level | INTEGER | NOT NULL DEFAULT 1 | Hero current level |

## Indexes

- Index on `fiefdom_id` for fiefdom hero lookups

## Relationships

- Many-to-one with `fiefdoms` via `fiefdom_id` foreign key
- Many-to-one with config heroes via `hero_config_id`

## Notes

- `hero_config_id` references keys in `config/heroes.json`, not a separate database table
- Level determines stats and morale boost from hero config
- Morale calculation: look up hero by config_id, get morale_boost array, index by level

## Usage Example

```cpp
std::vector<FiefdomHero> heroes = fetchFiefdomHeroes(fiefdom_id);

for (const auto& hero : heroes) {
    auto hero_opt = HeroRegistry::getInstance().getHero(hero.hero_config_id);
    if (hero_opt && !hero_opt->morale_boost.empty()) {
        int idx = std::min(hero.level - 1, static_cast<int>(hero_opt->morale_boost.size()) - 1);
        double morale_contribution = hero_opt->morale_boost[idx];
    }
}
```