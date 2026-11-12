# POST /api/updateUserProfile

Update non-age account settings. **Adult status cannot be changed here.** It is
account-level and set only by a staff/customer-service action
(`/api/staffSetAdultStatus`) today; self-service Digital Credentials
verification is deferred (see `docs/adult_verification.md`). Sending `adult`
returns `adult_status_managed`.

## Request

```json
{
  "auth": {
    "username": "player_name",
    "token": "xxx"
  },
  "language": "en"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| auth | object | Yes | Authentication object |
| language | string | No | Interface/display language (max 12 safe characters) |
| adult | boolean | No | **Rejected** with `adult_status_managed` |

## Response

```json
{
  "status": "ok",
  "data": {
    "adult": false,
    "adult_status_managed": true,
    "language": "en",
    "token": "hex_token_string"
  }
}
```

## Errors

```json
{ "error": "authentication required" }
{ "error": "adult_status_managed" }
{ "error": "invalid_language" }
```
