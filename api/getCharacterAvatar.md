# `GET /api/avatar/<character_id>[/<content-key>]`

Access to a character's **custom (18+) PFP**.

## Capability URL (preferred)

`/api/avatar/<character_id>/<content-key>` is an unguessable, content-addressed
URL. Its key is the stored `<character_id>/<sha256>.<ext>` path, and the server
validates it against `characters.avatar_custom_id` before serving. No token or
cookie is required.

```
GET /api/avatar/1/1/9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08.webp
```

- `local` — streams the file from `uploads/avatars/`.
- `r2` — `302 Found` to the public content-addressed R2 URL.

The URL is only returned to an adult owner/viewer; minors always receive the
safe-pool URL. A shared capability URL remains readable until the object is
deleted.

## Legacy age-gated route

```
GET /api/avatar/1?username=player&token=access_token
```

The bare form is retained for old clients and forwards to the same file. It
requires `username` + `token` and returns `404` for a non-owner minor viewer.

## Errors

`404 Not Found` (plain text) when the id/key is invalid, the key does not match
the stored custom avatar, the character has no custom avatar, the owner is not
an adult, or (legacy route only) the token/viewer check fails.

## Notes

- Upload is `/api/uploadCharacterAvatar`; removal is `/api/removeCustomAvatar`.
- Removing/replacing an avatar deletes the old object and purges the exact R2
  edge URL. Browser copies are accepted.
- **TODO: moderation.** There is no content review yet.
