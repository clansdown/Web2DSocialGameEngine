# `/api/getBaronies`

Fetches all available baronies that a player can join.

**Requires authentication.**

## Request

```json
{
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
        "baronies": [
            {
                "id": 1,
                "name": "Stormhold",
                "description": "A fortress in the north",
                "owner_character_id": 1,
                "owner_name": "CloudDragon",
                "member_count": 5,
                "created_at": 1700000000,
                "baron_character_id": 7,
                "baron_name": "IronFalcon"
            }
        ]
    }
}
```

## Notes

- Returns all baronies regardless of membership
- `owner_character_id`/`owner_name` is the immutable founder; `baron_character_id`/`baron_name`
  is the current baron, present only once a barony has been promoted (may be absent for baronies
  created before baron tracking was added)
- Use `/api/joinBarony` to join one
