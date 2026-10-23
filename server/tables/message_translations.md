# message_translations Table

Stored translations, keyed by engine so the baseline and (future) LLM results
coexist. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE message_translations (
    message_id TEXT NOT NULL,
    target_language TEXT NOT NULL,
    engine TEXT NOT NULL,
    model_version TEXT NOT NULL,
    body TEXT NOT NULL,
    detected_source_language TEXT,
    confidence REAL,
    created_at INTEGER NOT NULL,
    change_id INTEGER NOT NULL,
    PRIMARY KEY (message_id, target_language, engine, model_version)
);
```

## Fields

| Field | Purpose |
|---|---|
| `target_language` | Viewer language this translation is for. |
| `engine` | `madlad400-3b` (baseline) or a future LLM engine id. |
| `model_version` | Model revision/hash. |
| `body` | Translated text. |
| `change_id` | Global update cursor (delivered via `pollChats.translations`). |

## Notes

- Generated lazily on first view (`requestTranslations`).
- Library translations are age-reapable (`translation_reap_age_seconds`).
- Edits delete the message's translations.
