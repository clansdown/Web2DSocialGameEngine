# `/api/getAvatarPool`

Returns the **safe avatar pool** for one gender
(`game/images/avatars/safe/{male,female}/`, scanned + sorted, no catalog; the
pool is auto-rescanned when the directory's mtime changes, so new art appears
without a restart) for the
character-creation PFP picker and the Profile activity. Also reports whether the
requesting account is an adult (18+), which gates the custom-upload path.

The pool is returned in a **per-user randomized order** (seeded from the
authenticated username), so the same portraits are not always presented first,
while a given player sees a stable order across reloads. The canonical pool is
still sorted internally by numeric stem (validation is order-independent).

The pool is public art served at `/images/avatars/safe/<id>`. If no art has been
authored yet the list is empty and the client creation step skips (it never
blocks character creation); the Profile activity is where a portrait is chosen
later. Adding art is picked up automatically (the pool is rescanned when the
directory's modification time changes — no restart needed).

**Requires authentication.**

## Request

```json
{
    "gender": "female",
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

`gender` is `"male"` or `"female"` (the character's sex). The client requests the
character's own pool, so the picker only ever shows portraits of the right
gender. Missing/invalid values default to `"male"`.

## Success Response

```json
{
    "status": "ok",
    "data": {
        "safe": [
            { "id": "female/1.png", "url": "/images/avatars/safe/female/1.png" },
            { "id": "female/2.jpg", "url": "/images/avatars/safe/female/2.jpg" }
        ],
        "adult": false
    }
}
```

## Error Responses

```json
{ "needs_auth": true }
```

## Notes

- Pools accept `png`, `jpg`, `jpeg`, `webp`, and `avif`.
- The id is the gender-qualified path (`male/1.png`, `female/1.png`); the URL is
  the served path. Ids are stable across requests (sorted by numeric stem, then
  lexicographically within each gender), but the **array order is shuffled per
  user** (seeded from the username) so the display order is stable for a player
  and varied between players.
- Choosing a safe avatar is `/api/setCharacterAvatar`; the server validates the
  pick against the character's **stored** sex, not the request's `gender`. The
  18+ custom upload is `/api/uploadCharacterAvatar` (surfaced by the Profile
  activity).
- See `docs/retinue_design.md` §13 and `server/tables/characters.md`.
