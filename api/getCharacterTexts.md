# `/api/getCharacterTexts`

Fetches game text translations for the specified language, with gender and
character-name substitution applied server-side for the given character.
Falls back to English if a translation does not exist.

**Authenticated endpoint** — requires `auth` with username and session token.

## Why This Endpoint Exists

The character's sex and display name are stored server-side, so the client
never sends them. Passing `character_id` lets the server apply:

- Gender substitution on `{male|female}` tokens using the character's `sex`
- `{character_name}` replacement using the character's `display_name`

Pre-auth screens (language select, login) that have no character use the
public [`getTexts`](./getTexts.md) endpoint instead.

## Request

```json
{
    "auth": {
        "username": "player1",
        "token": "abc123"
    },
    "character_id": 4,
    "language": "es",
    "text_ids": ["ui_patent_title", "land_patent_earned"]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `auth` | object | `{ username, token }` session credentials |
| `character_id` | int | Character whose sex/name substitution applies |
| `language` | string | Language code (`en`, `es`, `de`, `fr`, `it`, `ar`, `zh-CN`, `ko`, `ja`) |
| `text_ids` | array[string] | Text IDs to fetch |

## Success Response

```json
{
    "status": "ok",
    "data": {
        "texts": {
            "ui_patent_title": "Título de la patente",
            "land_patent_earned": "A {character_name} ..."
        }
    }
}
```

## Error Response

```json
{
    "status": "ok",
    "error": "text_ids array required"
}
```

Possible errors:
- `"character_id required"`
- `"Character not found"`
- `"Character does not belong to this user"`
- `"text_ids array required"`
- `"Text system not initialized"`

## Notes

- Text substitution order: language lookup → gender tokens → `{character_name}`
- Returns empty string if neither the requested language nor English has the text
- If the character has no sex set, gender tokens resolve to the male default form
- `{character_name}` is replaced with `display_name` (not `safe_display_name`)
- Client callers should use `loadTexts()`/`loadText()` from `client/src/lib/text.ts`,
  which route to this endpoint automatically when a character is selected
