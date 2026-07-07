# `/api/getTexts`

Fetches game text translations for the specified language. Falls back to English if a translation does not exist.

**Public endpoint** — no authentication required.

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
| `sex` | string | Optional. `"male"` or `"female"` — applies gender substitution to `{male\|female}` tokens |

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

## Gender Substitution

When the optional `sex` field is provided, the server replaces gender tokens in the text:

- `{male_form|female_form}` → male form if sex is `"male"`, female form if sex is `"female"`
- Male option always comes first in the token
- Examples:
  - `{He|She}` → `"He"` for male, `"She"` for female
  - `{his|her}` → `"his"` for male, `"her"` for female
  - `{son|daughter}` → `"son"` for male, `"daughter"` for female
- If no `sex` is provided, the male form is used as default (safe fallback)
