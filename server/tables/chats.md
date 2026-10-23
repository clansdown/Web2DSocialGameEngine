# chats Table

A conversation. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE chats (
    id TEXT PRIMARY KEY,
    type TEXT NOT NULL CHECK (type IN ('direct','group','barony','dukedom','wildlands')),
    name TEXT,
    name_changed_at INTEGER,
    owner_character_id INTEGER,
    direct_key TEXT UNIQUE,
    topic_id TEXT,
    source_kind TEXT,
    source_id INTEGER,
    members_can_invite INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    archived_at INTEGER,
    last_seq INTEGER NOT NULL DEFAULT 0,
    change_id INTEGER NOT NULL,
    UNIQUE (source_kind, source_id)
);
```

## Fields

| Field | Purpose |
|---|---|
| `id` | `c_<hex>` chat id. |
| `type` | direct / group / barony / dukedom / wildlands. |
| `name` | Group/barony display name (NULL for direct/wildlands). |
| `name_changed_at` | Group rename cooldown anchor. |
| `owner_character_id` | Group owner / barony baron. |
| `direct_key` | `"<min>:<max>"` for direct chats; NULL otherwise (UNIQUE allows many NULLs). |
| `topic_id` | Reserved for future wildlands topics. |
| `source_kind` / `source_id` | Materialized-alliance link (`barony`/`dukedom` + game id). |
| `members_can_invite` | Group invite policy toggle. |
| `archived_at` | Set when an empty group is archived. |
| `last_seq` | Highest `seq` allocated. |
| `change_id` | Global update cursor. |

## Relationships

- `chat_members`, `messages`, `chat_invites`, `chat_bans` reference `id`.
- Materialized barony chats use `source_kind='barony'`, `source_id=baronies.id`.

## Notes

- Direct chat creation is idempotent via `direct_key`.
- The single wildlands chat is created lazily by `wildlands_chat_id()`.
