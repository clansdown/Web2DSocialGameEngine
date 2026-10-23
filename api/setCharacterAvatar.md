# `/api/setCharacterAvatar`

Sets the character's **safe-pool** PFP (the player representative image). This
is the character-creation pick and the "safe portrait" action in the hub
**Profile** activity. The safe pick is always present and is what minors (and
any viewer when no custom upload exists) see.

The change is **rate-limited to once per 24 hours per character**
(`characters.avatar_changed_at`). The very first pick is exempt because the
timestamp starts at 0, so character creation is never blocked.

**Requires authentication** and ownership of the character. No gameplay effect
(variety only).

## Request

```json
{
    "character_id": 1,
    "safe_id": "3.png",
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

`safe_id` must be an id from the character's **own gender pool** (what
`/api/getAvatarPool` returns for that sex, e.g. `male/1.png`). The server
validates against the character's stored `sex` — the request carries no gender.

## Success Response

```json
{
    "status": "ok",
    "data": {
        "avatar_safe_id": "3.png",
        "avatar_changed_at": 1750006500,
        "avatar_url": "/images/avatars/safe/3.png"
    }
}
```

## Error Responses

```json
{ "error": "Unknown avatar." }
{ "error": "You may only change your portrait once every 24 hours (12h remaining)." }
{ "error": "Character does not belong to this user" }
{ "needs_auth": true }
```

## Notes

- Setting a safe avatar does not remove a custom upload; resolution prefers the
  custom image for eligible 18+ viewers (`avatar_custom_id` stays set).
- The safe pools are gender-split (`images/avatars/safe/{male,female}/`); a
  character only ever picks from their own sex's pool.
- `avatar_url` is server-resolved for the acting (self) viewer: an 18+ custom
  image is returned as `/api/avatar/<character_id>` (the client appends its
  session credentials), otherwise the safe image URL.
- See `docs/retinue_design.md` §8 and `server/tables/characters.md`.
