# `/api/getBaronyMembers`

Returns the members of a barony, in join order. The requester must already be a
member of that barony.

**Requires authentication.**

## Request

```json
{
    "barony_id": 2,
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": {
        "members": [
            { "character_id": 1, "display_name": "CloudDragon", "role": "mesne_lord" },
            { "character_id": 7, "display_name": "IronFalcon", "role": "member" }
        ]
    }
}
```

## Error Responses

```json
{ "error": "barony_id required" }
{ "error": "Not a member of this barony" }
```

## Notes

- Membership is checked against the authenticated user's characters, so any
  character on the account may query a barony the account belongs to.
- `role` is `mesne_lord`, `member`, or `baron` (after promotion / founder
  succession).
- Members are ordered by `barony_members.joined_at` ascending.
- Used by the leave flow to pick a founder's successor
  (`/api/leaveBarony`).
