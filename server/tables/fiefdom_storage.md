# `fiefdom_storage`

The fiefdom's **general item storage** — a count-based storeroom for non-gear
items. Today that means **books/scrolls** (consumable XP grants for the
smithing tech tree); tomorrow it also holds potions, alchemist reagents, or any
other stackable consumable item. This is deliberately a *generic* item storage,
not a "books" table.

## Schema

```sql
CREATE TABLE fiefdom_storage (
    fiefdom_id INTEGER NOT NULL,
    item_id TEXT NOT NULL,
    count INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(fiefdom_id, item_id),
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

## Fields

| Field | Type | Purpose |
|---|---|---|
| `fiefdom_id` | int | Owning fiefdom |
| `item_id` | text | Key into the items config (`game/config/items.json` — category, source `drop`/`purchase`, and a typed effect such as `grant_xp` with `training_duration`) |
| `count` | int | How many of this item the fiefdom holds |

## Relationships

- `fiefdoms.id` → `fiefdom_storage.fiefdom_id`

## Usage Notes

- Capacity is a count budget across all items (config, generous). Items may
  also be **sold at discount** (`sell_discount`) — the same item+value transfer
  primitive that will later power barony-to-barony trade.
- **Books**: consumed by targeting a tech-capable building (applying starts a
  short `training_duration` timer; on completion the building gains `xp`).
  Teachers are *not* stored here — they are hired on demand and applied
  directly (see `retinue.json` `training`).
- Only **one training (any kind) per building** can run at a time. If the
  building is demolished mid-training the consumable is lost.