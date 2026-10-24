# `/api/uploadCharacterAvatar`

Uploads a **custom (18+) PFP** for the character, surfaced by the hub **Profile**
activity. The client pre-scales the chosen file: center-crop to a square, scale
to 256x256, and encode as **WebP** when the browser's canvas supports it, else
**JPEG** (`file_to_avatar_payload` in `client/src/lib/avatars.ts`); the request
is `uploadCharacterAvatarRequest` in `client/src/lib/api.ts`.

**Requires authentication**, ownership, and an **adult account** (`users.adult`);
non-adults are rejected with `adult_required`. No gameplay effect.

Custom images are stored OUTSIDE the static image tree using a content-addressed
key: `<character_id>/<sha256>.<ext>` (SHA-256 over the exact bytes stored). The
backend is selected by the server (`characters.avatar_storage`):

- `local` — under `uploads/avatars/`, served only through the age-gated
  `/api/avatar/<character_id>` route.
- `r2` — under the configured R2 prefix (default `avatars/`) in the public
  assets bucket. Authorized adults receive the absolute public URL; the object
  is content-addressed and unguessable.

The custom image is only disclosed to **both** an adult owner and an adult
viewer. The server never returns an R2 custom URL to a minor.

The storage write runs asynchronously on a bounded worker pool, so the uWS loop
is never blocked on R2. Validation and R2 failures fail cleanly and leave the
previous avatar intact.

**Moderation is not implemented** (documented TODO) — this is intentionally
adult-gated only for now.

The change is **rate-limited to once per 24 hours per character**. Only one
upload per character may be in flight.

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
        "avatar_custom_id": "1/4f2c...e91a.webp",
        "avatar_storage": "r2",
        "avatar_changed_at": 1750006500,
        "avatar_url": "https://assets.example.com/avatars/1/4f2c...e91a.webp"
    }
}
```

In local mode `avatar_storage` is `"local"` and `avatar_url` is
`"/api/avatar/1"` (the age-gated route).

## Error Responses

```json
{ "error": "adult_required" }
{ "error": "avatar_locked" }
{ "error": "Unsupported image format (png, jpg, webp, avif)." }
{ "error": "image_base64 required" }
{ "error": "invalid_avatar" }
{ "error": "avatar_too_large" }
{ "error": "invalid_avatar_dimensions" }
{ "error": "avatar_upload_in_progress" }
{ "error": "avatar_upload_conflict" }
{ "error": "avatar_storage_unavailable" }
{ "error": "avatar_upload_failed" }
{ "error": "server_busy" }
{ "error": "request_too_large" }
{ "error": "You may only change your portrait once every 24 hours (12h remaining)." }
{ "error": "Character does not belong to this user" }
```

The total request body is capped at 2 MB (`request_too_large`). Queue/storage
failures return the normal JSON error envelope (for example `server_busy` or
`avatar_storage_unavailable`).

## Notes

- Enforced server-side: format whitelist, magic-byte identity check, a 1 MB
  decoded-size cap, and a 256x256 dimension cap for PNG/JPEG/WebP/AVIF.
- The SHA-256 key is computed over the decoded image bytes, not the base64.
- Replacing an upload deletes the previous object (when the key changed) and
  purges the old R2 URL; browser-cached copies are accepted.
- The old avatar remains intact if validation, storage, or the DB update fails.
- Removing it is `/api/removeCustomAvatar`.
