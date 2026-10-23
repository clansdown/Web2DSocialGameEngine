# chat_language_prefs Table

Per-character translation language preferences. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE chat_language_prefs (
    character_id INTEGER NOT NULL,
    language TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    change_id INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (character_id, language)
);
```

## Notes

- Absence of a row means **enabled**: by default every source language except
  the game language is translated for display.
- Only explicit toggles are stored.
- Read by `getChatLanguagePrefs`, written by `setChatLanguagePref`.
