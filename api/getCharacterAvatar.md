# `GET /api/avatar/<character_id>`

Streams a character's **custom (18+) PFP**. Custom uploads are never served
statically; this is the only way to fetch one, and it is age-gated.

This is a `GET` (not a JSON `POST`) because it is used directly as an `<img>`
`src`. Authentication therefore rides the query string.

## Request

```
GET /api/avatar/1?username=player&token=session_token_hex
```

| Query param | Purpose |
|---|---|
| `username` | Account username |
| `token` | Session token for `username` |

## Success Response

`200 OK` with the image bytes and the matching `Content-Type`
(`image/png`, `image/jpeg`, `image/webp`, or `image/avif`).

## Error Responses

`404 Not Found` (plain text) when any of these hold:

- the character id is missing/invalid, or `username`/`token` are absent;
- the token is invalid for the username;
- the character has no custom avatar;
- the owner's account is not an adult;
- the viewer is neither the owner nor an adult.

Returning `404` (rather than `403`) avoids confirming that a custom image exists.

## Notes

- Viewer resolution: an under-18 viewer always sees the safe pick instead (the
  server never hands them this URL). `/api/getRetinue` and `/api/getCharacter`
  return a resolved `avatar_url`; when it is a custom image the client appends
  `?username=&token=` (see `with_avatar_auth` in `client/src/lib/avatars.ts`).
- Upload is `/api/uploadCharacterAvatar`; removal is `/api/removeCustomAvatar`.
- **TODO: moderation.** There is no content review yet.
