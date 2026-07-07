# POST /api/verifyAgeOverride

Validates an age verification override code. Provided for tech support to verify users through alternate means.

## Request

```json
{
    "code": "a1b2c3d4"
}
```

## Response (success — valid code)

```json
{
    "status": "ok",
    "data": {
        "verified": true
    }
}
```

## Response (success — invalid code)

```json
{
    "status": "ok",
    "data": {
        "verified": false
    }
}
```

## Notes

- Public endpoint (no authentication required)
- Currently uses a single hardcoded code `a1b2c3d4`
- Does not modify any database state
- The client uses `verified: true` to set the adult flag during account creation
