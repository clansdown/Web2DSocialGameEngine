# fiefdom_river Table

Stores the river cells for each fiefdom's manor board. The river is the water
source for the manor's water-power infrastructure: a **mill pond** must be placed
adjacent to a river cell, **head races** carry water from the pond to water-powered
buildings, and **tail races** drain back into the river (see
`server/docs/fiefdom_building_types.md` — Water Power).

## Schema

```sql
CREATE TABLE fiefdom_river (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    x INTEGER NOT NULL,
    y INTEGER NOT NULL,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id),
    UNIQUE(fiefdom_id, x, y)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique river cell identifier |
| fiefdom_id | INTEGER | NOT NULL FK | Parent fiefdom (fiefdoms.id) |
| x | INTEGER | NOT NULL | X grid coordinate (same cell space as buildings) |
| y | INTEGER | NOT NULL | Y grid coordinate |

## Indexes

- Index on `fiefdom_id` for river lookups by fiefdom

## Relationships

- Many-to-one with `fiefdoms` via `fiefdom_id`
- Each fiefdom has exactly one river layout (seeded once, stored as cells)

## Seeding

Rivers are seeded **lazily** by `FiefdomFetcher::ensureFiefdomRiver` (called from
`/api/getFiefdom` and the economy tick), so existing fiefdoms get a river too:

1. Load templates from `game/config/manor_river.json`. Each template is a
   meandering polyline (`points`) with a band `width`, authored in one canonical
   corner (southwest: entering the west edge, exiting the south edge).
2. Choose deterministically per fiefdom:
   - `template = fiefdom_id % templates.size()`
   - `rotation = (fiefdom_id / templates.size()) % 4` → 0/90/180/270 degrees
     about the manor origin `(0,0)`, mapping the template onto all four corners.
3. Expand the polyline to cells (Bresenham segments, thick perpendicular to the
   dominant axis by `width`) and insert the rotated cells.

The same fiefdom always gets the same river; different fiefdoms vary.

## Rules

- Buildings **may not overlap** river cells (`/api/Build` rejects placement on
  the river with `invalid_location`).
- A mill pond is an *active* water source only when its footprint **edge-touches**
  a river cell (it may not overlap the river).
- A tail race is a valid drain only when connected to a river cell.
