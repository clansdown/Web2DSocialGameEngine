# `/api/getTexts`

Fetches game text translations for the specified language. Falls back to English if a translation does not exist.

**Public endpoint** — no authentication required.

## Scope

Use this endpoint only for **pre-auth screens** (language select, login,
character creation) where no character context exists. No gender or
character-name substitution is applied — gender tokens resolve to the male
default form.

For in-game text that needs the player's gender/name, use the authenticated
[`getCharacterTexts`](./getCharacterTexts.md) endpoint (via `loadTexts()` in
`client/src/lib/text.ts`).

## Request

```json
{
    "language": "es",
    "text_ids": ["ui_login_title", "ui_path_wolf_warden"]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `language` | string | Language code (`en`, `es`, `de`, `fr`, `it`, `ar`, `zh-CN`, `ko`, `ja`) |
| `text_ids` | array[string] | Text IDs to fetch |

## Success Response

```json
{
    "status": "ok",
    "data": {
        "texts": {
            "ui_login_title": "Ravenest: Build and Battle",
            "ui_path_wolf_warden": "Wolf Warden"
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

## Notes

- Texts are read from `text/{language}/{id}.txt`
- Returns empty string if neither the requested language nor English has the text
- Designed for per-component fetching — the client requests only the texts it needs
- The `sex` field is **not** accepted — the client never sends sex. In-game text
  must go through `getCharacterTexts` so the server can substitute gender tokens
  using the character's stored sex.

## Gender Substitution

Gender tokens (`{male_form|female_form}`) are only meaningful with a character
context, so they are handled by the authenticated `getCharacterTexts` endpoint.
On the public endpoint they resolve to the male form (the safe default).
