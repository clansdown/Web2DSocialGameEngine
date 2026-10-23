# `/api/getStore`

Returns the premium-variety store: the cosmetic catalog, bundles, directly-priced
products, the character's **Platinum** balance, and the set of cosmetics the
character already owns. Reads `game/config/cosmetics.json` (hot-reloadable) plus
the character's Platinum ledger and ownership rows.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": {
        "items": [
            {
                "id": "banner_emberfield",
                "name": "Emberfield Banner",
                "slot": "banner",
                "tier": "rare",
                "price_platinum": 450,
                "source": "store",
                "description": "Gules and or, the field of the burning autumn.",
                "appearance": { "palette": ["#7a2b21", "#c9a227"], "pattern": "quarterly", "emblem": "flame", "trim": "#d8d2b0" }
            }
        ],
        "bundles": [
            { "id": "bundle_emberfield_kit", "name": "Emberfield Kit", "price_platinum": 1300, "items": ["banner_emberfield", "knight_emberfield_plate", "flourish_ember_splash"] }
        ],
        "products": [
            { "id": "platinum_pouch", "name": "Pouch of Platinum", "price_real": 4.99, "platinum": 500, "kind": "platinum_pack" }
        ],
        "platinum": 500,
        "owned": ["banner_ravensworn", "uniform_muster_green"]
    }
}
```

| Field | Type | Description |
|---|---|---|
| `items` | array | Cosmetic catalog entries (appearance + `price_platinum` + `source`) |
| `bundles` | array | Grouped cosmetic purchases at a bundle price |
| `products` | array | Direct real-money products (Platinum packs, monthly pass) — see `/api/purchasePlatinum` |
| `platinum` | int | The character's Platinum balance |
| `owned` | string[] | Cosmetic ids the character owns (not revoked) |

## Error Responses

```json
{ "error": "Character does not belong to this user" }
```

(Standard auth errors only.)

## Notes

- Every item is validated by a **deny-by-default whitelist** in the config
  linter — a cosmetic can carry no gameplay key (`rts_design.md` §15.5.1).
- Platinum is a separate ledger and is **not fungible** with `gold`/`silver_pence`
  in either direction.
