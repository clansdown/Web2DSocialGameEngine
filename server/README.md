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
├── init_db.cpp            # Database initialization (tables + indexes)
├── init_db.hpp            # Database initialization declarations
├── CMakeLists.txt        # Build configuration (FetchContent for deps)
├── Database.hpp          # Database singleton & connection management
├── ApiResponse.hpp       # Response structure with auth flags
├── AuthManager.hpp       # Token cache and SHA256 generation
├── ApiHandlers.hpp       # Handler types, ClientInfo, endpoint map
├── PasswordHash.hpp      # glibc crypt() yescrypt hashing
└── SafeNameGenerator.*   # Safe display name generation
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
- **Behavior**: Creates user + default character, returns user_id, characters array, and token
- **Response**: `{ "user_id": ..., "username": ..., "characters": [...], "token": "..." }`

### login
- **Auth**: Required (password OR token)
- **Behavior**: Returns user account info and all characters
- **Response**: `{ "user_id": ..., "username": ..., "adult": ..., "characters": [...], token }`

### getCharacter
- **Auth**: Required (password OR token)
- **Behavior**: Returns character data by character_id
- **Response**: `{ "id": ..., "display_name": ..., "safe_display_name": ..., "level": ... }`

### updateUserProfile
- **Auth**: Required (password OR token)
- **Behavior**: Update user account settings (adult flag)
- **Response**: `{ "adult": ..., "token": "..." }`

### updateCharacterProfile
- **Auth**: Required (password OR token)
- **Behavior**: Update character display names (requires character_id, checks adult flag)
- **Response**: `{ "id": ..., "display_name": ..., "safe_display_name": ..., "level": ..., "token": "..." }`

### getPlayer, Build, getWorld, getFiefdom, sally, campaign, hunt
- **Auth**: Required (password OR token)
- **Behavior**: Endpoint-specific functionality
- **Token refresh**: Accept password in auth object to generate new token

## Database Architecture

Two independent SQLite databases for maximum concurrency:

### game.db
- `users`: User accounts (id, username, password_hash, created_at, adult)
- `characters`: Character entities (id, user_id, display_name, safe_display_name, level)
- `fiefdoms`: Character territories (id, owner_id, name, x, y)

### messages.db
- `player_messages`: Direct messages (id, from_character_id, to_character_id, message, timestamp, read)
- `message_queues`: Unread counters (character_id, unread_count)

**Design:**
- No joins across databases
- Concurrent writes to both simultaneously
- Separate file handles for true parallel access

**Initialization:**
- Schema creation and index management handled by `init_db.cpp`
- `initializeGameDB()` creates tables + indexes for game.db
- `initializeMessagesDB()` creates tables + indexes for messages.db
- `initializeAllDatabases()` calls both for complete initialization

### game.db Schema

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    adult INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE characters (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    display_name TEXT NOT NULL,
    safe_display_name TEXT NOT NULL,
    level INTEGER DEFAULT 1,
    FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE fiefdoms (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    x INTEGER NOT NULL,
    y INTEGER NOT NULL,
    peasants INTEGER NOT NULL DEFAULT 0,
    gold INTEGER NOT NULL DEFAULT 0,
    grain INTEGER NOT NULL DEFAULT 0,
    wood INTEGER NOT NULL DEFAULT 0,
    steel INTEGER NOT NULL DEFAULT 0,
    bronze INTEGER NOT NULL DEFAULT 0,
    stone INTEGER NOT NULL DEFAULT 0,
    leather INTEGER NOT NULL DEFAULT 0,
    mana INTEGER NOT NULL DEFAULT 0,
    wall_count INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(owner_id) REFERENCES characters(id)
);

CREATE TABLE fiefdom_buildings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);

CREATE TABLE officials (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    role TEXT NOT NULL,
    portrait_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    level INTEGER NOT NULL DEFAULT 1,
    intelligence INTEGER NOT NULL,
    charisma INTEGER NOT NULL,
    wisdom INTEGER NOT NULL,
    diligence INTEGER NOT NULL,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

### messages.db Schema

```sql
CREATE TABLE player_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    from_character_id INTEGER NOT NULL,
    to_character_id INTEGER NOT NULL,
    message TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    read INTEGER DEFAULT 0
);

CREATE TABLE message_queues (
    character_id INTEGER PRIMARY KEY NOT NULL,
    unread_count INTEGER DEFAULT 0
);
```

## Deployment

- **Port**: 2290 (HTTP only)
- **HTTPS**: Handled by reverse proxy (nginx)
- **Databases**: Created in working directory on first run
- **Nginx config**: See project root README.md

### Command Line Options

| Option | Description |
|--------|-------------|
| `--db-dir PATH` | Database directory (default: current directory) |
| `--port PORT` | Port to bind (default: 2290) |
| `--init-db` | Initialize all database tables and indexes, then exit |
| `--create-tables` | Create all database tables, then exit |
| `--ensure-indexes` | Ensure all indexes exist, then exit |
| `--test-num-requests N` | Exit after N requests (agent test mode) |
| `--test-timeout-seconds M` | Exit after M seconds (agent test mode) |
| `--verbose` | Enable verbose logging |
| `--quiet` | Minimal logging |
| `-h, --help` | Show help message |

### Database Initialization

The server uses `init_db.cpp` to manage database schema creation and index management. Three command-line modes are available:

```bash
# Initial database setup (creates tables and indexes)
./server --init-db --db-dir /var/lib/ravenest

# Create tables only (for new databases)
./server --create-tables --db-dir /var/lib/ravenest

# Ensure indexes exist (use after backup/restore)
./server --ensure-indexes --db-dir /var/lib/ravenest

# Normal server startup (auto-initializes)
./server --port 2290 --db-dir /var/lib/ravenest
```

For normal server operation, `initializeAllDatabases()` is called automatically on startup to create tables and ensure indexes.

## Future Enhancements

| Feature | Status | Notes |
|---------|--------|-------|
| Salted password hashes | Implemented | Yescrypt ($y$) with random salt |
| Session timeout | TODO | Tokens valid until restart per design |
| Rate limiting | TODO | Not yet implemented |
| API versioning | TODO | Current endpoints unversioned |