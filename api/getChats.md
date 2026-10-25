# `/api/getChats`

Returns the character's viewer-relative chat list, settings, unread counts, last-message
previews, the wildlands chat id, the social block/mute sets, the per-chat
limits, and the **current max `change_id`** (the polling baseline for a fresh
device).

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12 }
```

## Success Response

```json
{
  "status": "ok",
  "data": {
    "cursor": 10432,
    "chats": [ { "id": "c_ab..", "type": "direct", "name": "", "owner_character_id": 0, "direct_key": "12:34", "members_can_invite": false, "created_at": 0, "name_changed_at": 0, "archived": false, "last_seq": 5, "source_kind": "", "source_id": 0 } ],
    "memberships": [ { "chat_id": "c_ab..", "role": "member", "sticky": false, "muted": false, "notify_level": "all", "last_read_seq": 4, "unread_count": 1, "joined_at": 0, "left_at": 0, "change_id": 10430 } ],
    "last_messages": { "c_ab..": { "id": "m_..", "seq": 5, "body": "hi" } },
    "characters": { "34": { "display_name": "Bob", "portrait_url": "/api/avatar/34" } },
    "wildlands_chat_id": "c_wl..",
    "blocked_character_ids": [],
    "muted_character_ids": [],
    "limits": { "direct": { "max_characters": 512, "max_lines": 20 }, "direct_friend": { "max_characters": 4096, "max_lines": 20 }, "wildlands": { "max_characters": 512, "max_lines": 10 } }
  }
}
```

## Error Responses

```json
{ "error": "character_required" }
{ "error": "needs_auth" }
```

## Notes

- Wildlands is excluded from `chats`; the client uses `wildlands_chat_id`.
- History is fetched separately with `/api/getChatMessages`.
- `last_messages`, `characters`, and unread counts are viewer-relative. A minor
  receives safe names/portraits and only safe-message previews; hidden custom
  messages produce no preview or unread increment, and group chats are omitted.
