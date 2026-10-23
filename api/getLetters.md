# `/api/getLetters`

Returns the character's in-game letters (the king's missives and future
notices), newest first. A letter is a **text-system key** plus timestamps; the
client renders it through the character-text path so gender, name, and honor
substitution still apply.

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
        "letters": [
            {
                "id": 2,
                "letter_key": "letter_mesne_lord_eligible",
                "created_at": 1700000000,
                "read_at": 0
            }
        ]
    }
}
```

## Error Responses

```json
{ "error": "character_id required" }
{ "error": "Character does not belong to this user" }
```

## Notes

- `read_at = 0` means unread; a non-zero value is the unix timestamp the letter
  was marked read.
- The client resolves `letter_key` through the normal text system
  (`game/text/<lang>/<letter_key>.txt`), passing the character context so
  server-side gender/name/honor substitution applies.
- `/api/getPlayerState` exposes `has_letters` (for the hub's Letters button) and
  `unread_letter` (the oldest unread letter, for the one-time overlay) so the
  client can prompt without fetching the full list.
- Mark a letter read with `/api/markLetterRead`.
