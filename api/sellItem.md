# `/api/sellItem`

Sells an item at the config **sell discount** (`retinue.json sell_discount`):
`base_value × sell_discount` gold is credited to the fiefdom wallet.

- `kind: "armory"` — sells a specific armory row (`armory_id`); it must be
  **unequipped**.
- `kind: "storage"` — sells one unit of a general-storage item (`item_id`).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "kind": "armory",
    "armory_id": 7,
    "item_id": "",
    "auth": { "username": "player", "token": "session_token_hex" }
}
```

```json
{
    "character_id": 1,
    "kind": "storage",
    "armory_id": 0,
    "item_id": "treatise_on_smithing",
    "auth": { "username": "player", "token": "session_token_hex" }
}
```

## Success Response

```json
{ "status": "ok", "data": { "gold": 5 } }
```

## Error Responses

```json
{ "error": "kind must be 'armory' or 'storage'." }
{ "error": "Armory item not found." }
{ "error": "Sell items must be unequipped first." }
{ "error": "You need a manor." }
```

## Notes

- Selling is the disposal release valve for finite storage: sharp discount by
  design. The same item-identity + value model will later power barony trade
  (sell-to-town is the degenerate case).