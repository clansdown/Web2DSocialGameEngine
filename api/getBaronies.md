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
                "safe_name_id": "stormhold",
                "safe_tag_ids": ["laid_back", "pve_focused"],
                "tags": [
                    { "id": "laid_back", "name": "Laid back" },
                    { "id": "pve_focused", "name": "PvE focused" }
                ],
                "accepts_minors": true,
                "owner_character_id": 1,
                "owner_name": "CloudDragon",
                "owner_avatar_url": "/images/avatars/safe/male/3.png",
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
- **Defunct** baronies are excluded: when a founder leaves while they are the
  last member, the barony is tombstoned (`baronies.defunct_at > 0`), hidden from
  this list, and its name stays permanently reserved (see `/api/leaveBarony`)
- `owner_character_id`/`owner_name` is the immutable founder; `baron_character_id`/`baron_name`
  is the current baron, present only once a barony has been promoted (may be absent for baronies
  created before baron tracking was added)
- Names/descriptions are viewer-relative: a minor always receives the safe
  name and tags, never custom barony text. `owner_name`/`baron_name` are
  likewise projected.
- A minor may join only a barony with `accepts_minors = true`.
- Use `/api/joinBarony` to join one; use `/api/setBaronyAcceptsMinors` to
  change the flag as owner/admin.
