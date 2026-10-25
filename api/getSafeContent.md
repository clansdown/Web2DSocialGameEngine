# `/api/getSafeContent`

Returns the server-owned safe content catalogs for the authenticated viewer.
Minors receive only minor-safe reactions and GIFs; all safe messages, barony
names, tags, and ban reasons are minor-safe by definition.

Requires authentication.

## Request

```json
{
  "auth": { "username": "alice", "token": "..." }
}
```

## Response

```json
{
  "status": "ok",
  "data": {
    "is_adult": false,
    "messages": [
      { "id": "greeting_hello", "body": "Hello.", "contexts": ["direct", "group"], "sort": 10 }
    ],
    "barony_names": [
      { "id": "stormhold", "name": "Stormhold", "sort": 10 }
    ],
    "barony_tags": [
      { "id": "laid_back", "name": "Laid back", "sort": 10 }
    ],
    "reactions": [
      { "id": "thumbs_up", "emoji": "👍", "minor_safe": true }
    ],
    "gifs": [
      { "id": "book", "asset_url": "/images/ui/book.png", "preview_url": "/images/ui/book.png", "minor_safe": true }
    ],
    "ban_reasons": [
      { "id": "harassment", "label": "Harassment" }
    ]
  }
}
```

## Notes

- The catalogs are config-driven (`game/config/safe_*.json`).
- Safe message `contexts` are checked when authoring; an entry is valid only in
  its listed chat types.
- The client must use these IDs for minor authoring. It must never synthesize
  custom content for a minor.
