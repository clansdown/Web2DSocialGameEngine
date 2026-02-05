# Ravenest Server

C++23 HTTP API server for Ravenest Build and Battle game backend.

## Overview

High-performance server built with:
- **uWebSockets**: Async HTTP endpoint handling
- **nlohmann/json**: Modern JSON parsing
- **sqlite_modern_cpp**: SQLite ORM wrapper
- **OpenSSL**: SHA-256 for token generation
- **glibc crypt()**: Yescrypt ($y$ prefix) for password hashing

Design goals:
- Minimal code duplication via unified handler
- Pre-dispatch authentication for all endpoints
- Clean architecture with logical header separation

## Architecture

### Request Flow

```
Request → POST /api/{endpoint}
    ↓
Unified /api/* handler (single handler for all endpoints)
    ↓
[1] Buffer accumulation (once)
    ↓
[2] Parse JSON body
    ↓
[3] Extract endpoint name from URL (/api/login → "login")
    ↓
[4] Extract auth object and IP address (X-Real-IP header)
    ↓
[5] CALL handleAuth() - authentication logic
    ├─ Returns AuthResult with:
    │   - authenticated username (optional)
    │   - new_token (optional)
    │   - needs_auth (bool)
    │   - auth_failed (bool)
    │   - error (optional)
    │
    ├─ If auth fails/needs auth: return early with ApiResponse
    └─ If auth succeeds: proceed to dispatch
    ↓
[6] Endpoint dispatch
    ├─ If endpoint == "createAccount": handleCreateAccount() (special case)
    └─ Otherwise: lookup in handler map → dispatch to handler function
    ↓
[7] Handler returns ApiResponse
    ↓
[8] sendJsonResponse(ApiResponse)
```

### Header File Organization

```
server/
├── main.cpp              # Entry point, unified handler, all endpoint implementations
├── CMakeLists.txt        # Build configuration (FetchContent for deps)
├── Database.hpp          # Database singleton & schema initialization
├── ApiResponse.hpp       # Response structure with auth flags
├── AuthManager.hpp       # Token cache and SHA256 generation
├── ApiHandlers.hpp       # Handler types, ClientInfo, endpoint map
└── PasswordHash.hpp      # glibc crypt() yescrypt hashing
```

## Authentication System

### Request Format

All endpoints expect (except createAccount):
```json
{
  "auth": {
    "username": "player_name",
    "password": "xxx"                    // OR
    "token": "yyy"
  },
  "player_id": 123                         // endpoint-specific fields
}
```

### Token Generation

**Algorithm**: `SHA256(secret_salt + username + password + IP_address)`

1. Server generates 32 random bytes at startup (secret_salt)
2. Hash computed using OpenSSL SHA256
3. Token stored in in-memory cache: `unordered_map<username, token>`
4. Token validity: Until server restart (stateless deployment)

### Password Hashing

**Algorithm**: glibc `crypt()` with yescrypt prefix (`$y$`)

```cpp
std::string salt = "$y$" + generateRandomSalt(16) + "$";
char* hashed = crypt(password.c_str(), salt.c_str());
```

Yescrypt is memory-hard and CPU-hard, providing security against offline attacks.

### Auth Flow Logic

Handled in `handleAuth()` function (pre-dispatch):

```
handleAuth(endpoint, auth_object, ip_address)
    ↓
if endpoint == "createAccount":
    └─ skip auth, return anonymous

if !auth_object.is_object() OR empty:
    └─ return needs_auth = true

if username empty:
    └─ return error = "username required"

if password supplied:
    ├─ Fetch stored hash from DB
    ├─ If user not found: auth_failed = true
    ├─ Verify password with crypt()
    ├─ If mismatch: auth_failed = true
    ├─ Generate token with SHA256(secret + user + pass + ip)
    ├─ Cache token
    └─ return username + new_token

if token supplied:
    ├─ Lookup username in cache
    ├─ If not found OR mismatch: needs_auth = true
    └─ return username

if neither:
    └─ return needs_auth = true
```

### Response Format

All responses descend from base structure:

```json
{
  "status": "ok",
  "data": { /* endpoint-specific fields */ },
  "needs-auth": false,
  "auth-failed": false,
  "error": null
}
```

**Response interpretation:**
- `data.token`: Included when new token generated (password supplied)
- `auth-failed = true`: Only when password verification fails
- `needs-auth = true`: No auth, expired token, or token mismatch
- `error`: Error message string (present only on error)

## API Endpoints

### createAccount
- **Auth**: Not required
- **Behavior**: Creates user + default player, returns token
- **Response**: `{ "id": ..., "username": ..., "player_id": ..., "token": "..." }`

### login
- **Auth**: Required (password OR token)
- **Behavior**: Returns player data
- **Response**: `{ "id": ..., "name": ..., "level": ..., username, token }`

### getPlayer, Build, getWorld, getFiefdom, sally, campaign, hunt
- **Auth**: Required (password OR token)
- **Behavior**: Endpoint-specific functionality
- **Token refresh**: Accept password in auth object to generate new token

## Database Architecture

Two independent SQLite databases for maximum concurrency:

### game.db
- `users`: User accounts (id, username, password_hash, created_at)
- `players`: Player characters (id, user_id, name, level)
- `fiefdoms`: Player territories (id, owner_id, name, x, y)

### messages.db
- `player_messages`: Direct messages (id, from_player_id, to_player_id, message, timestamp, read)
- `message_queues`: Unread counters (player_id, unread_count)

**Design:**
- No joins across databases
- Concurrent writes to both simultaneously
- Separate file handles for true parallel access

## Building

```bash
cd server
mkdir -p build && cd build
cmake ..
make
./server
```

## Deployment

- **Port**: 2290 (HTTP only)
- **HTTPS**: Handled by reverse proxy (nginx)
- **Databases**: Created in working directory on first run
- **Nginx config**: See project root README.md

## Future Enhancements

| Feature | Status | Notes |
|---------|--------|-------|
| Salted password hashes | Implemented | Yescrypt ($y$) with random salt |
| Session timeout | TODO | Tokens valid until restart per design |
| Rate limiting | TODO | Not yet implemented |
| API versioning | TODO | Current endpoints unversioned |