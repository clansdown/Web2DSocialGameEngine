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
    grain INTEGER NOT NULL DEFAULT 0,
    wood INTEGER NOT NULL DEFAULT 0,
    steel INTEGER NOT NULL DEFAULT 0,
    bronze INTEGER NOT NULL DEFAULT 0,
    stone INTEGER NOT NULL DEFAULT 0,
    leather INTEGER NOT NULL DEFAULT 0,
    mana INTEGER NOT NULL DEFAULT 0,
    wall_count INTEGER NOT NULL DEFAULT 0,
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
| gold | INTEGER | NOT NULL DEFAULT 0 | Treasury currency |
| grain | INTEGER | NOT NULL DEFAULT 0 | Food resource |
| wood | INTEGER | NOT NULL DEFAULT 0 | Building material |
| steel | INTEGER | NOT NULL DEFAULT 0 | Military material |
| bronze | INTEGER | NOT NULL DEFAULT 0 | Alloy material |
| stone | INTEGER | NOT NULL DEFAULT 0 | Construction material |
| leather | INTEGER | NOT NULL DEFAULT 0 | Crafting material |
| mana | INTEGER | NOT NULL DEFAULT 0 | Magical resource |
| wall_count | INTEGER | NOT NULL DEFAULT 0 | Defensive wall layers |

## Indexes

- Index on `owner_id` for character fiefdom lookups

## Relationships

- Many-to-one with `characters` via `owner_id` foreign key
- One-to-many with `fiefdom_buildings` via `fiefdom_id` foreign key
- One-to-many with `officials` via `fiefdom_id` foreign key

## Notes

- x and y coordinates represent grid world position
- Resources use INTEGER (64-bit on most systems) for large stockpiles
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
| wall_count | 0 - 100 | Defensive strength |

## Usage Examples

Fetching complete fiefdom data:

```cpp
FiefdomData fiefdom;
db << R"(
    SELECT owner_id, name, x, y, peasants, gold, grain, wood, steel, bronze, stone, leather, mana, wall_count
    FROM fiefdoms WHERE id = ?;
)" << fiefdom_id
>> [&](int owner_id, std::string name, int x, int y,
       int peasants, int gold, int grain, int wood, int steel,
       int bronze, int stone, int leather, int mana, int wall_count) {
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
};
```