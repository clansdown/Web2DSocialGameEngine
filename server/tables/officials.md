# officials Table

Stores officials serving within a fiefdom. Each official has a specific role that contributes to fiefdom management and operations.

## Official Roles

| Role | Purpose |
|------|---------|
| Bailiff | Manages fiefdom administration and justice |
| Wizard | Handles magical research and mana resources |
| Architect | Plans construction and improvements |
| Steward | Manages treasury and resources |
| Reeve | Oversees local affairs and peasant welfare |
| Beadle | Religious and ceremonial duties |
| Constable | Maintains order and defense |
| Forester | Manages wood and wilderness resources |

## Schema

```sql
CREATE TABLE officials (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    role TEXT NOT NULL,
    portrait_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    level INTEGER NOT NULL DEFAULT 1,
    intelligence INTEGER NOT NULL,
    charisma INTEGER NOT NULL,
    wisdom INTEGER NOT NULL,
    diligence INTEGER NOT NULL,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique official identifier |
| fiefdom_id | INTEGER | NOT NULL FK | Parent fiefdom (fiefdoms.id) |
| role | TEXT | NOT NULL | Role type (lowercase string: "bailiff", "wizard", etc.) |
| portrait_id | INTEGER | NOT NULL | Visual asset reference for character portrait |
| name | TEXT | NOT NULL | Official's personal name |
| level | INTEGER | NOT NULL DEFAULT 1 | Official's experience level |
| intelligence | INTEGER | NOT NULL | Mental acuity (0-255) |
| charisma | INTEGER | NOT NULL | Leadership and persuasion (0-255) |
| wisdom | INTEGER | NOT NULL | Judgment and insight (0-255) |
| diligence | INTEGER | NOT NULL | Work ethic and reliability (0-255) |

## Indexes

- Index on `fiefdom_id` for official lookups by fiefdom
- Index on `role` for role-based queries

## Relationships

- Many-to-one with `fiefdoms` via `fiefdom_id` foreign key
- Each fiefdom can have multiple officials (one per role type recommended)

## Stat Ranges

All stats (intelligence, charisma, wisdom, diligence) use unsigned 8-bit range:
- **Minimum**: 0 (no ability)
- **Maximum**: 255 (peak ability)
- **Typical range**: 10-200 for normal officials

Stats affect official performance in their role:
- **Intelligence**: Wizard research speed, Architect planning
- **Charisma**: Bailiff justice resolution, Beadle ceremonies
- **Wisdom**: Steward resource management, Reeve welfare
- **Diligence**: Constable vigilance, Forester patrols

## Usage Examples

Fetching officials for a fiefdom:

```cpp
struct OfficialData {
    int id;
    std::string role;
    int portrait_id;
    std::string name;
    int level;
    uint8_t intelligence;
    uint8_t charisma;
    uint8_t wisdom;
    uint8_t diligence;
};

std::vector<OfficialData> officials;
db << R"(
    SELECT id, role, portrait_id, name, level, intelligence, charisma, wisdom, diligence
    FROM officials WHERE fiefdom_id = ?;
)" << fiefdom_id
>> [&](int id, std::string role, int portrait_id, std::string name, int level,
       int intelligence, int charisma, int wisdom, int diligence) {
    officials.push_back({id, role, portrait_id, name, level,
                        static_cast<uint8_t>(intelligence),
                        static_cast<uint8_t>(charisma),
                        static_cast<uint8_t>(wisdom),
                        static_cast<uint8_t>(diligence)});
};
```

Creating a new official:

```cpp
db << R"(
    INSERT INTO officials (fiefdom_id, role, portrait_id, name, level, intelligence, charisma, wisdom, diligence)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
)" << fiefdom_id << "wizard" << portrait_id << "Merlin" << 5 << 180 << 120 << 200 << 150;
```

## Implementation Notes

- Role strings are stored lowercase for consistency
- Stats are stored as INTEGER but conceptually 0-255
- Portrait ID references art assets (not yet implemented)
- One official per role recommended but not enforced by schema