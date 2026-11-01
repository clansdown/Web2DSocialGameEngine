# `/api/testTranslationTargets` (test-only)

Returns the premium fan-out language targets for a room. This endpoint is
registered **only** when the server starts with
`RAVENEST_TEST_TRANSLATION_TARGETS=1`; it is not part of the production API.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_...", "source_language": "fr" }
```

## Success Response

```json
{ "status": "ok", "data": { "languages": ["de", "en"] } }
```

## Notes

- `languages` excludes the author, minors, members with a block involving the
  author in either direction, empty languages, and the source language.
- Languages are ordered by frequency (descending) then lexicographically, and
  capped by `game/config/translation.json` `premium.max_target_languages`.
- Used by `tools/translation_smoke.sh` to verify the Phase 2 room-language
  selection without an LLM backend.
