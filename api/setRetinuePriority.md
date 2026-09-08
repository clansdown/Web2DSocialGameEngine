# `/api/setRetinuePriority`

Reorders the player's retinue by **strict priority** — the single rank that
drives household **funding order** and **infirmary-bed order** in the economy
tick (members are funded all-or-nothing in `priority ASC`, and injured members
heal in the same order). The knight is always first (`priority 0`); every
non-knight member keeps a unique rank `1..n`.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "member_ids": [12, 9, 15],
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

`member_ids` is an **ordered array listing every non-knight member of the
roster** in the new preferred order, top priority first. It must be a full
permutation of the current non-knight members — omitting a member or adding an
unknown one rejects the whole request (nothing is written). The knight must not
appear in the list.

## Success Response

```json
{
    "status": "ok",
    "data": {
        "members": [
            { "id": 5, "priority": 0 },
            { "id": 12, "priority": 1 },
            { "id": 9, "priority": 2 },
            { "id": 15, "priority": 3 }
        ]
    }
}
```

`members` reflects the confirmed ranking so the client can re-render without a
refetch (the knight is included, always `priority 0`).

## Error Responses

```json
{ "error": "member_ids (an ordered array of member ids) is required." }
{ "error": "member_ids must be exactly your current non-knight members, in your preferred order." }
{ "error": "Character does not belong to this user" }
```

## Notes

- **Permutation validation** is all-or-nothing: the submitted list and the
  character's non-knight member set must match exactly in size and content.
- The knight is never reorderable and is always pinned at `priority 0`.
- `/api/getRetinue` returns each member's current `priority`; the roster tab of
  the manor Retinue panel displays members in this order.