# `/api/getChatLanguagePrefs`

Returns the character's translation language preferences and translation-engine
availability.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12 }
```

## Success Response

```json
{ "status": "ok", "data": { "game_language": "en", "disabled_languages": ["de"],
  "baseline_engine": "madlad400-3b", "baseline_available": true, "llm_available": false } }
```

## Notes

- `game_language` is the account's chosen interface / display language (the
  translation target). Default: every source language except the display
  language is translated.
- `disabled_languages` lists explicit opt-outs (source languages the viewer
  does not want translated).
