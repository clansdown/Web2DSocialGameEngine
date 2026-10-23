# `/api/setChatLanguagePref`

Enables or disables translation for a source language for the character.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "language": "de", "enabled": false }
```

## Success Response

```json
{ "status": "ok", "data": { "language": "de", "enabled": false } }
```

## Error Responses

```json
{ "error": "language_required" }
```
