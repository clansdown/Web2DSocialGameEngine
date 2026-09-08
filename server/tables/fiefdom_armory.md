# `fiefdom_armory`

The fiefdom's **gear storage** — a slot-based armory holding every non-basic
gear item a retinue member owns but is not currently wearing. Items enter from
forge orders (`craftGear`), city purchases (`buyGearCity`), and event rewards;
members **equip** items from the armory and **de-equip** gear back into it.
**Basic kits** (per-class baseline gear) are invisible and never occupy armory
slots.

## Schema

```sql
CREATE TABLE fiefdom_armory (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    item_id TEXT NOT NULL,
    member_id INTEGER,
    created_at INTEGER NOT NULL,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

## Fields

| Field | Type | Purpose |
|---|---|---|
| `id` | int | Primary key — one row per physical item |
| `fiefdom_id` | int | Owning fiefdom |
| `item_id` | text | Key into the equipment config (`game/config/equipment.json` — describes slot, member-level requirement, upkeep, `armory_slots`, value, craft/purchase data) |
| `member_id` | int \| null | Which `retinue_members.id` currently has the item equipped (`NULL` = in storage) |
| `created_at` | int | Unix timestamp |

## Indexes

- `idx_fiefdom_armory_fiefdom` on `fiefdom_id`
- `idx_fiefdom_armory_member` on `member_id`

## Relationships

- `fiefdoms.id` → `fiefdom_armory.fiefdom_id`
- `retinue_members.id` → `fiefdom_armory.member_id`

## Capacity & Overflow

- Capacity is a per-fiefdom **slot budget** (config, e.g. `armory_capacity` in
  `retinue.json`); each item occupies `armory_slots` slots.
- Player-initiated adds (forge, city buy) are blocked when the armory is at
  capacity, but **grants/rewards always land even over cap** — the fiefdom must
  clear space (sell/assign) before new crafting or purchases can start.
- Disposal is **sell-at-discount** (config `sell_discount`), proceeds to the
  fiefdom wallet. Sold items must be un-equipped.