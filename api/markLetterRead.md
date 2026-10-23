# `/api/markLetterRead`

Marks one of the character's letters as read (idempotent).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "letter_id": 2,
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
        "result": "read"
    }
}
```

## Error Responses

```json
{ "error": "character_id required" }
{ "error": "letter_id required" }
{ "error": "Character does not belong to this user" }
{ "error": "Letter not found" }
```

## Notes

- The letter must belong to the given character; otherwise `Letter not found`
  is returned (the same error for a missing id and an id owned by someone else).
- Marking an already-read letter succeeds and refreshes `read_at`.
- See `/api/getLetters` to list letters and `/api/getPlayerState` for the
  `unread_letter` prompt.
