# POST /api/setCharacterSex

Sets the character's biological sex (`"male"` or `"female"`) for gender-substituted text rendering. Gender substitution transforms `{his|her}` tokens in text files based on this value. This is a one-time setting per character — the choice cannot be changed after it is set.

This endpoint requires authentication.

## Request

```json
{
    "character_id": 1,
    "sex": "male",
    "auth": {
        "username": "player1",
        "token": "session_token_here"
    }
}
```

## Response (Success)

```json
{
    "status": "ok",
    "data": {
        "id": 1,
        "display_name": "MyCharacter",
        "safe_display_name": "SwiftFox",
        "level": 1,
        "archetype": null,
        "sex": "male"
    }
}
```

## Response (Error)

```json
{
    "error": "character_id required"
}
```

```json
{
    "error": "sex must be 'male' or 'female'"
}
```

```json
{
    "error": "Character not found"
}
```

```json
{
    "error": "Character does not belong to this user"
}
```

## Notes

- Valid sex values are `"male"` and `"female"` only (case-sensitive).
- The endpoint verifies that the character belongs to the authenticated user.
- Once set, `sex` is used by `POST /api/getCharacterTexts` to apply gender substitution server-side. The client never sends `sex`; it calls `loadTexts()`/`loadText()` from `client/src/lib/text.ts`, which routes to `getCharacterTexts` when a character is selected.
- Text files use `{male_form|female_form}` syntax with the male option always first.
- Example: `{He|She} walks through the forest.` → "He" for male, "She" for female.
- If no sex is set, the male form is used as default.
