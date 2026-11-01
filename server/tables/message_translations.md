# message_translations Table

Stored translations, keyed by engine so the baseline and (future) LLM results
coexist. Stored in `ravenest_chat`.

## Schema

```sql
CREATE TABLE message_translations (
    message_id TEXT NOT NULL,
    target_language TEXT NOT NULL,
    engine TEXT NOT NULL,
    model_version TEXT NOT NULL,
    body TEXT NOT NULL,
    detected_source_language TEXT,
    confidence DOUBLE PRECISION,
    created_at BIGINT NOT NULL,
    change_id BIGINT NOT NULL,
    tier TEXT NOT NULL DEFAULT 'baseline',
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
| `tier` | `baseline` or `premium`. Premium rows are cached and shared with every adult viewer reading that language. |

## Notes

- Baseline translations are generated lazily per viewer: `requestTranslations`
  returns cached rows only and enqueues misses on `BaselineTranslationQueue`.
- Monthly-pass authors fan out immediately to room languages in Phase 2.
- A queued job carries the source message `version`; a translation that arrives
  after an edit is discarded.
- Library translations are age-reapable (`translation_reap_age_seconds`).
- Edits delete the message's translations.
