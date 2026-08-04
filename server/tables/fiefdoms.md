# fiefdoms Table

Stores character territory holdings with their locations, resources, and defensive infrastructure.

## Schema

```sql
CREATE TABLE fiefdoms (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    x INTEGER NOT NULL,
    y INTEGER NOT NULL,
    peasants INTEGER NOT NULL DEFAULT 0,
    gold INTEGER NOT NULL DEFAULT 0,
    silver_pence INTEGER NOT NULL DEFAULT 0,
    grain INTEGER NOT NULL DEFAULT 0,
    wood INTEGER NOT NULL DEFAULT 0,
    steel INTEGER NOT NULL DEFAULT 0,
    bronze INTEGER NOT NULL DEFAULT 0,
    stone INTEGER NOT NULL DEFAULT 0,
    leather INTEGER NOT NULL DEFAULT 0,
    mana INTEGER NOT NULL DEFAULT 0,
    charcoal INTEGER NOT NULL DEFAULT 0,
    iron INTEGER NOT NULL DEFAULT 0,
    ironwork INTEGER NOT NULL DEFAULT 0,
    fancy_ironwork INTEGER NOT NULL DEFAULT 0,
    wall_count INTEGER NOT NULL DEFAULT 0,
    morale REAL NOT NULL DEFAULT 0,
    last_update_time INTEGER NOT NULL DEFAULT 0,
    manor_level INTEGER NOT NULL DEFAULT 1,
    import_settings TEXT NOT NULL DEFAULT '...',
    reserves TEXT NOT NULL DEFAULT '{}',
    FOREIGN KEY(owner_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique fiefdom identifier |
| owner_id | INTEGER | NOT NULL FK | Owning character (characters.id) |
| name | TEXT | NOT NULL | Fiefdom name |
| x | INTEGER | NOT NULL | X coordinate in world |
| y | INTEGER | NOT NULL | Y coordinate in world |
| peasants | INTEGER | NOT NULL DEFAULT 0 | Population count |
| gold | INTEGER | NOT NULL DEFAULT 0 | Treasury currency (building costs, non-penny imports) |
| silver_pence | INTEGER | NOT NULL DEFAULT 0 | Silver-pence wallet (mini-game rewards + penny-market resource sales) |
| grain | INTEGER | NOT NULL DEFAULT 0 | Food resource |
| wood | INTEGER | NOT NULL DEFAULT 0 | Building material |
| steel | INTEGER | NOT NULL DEFAULT 0 | Military material |
| bronze | INTEGER | NOT NULL DEFAULT 0 | Alloy material |
| stone | INTEGER | NOT NULL DEFAULT 0 | Construction material |
| leather | INTEGER | NOT NULL DEFAULT 0 | Crafting material |
| mana | INTEGER | NOT NULL DEFAULT 0 | Magical resource |
| charcoal | INTEGER | NOT NULL DEFAULT 0 | Fuel resource (produced by collier, consumed by blacksmith) |
| iron | INTEGER | NOT NULL DEFAULT 0 | Ore resource (consumed by blacksmith; produced by the bloomery) |
| ironwork | INTEGER | NOT NULL DEFAULT 0 | Forged metal tools (produced by blacksmith, consumed by building upkeep and unit upkeep) |
| fancy_ironwork | INTEGER | NOT NULL DEFAULT 0 | Fine tempered iron (produced by blacksmith level 2+ via its `fancy_ironwork` output) |
| wall_count | INTEGER | NOT NULL DEFAULT 0 | Defensive wall layers |
| morale | REAL | NOT NULL DEFAULT 0 | Fiefdom morale score | Range: -1000 (disastrous) to 1000 (inspired). Default 0 (neutral). Affects bonuses for production, building speed, combat, etc. |
| last_update_time | INTEGER | NOT NULL DEFAULT 0 | Unix timestamp (seconds) when production updates were last applied. Used to calculate pending resource production since last update. |
| manor_level | INTEGER | NOT NULL DEFAULT 1 | Manor house upgrade level. Controls building type unlocks and max building count. Upgraded via build endpoint. |

## Indexes

- Index on `owner_id` for character fiefdom lookups

## Relationships

- Many-to-one with `characters` via `owner_id` foreign key
- One-to-many with `fiefdom_buildings` via `fiefdom_id` foreign key
- One-to-many with `officials` via `fiefdom_id` foreign key

## Notes

- x and y coordinates represent grid world position
- Resources use INTEGER (64-bit on most systems) for large stockpiles
- `gold` is fractional-capable: the economy tick writes fractional values (e.g. from production or a 0.8 building cost) and the server reads it as a double, so gold can hold non-integer amounts (e.g. 0.8).
- `silver_pence` is the penny-market wallet. Mini-game rewards credit it directly, and grain — whose `import_prices` value is a money object (`{ "shillings": 1 }`) — imports from it and auto-sells to it at 50% of import price (6 pence per grain). Non-penny resources still trade in gold.
- `reserves` stores per-resource reserve amounts (JSON object). Each tick, any resource above its
  reserve is auto-sold at 50% of the import price; amounts at/below reserve are kept. Set via
  `/api/setFiefdomReserve`.
- wall_count represents number of defensive layers around the manor
- Accessed by `/api/getFiefdom` endpoint

## Resource Ranges

| Resource | Typical Range | Purpose |
|----------|---------------|---------|
| peasants | 0 - millions | Population and labor |
| gold | 0 - billions | Currency and trade |
| grain | 0 - millions | Food for population |
| wood | 0 - millions | Building construction |
| steel | 0 - millions | Military equipment |
| bronze | 0 - millions | Artifacts and tools |
| stone | 0 - millions | Fortification building |
| leather | 0 - millions | Equipment and armor |
| mana | 0 - millions | Magic research |
| charcoal | 0 - millions | Fuel for smelting |
| iron | 0 - millions | Ore for smelting |
| ironwork | 0 - millions | Military equipment |
| wall_count | 0 - 100 | Defensive strength |
| morale | -1000 to 1000 | Fiefdom morale rating |

## Usage Examples

Fetching complete fiefdom data:

```cpp
FiefdomData fiefdom;
db << R"(
    SELECT owner_id, name, x, y, peasants, gold, grain, wood, steel, bronze, stone, leather, mana, wall_count, morale
    FROM fiefdoms WHERE id = ?;
)" << fiefdom_id
>> [&](int owner_id, std::string name, int x, int y,
       int peasants, int gold, int grain, int wood, int steel,
       int bronze, int stone, int leather, int mana, int wall_count, double morale) {
    fiefdom.id = fiefdom_id;
    fiefdom.owner_id = owner_id;
    fiefdom.name = name;
    fiefdom.x = x;
    fiefdom.y = y;
    fiefdom.peasants = peasants;
    fiefdom.gold = gold;
    fiefdom.grain = grain;
    fiefdom.wood = wood;
    fiefdom.steel = steel;
    fiefdom.bronze = bronze;
    fiefdom.stone = stone;
    fiefdom.leather = leather;
    fiefdom.mana = mana;
    fiefdom.wall_count = wall_count;
    fiefdom.morale = morale;
};
```