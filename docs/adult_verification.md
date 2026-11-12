# Adult (18+) verification

**Status: deferred self-service flow.** Account creation always starts
non-adult. This document preserves the removed creation-time Digital
Credentials design for a future self-service 18+ action in the Profile
activity.

## Current behavior

- `/api/createAccount` has no `adult`, `displayName`, or `digitalCredential`
  fields. Every new account is created non-adult.
- A new character's visible name is its allocated safe name. Adults can set a
  custom display name later from the Profile activity.
- Non-adult accounts see the **18+ status** section on the Profile activity.
  It creates a private `account`-category support ticket with a required
  free-text note and opens the thread immediately. Staff review it there.
- `staffSetAdultStatus` is the only current grant path. Grant/revoke updates
  `users.adult`, the adult-verification fields, the session snapshot, and
  revokes all sessions.
- `/api/updateUserProfile` rejects client adult changes with
  `adult_status_managed`. There is no client-side age override.

## Deferred self-service flow

When this lands, it should be triggered from the Profile activity, not account
creation:

1. The player chooses **Verify 18+**.
2. The client checks Digital Credentials support and asks the browser for an
   age credential.
3. On success, the client posts the credential to a dedicated account
   endpoint (TODO: name the endpoint; previously the credential rode on
   `createAccount`).
4. The server verifies the credential through the verifier service.
5. Only a verified `is_adult` result sets the account adult; cancellation,
   unsupported browsers, and verifier failures leave the account non-adult.
6. The server revokes sessions after a status change, so the player signs in
   again with the updated status.

The historical creation-time payload is preserved here as the basis for the
future request:

```json
{
  "adult": true,
  "displayName": "Optional custom name",
  "digitalCredential": {
    "protocol": "openid4vp-v1-signed",
    "data": {
      "response": "eyJhbGciOiJSU0EtIn0..."
    }
  }
}
```

In the future flow, `displayName` is **not** part of age verification. Custom
display names are handled by `/api/updateCharacterProfile`
(`display_name` / `clear_display_name`).

## Client implementation notes

The client code remains in `client/src/lib/digitalCredentials.ts`.

### Support detection

- `checkDigitalCredentialsSupport()` first checks for the spec-compliant
  `DigitalCredential` global.
- Some browsers support `navigator.credentials.get({ digital: ... })` without
  exposing that global. The fallback performs a silent request with empty
  digital requests and treats an accepted call as supported.
- **Brave:** Brave may expose `navigator.credentials.get({ digital: ... })`
  without exposing the `DigitalCredential` global, so the fallback is required.
  Brave also ships with the Digital Credentials API disabled by default in some
  versions. The exact instruction is
  `brave://flags/#web-digital-credentials`.
- Do not pre-filter protocols with `DigitalCredential.userAgentAllowsProtocol`.
  It is unreliable across Chromium forks (Brave does not expose it reliably).

`isBraveBrowser()` uses `navigator.brave.isBrave()` when available.

### Protocols and request shape

`getUserSupportedProtocols()` returns:

- `openid4vp-v1-signed` (preferred)
- `openid4vp-v1-unsigned`
- `org-iso-mdoc`

`getVerifierMetadata(session_id)` builds:

- `response_type: "vp_token"`
- a random 32-byte nonce
- `vp_formats_supported: { openid4vp_jwt: true, openid4vp_vc_sd_jwt: true }`
- a `dcql_query` with a `dc+sd-jwt` credential and
  `claims: [{ path: ["age_equal_or_over", "18"] }]`

`requestAgeVerification(session_id)` calls
`navigator.credentials.get({ digital: { requests: [...] }, mediation: "required" })`
and returns the credential response or `null` when unavailable, cancelled, or
failed. The old call site used the session id `create-account`; the future
Profile action should use `profile`.

### Preserved UI copy

When reimplementing, move these strings to the text system. The removed creation
UI used:

- "I am 18 or older (enable custom display name)"
- "WARNING: Brave ships with the Digital Credentials API disabled by default.
  Enable it at `brave://flags/#web-digital-credentials`."
- "Digital Credentials API not available in this browser"
- "Verifying age with digital credential..."
- "Age verified successfully"
- "Age verification cancelled or failed. Adult flag will not be set."
- "Digital Credentials API not supported in this browser. Adult verification
  unavailable."
- "Age verification failed. Adult flag will not be set."

## Server implementation notes

The server code remains in `server/DigitalCredentialsVerifier.{hpp,cpp}`.

- Entry point: `DigitalCredentialsVerifier::verifyDigitalCredential(protocol, credential_data)`.
- Default verifier URL: `http://localhost:2291/verifier/dcGetData`.
- Default timeout: 30 seconds.
- Request body:
  - `credentialProtocol`: the client protocol string
  - `credentialResponse`: the credential `data` value
- The verifier service decrypts/verifies the credential and returns a report
  shaped as `pages[].lines[].{key,value}`.
- The verifier treats a boolean `true` (or the string `"true"` / `"yes"`) on a
  key containing `age`, `over_18`, or `equal_or_over` as the adult result.
- Only `success && is_adult` grants adult status.

Historical creation-time errors, preserved for the future endpoint:

- `digital_cred_required` — adult requested without a credential.
- `digital_cred_not_allowed` — credential sent without an adult request.
- `verifier_service_unavailable` — verifier could not be reached.
- `age_verification_failed` — verifier response was invalid or failed.
- `not_an_adult` — the credential did not prove 18+.

For the future self-service endpoint, keep the same distinction between
"verification unavailable" and "verified not adult". Never partially grant
adult status.

## Data model

Relevant `users` columns:

| Column | Purpose |
|---|---|
| `adult` | Account-level adult flag; gates custom names/avatars, groups, voice, and custom chat content |
| `adult_verified_at` | Unix timestamp of the last verification/status change |
| `adult_verified_by` | Staff account that made a manual change (NULL for self-service) |
| `adult_verification_method` | `digital_credential`, `customer_service`, `test_only`, or a future self-service value |

A status change must update the in-memory session snapshot
(`auth::SessionManager::update_adult`) and revoke existing sessions
(`revoke_all`). Demotion also runs the identity scrub and message tombstoning
described in `docs/minor_content_visibility.md`.

## Test-only hook

`/api/createAccount` retains a test-only escape hatch:

- the server must be in bounded test mode (`--test-num-requests` or
  `--test-timeout-seconds`), and
- `RAVENEST_TEST_ADULT_ACCOUNTS=1` must be set.

Only then does `adult: true` on `createAccount` create an adult account without
a credential. It is never available on a normal production start. Smoke tests
that need adult-only surfaces use this hook; see `tools/chat_smoke.sh`,
`tools/avatar_smoke.sh`, `tools/translation_smoke.sh`, and
`tools/profile_smoke.sh`.

## Related code

- `client/src/lib/digitalCredentials.ts`
- `client/src/components/ProfilePanel.svelte`
- `client/src/components/CreateAccountPage.svelte`
- `server/DigitalCredentialsVerifier.cpp`
- `server/Visibility.cpp` (`admin_set_account_adult_status`)
- `server/auth/SessionManager.cpp` (`update_adult`)
- `api/updateUserProfile.md`
- `api/staff.md`
- `docs/minor_content_visibility.md`
- `server/tables/users.md`
