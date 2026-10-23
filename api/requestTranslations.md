# `/api/requestTranslations`

Returns translations of the given messages into the viewer's language, using
stored results or generating them via the baseline translation sidecar.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "message_ids": ["m_..", "m_.."], "lang": "fr" }
```

## Success Response

```json
{ "status": "ok", "data": { "translations": {
  "m_..": { "body": "bonjour", "engine": "madlad400-3b", "language": "fr",
            "detected_source_language": "en", "confidence": 0.9 } } } }
```

## Notes

- Messages already in `lang` are skipped; inaccessible messages are ignored.
- New translations are stored and later delivered via `pollChats.translations`.
- If the sidecar is unavailable, the map is simply empty (original shown).
