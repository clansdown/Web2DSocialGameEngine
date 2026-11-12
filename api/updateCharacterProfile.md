# POST /api/updateCharacterProfile

Update a character's custom display name and/or safe display name from the
Profile activity.

## Request

```json
{
  "auth": {
    "username": "player_name",
    "token": "xxx"
  },
  "character_id": 123,
  "display_name": "NewName",
  "word1": "Fire",
  "word2": "Knight"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| auth | object | Yes | Authentication object (see below) |
| character_id | integer | Yes | Character id to update |
| display_name | string | No | New custom display name (adult accounts only; max 64 bytes) |
| clear_display_name | boolean | No | Revert to the safe display name (adult accounts only) |
| word1 | string | No | First safe-name word; requires `word2` |
| word2 | string | No | Second safe-name word; requires `word1` |

**Authentication object:**

| Field | Type | Description |
|---|---|---|
| username | string | Account username |
| token | string | Authentication token |

Any subset of fields may be supplied. `display_name` and
`clear_display_name` are mutually exclusive. `word1` and `word2` must be
supplied together.

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "id": 123,
    "display_name": "NewName",
    "safe_display_name": "FireKnight",
    "level": 1,
    "is_adult": true,
    "name_locked": false,
    "token": "hex_token_string"
  }
}
```

| Field | Type | Description |
|---|---|---|
| id | integer | Character id |
| display_name | string | Current visible name (custom when set, otherwise safe) |
| safe_display_name | string | Current safe name |
| level | integer | Character level |
| is_adult | boolean | Whether the owning account is adult |
| name_locked | boolean | Staff identity lock state |
| token | string | Updated authentication token, when the request rotated one |

### Errors

```json
{
  "status": "ok",
  "data": {},
  "error": "character_id required"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "character not found"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "Character does not belong to this user"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "display_name can only be set if account is adult"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "display_name and clear_display_name are mutually exclusive"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "display_name_too_long"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "word1 and word2 required to change safe name"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "Invalid word1 or word2 - words must exist in safe word lists"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "name_locked"
}
```

## Implementation Notes

- **Adult-only custom names**: `display_name` and `clear_display_name` require
  an adult account. Minors may still change their safe name.
- **Safe-name uniqueness**: the safe name is case-insensitively unique. If the
  selected word pair is taken, a numeric suffix is appended (for example
  `FireKnight-2`). Re-saving the same word pair keeps the existing safe name.
- **Visible-name following**: when the character has no custom name, changing
  the safe name also changes the visible `display_name`. When a custom name is
  set, the safe name changes underneath it without changing the visible name.
- **Clearing**: `clear_display_name: true` removes the custom name and makes
  the visible name the safe name.
- **Name history**: every visible-name change records the prior value in
  `character_identity_history` so staff can restore it if needed.
- **Identity lock**: `name_locked` blocks both safe and custom name changes.
  Only staff can change a locked name.
- **Atomicity**: name writes run in a transaction; a failed update is rolled
  back.
- **Partial updates**: omitted fields are left untouched. `safe_display_name`
  is not accepted directly; use `word1` + `word2`.
- Verifies the character belongs to the authenticated user before updating.

## Examples

### Change safe display name

```json
{
  "auth": { "username": "player1", "token": "abc123" },
  "character_id": 123,
  "word1": "Storm",
  "word2": "Wolf"
}
```

### Set custom display name (adult accounts only)

```json
{
  "auth": { "username": "player1", "token": "abc123" },
  "character_id": 123,
  "display_name": "EpicGamer"
}
```

### Revert to the safe display name (adult accounts only)

```json
{
  "auth": { "username": "player1", "token": "abc123" },
  "character_id": 123,
  "clear_display_name": true
}
```

## Implementation Status

- Implemented
