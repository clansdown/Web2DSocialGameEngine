# `/api/requestTranslations`

Returns cached translations of the given messages into the viewer's language.
Cache misses are enqueued on the background baseline translation worker; the
response never waits for the sidecar.

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
            "detected_source_language": "en", "confidence": 0.9,
            "tier": "baseline" } } } }
```

## Notes

- Messages already in `lang` are skipped; inaccessible messages are ignored.
- Only cached rows are returned. A miss returns no entry for that message and is
  queued; the result is delivered later via `pollChats.translations` (or
  `pollWildlands.translations`) with `message_id`, `change_id`, and `tier`.
- A queued job is version-guarded: if the message is edited before the sidecar
  finishes, the stale translation is discarded.
- If the sidecar is unavailable, the map stays empty and the original is shown.
- Monthly-pass viewers enqueue the premium tier when that engine is enabled
  (Phase 2); cached premium rows are shared with every adult viewer of the same
  language.
