# `GET /api/avatar/<character_id>`

Authorized access to a character's **custom (18+) PFP**. The route is always
age-gated; the storage backend decides what it returns:

- `local` — streams the file from `uploads/avatars/`.
- `r2` — returns `302 Found` with a `Location` pointing at the public,
  content-addressed R2 URL. The public object itself is a capability URL; it is
  only disclosed to an adult owner or adult viewer.

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

Local storage: `200 OK` with the image bytes and the matching `Content-Type`
(`image/png`, `image/jpeg`, `image/webp`, or `image/avif`).

R2 storage: `302 Found` with `Location` set to the public assets URL. The
redirect response is `Cache-Control: private, no-store`; the R2 object carries
an immutable public cache policy.

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
  return a resolved `avatar_url`; for local custom storage the client appends
  `?username=&token=`. R2 custom URLs are absolute and need no credentials (see
  `with_avatar_auth` in `client/src/lib/avatars.ts`).
- Upload is `/api/uploadCharacterAvatar`; removal is `/api/removeCustomAvatar`.
- The R2 redirect is still behind the same owner/adult checks. A leaked public
  capability URL may remain readable until the object is deleted; removal
  deletes the object and purges the exact edge URL (browser copies are accepted).
- **TODO: moderation.** There is no content review yet.
