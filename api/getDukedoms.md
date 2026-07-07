# `/api/getDukedoms`

Fetches all available dukedoms that a player can join.

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
        "dukedoms": [
            {
                "id": 1,
                "name": "Stormhold",
                "description": "A fortress in the north",
                "owner_character_id": 1,
                "owner_name": "CloudDragon",
                "member_count": 5,
                "created_at": 1700000000
            }
        ]
    }
}
```

## Notes

- Returns all dukedoms regardless of membership
- Use `/api/joinDukedom` to join one
