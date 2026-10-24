# `/api/removeCustomAvatar`

Removes the character's custom (18+) PFP, reverting the display to the safe-pool
pick. The DB reference is cleared first, then the stored object is deleted on the
maintenance worker (local file or R2 object); R2 removals also purge the exact
Cloudflare URL when purge credentials are configured. Browser-cached copies are
accepted.

**Requires authentication**, ownership, and an **adult account**
(`adult_required`). Rate-limited to once per 24 hours per character.

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
        "result": "removed",
        "avatar_storage": "local",
        "avatar_changed_at": 1750006500,
        "avatar_url": "/images/avatars/safe/male/3.png"
    }
}
```

When there is nothing to remove the call is a no-op:

```json
{
    "status": "ok",
    "data": { "result": "no_custom", "avatar_url": "/images/avatars/safe/3.png" }
}
```

## Error Responses

```json
{ "error": "adult_required" }
{ "error": "avatar_locked" }
{ "error": "avatar_upload_in_progress" }
{ "error": "avatar_remove_failed" }
{ "error": "You may only change your portrait once every 24 hours (12h remaining)." }
{ "error": "Character does not belong to this user" }
```

## Notes

- The safe pick (`avatar_safe_id`) is untouched; it remains the fallback.
- See `/api/uploadCharacterAvatar` and `docs/retinue_design.md` §8.
