# `/api/uploadCharacterAvatar`

Uploads a **custom (18+) PFP** for the character, surfaced by the hub **Profile**
activity. The client pre-scales the chosen file: center-crop to a square, scale
to 256x256, and encode as **WebP** when the browser's canvas supports it, else
**JPEG** (`file_to_avatar_payload` in `client/src/lib/avatars.ts`); the request
is `uploadCharacterAvatarRequest` in `client/src/lib/api.ts`.

**Requires authentication**, ownership, and an **adult account** (`users.adult`);
non-adults are rejected with `adult_required`. No gameplay effect.

Custom images are stored OUTSIDE the static image tree at
`game/uploads/avatars/<character_id>.<ext>` and are never served statically.
They are returned only through the age-gated `/api/avatar/<character_id>` route,
and only when **both** the owner and the viewer are 18+.

**Moderation is not implemented** (documented TODO) — this is intentionally
adult-gated only for now.

The change is **rate-limited to once per 24 hours per character**.

## Request

```json
{
    "character_id": 1,
    "format": "png",
    "image_base64": "iVBORw0KGgoAAAANSUhEUg...",
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

`image_base64` may be a bare base64 payload or a `data:` URI. `format` is one of
`png`, `jpg`, `jpeg`, `webp`, `avif`.

## Success Response

```json
{
    "status": "ok",
    "data": {
        "avatar_custom_id": "1.png",
        "avatar_changed_at": 1750006500,
        "avatar_url": "/api/avatar/1"
    }
}
```

## Error Responses

```json
{ "error": "adult_required" }
{ "error": "Unsupported image format (png, jpg, webp, avif)." }
{ "error": "Image data is missing or invalid." }
{ "error": "Image is too large (max 1 MB)." }
{ "error": "Image is too large (max 256x256)." }
{ "error": "You may only change your portrait once every 24 hours (12h remaining)." }
{ "error": "Character does not belong to this user" }
```

## Notes

- Enforced server-side: format whitelist, magic-byte identity check, a 1 MB size
  cap, and a 256x256 dimension cap for PNG/JPEG (WebP/AVIF dimensions are not
  parsed and rely on the client pre-scale + size cap).
- Replacing an upload removes the previous file if the extension changes.
- Removing it is `/api/removeCustomAvatar`.
