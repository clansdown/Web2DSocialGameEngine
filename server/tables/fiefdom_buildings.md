# fiefdom_buildings Table

Stores buildings constructed within a fiefdom. Buildings represent player-constructed infrastructure that contributes to fiefdom functionality.

## Schema

```sql
CREATE TABLE fiefdom_buildings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique building identifier |
| fiefdom_id | INTEGER | NOT NULL FK | Parent fiefdom (fiefdoms.id) |
| name | TEXT | NOT NULL | Building name |

## Indexes

- Index on `fiefdom_id` for building lookups by fiefdom

## Relationships

- Many-to-one with `fiefdoms` via `fiefdom_id` foreign key
- Each fiefdom can have zero or more buildings

## Notes

- Building names are user-defined (generic system)
- Buildings are fetched alongside fiefdom data via `/api/getFiefdom`
- Up to 255 buildings per fiefdom (SQLite INTEGER range)
- No building types enforced - names describe purpose

## Usage Examples

Fetching buildings for a fiefdom:

```cpp
std::vector<BuildingData> buildings;
db << "SELECT id, name FROM fiefdom_buildings WHERE fiefdom_id = ?;"
   << fiefdom_id
   >> [&](int id, std::string name) {
       buildings.push_back({id, name});
   };
```

Creating a new building:

```cpp
db << "INSERT INTO fiefdom_buildings (fiefdom_id, name) VALUES (?, ?);"
   << fiefdom_id << "Farm";
```