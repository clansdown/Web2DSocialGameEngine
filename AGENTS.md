# AGENTS.md

## Documentation Policy

When working on this project:

1. **Consult component-specific README files first**:
   - See `server/README.md` for server architecture, auth system, and design decisions
   - Component READMEs are the primary source of architectural context

2. **API documentation required for all endpoints**:
   - Every new API endpoint MUST have a corresponding `.md` file in `api/`
   - Document request format, response format, and error cases
   - See existing `api/*.md` files for format reference

3. **Update documentation when making changes**:
   - Endpoint changes → update both endpoint file AND server README
   - Authentication changes → update server README auth section
   - Schema changes → update `server/tables/*.md` files
   - New dependencies → update CMakeLists.txt AND dependencies list

4. **Document your decisions**:
   - Explain WHY choices were made (not just WHAT)
   - Include tradeoff considerations for future reference
   - Mark unimplemented features with "TODO: ..." placeholder

This maintains living documentation that grows with the codebase.

## Server

The server is a C++23 application built with CMake that:
- Uses uWebSockets for HTTP endpoint handling
- Uses nlohmann/json for JSON request/response parsing
- Uses sqlite_modern_cpp for SQLite database operations
- Listens on port 2290 for incoming requests (HTTP only)
- Designed to be deployed behind nginx or other HTTPS reverse proxies

### Dependencies
- **uWebSockets**: High-performance async web server framework
- **nlohmann/json**: Modern JSON library for C++
- **sqlite_modern_cpp**: Header-only SQLite C++ wrapper
- **SQLite3**: Embedded database (system library)

### Documentation References

- **uWebSockets**: https://unetworking.github.io/uWebSockets.js/generated/index.html
- **nlohmann/json**: https://github.com/nlohmann/json
- **sqlite_modern_cpp**: https://github.com/SqliteModernCpp/sqlite_modern_cpp

### Table Documentation

The `server/tables/` directory contains detailed schema documentation for each SQL table:
- `server/tables/users.md` - users table
- `server/tables/characters.md` - characters table (renamed from players)
- `server/tables/fiefdoms.md` - fiefdoms table
- `server/tables/player_messages.md` - player_messages table
- `server/tables/message_queues.md` - message_queues table

Each `.md` file documents the table's purpose, full schema, field descriptions, indexes, relationships, and usage notes. See `server/tables/README.md` for guidance on documenting new tables.

### Database Architecture

The server uses **two independent SQLite databases** for maximum concurrency:

1. **game.db** - Game state and persistent data
   - `users`: User accounts and authentication
   - `characters`: Character entities with display names and levels
   - `fiefdoms`: Character territories and holdings
   - Future game tables as needed

2. **messages.db** - Character messaging system
   - `player_messages`: Direct messages between characters
   - `message_queues`: Unread message counters per character
   - Future messaging tables as needed

**Design Notes:**
- No joins across databases - they are completely independent
- Concurrent writes possible to both databases simultaneously
- Separate file handles allow true parallel access
- Database files created automatically in working directory

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
    FOREIGN KEY(owner_id) REFERENCES characters(id)
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

### API Endpoints

All endpoints accept POST requests with JSON bodies and respond with:
- Success: `{ "status": "ok", "data": {...} }`
- Error: `{ "error": "error_message_string" }`

**IMPORTANT:** Each API endpoint has corresponding documentation in `api/` directory:
- See `api/login.md` for `/api/login` documentation
- See `api/getCharacter.md` for `/api/getCharacter` documentation
- See `api/Build.md` for `/api/Build` documentation
- See `api/getWorld.md` for `/api/getWorld` documentation
- See `api/getFiefdom.md` for `/api/getFiefdom` documentation
- See `api/sally.md` for `/api/sally` documentation
- See `api/campaign.md` for `/api/campaign` documentation
- See `api/hunt.md` for `/api/hunt` documentation
- See `api/updateUserProfile.md` for `/api/updateUserProfile` documentation
- See `api/updateCharacterProfile.md` for `/api/updateCharacterProfile` documentation

#### Endpoint Overview

- **/api/login**: Authenticate user and return all characters
- **/api/getCharacter**: Retrieve character information
- **/api/updateUserProfile**: Update user account settings (adult flag)
- **/api/updateCharacterProfile**: Update character profile (display names)
- **/api/Build**: Building construction/management (STUB - TODO: implement)
- **/api/getWorld**: Get world state (STUB - TODO: implement)
- **/api/getFiefdom**: Get fiefdom information (STUB - TODO: implement)
- **/api/sally**: Sally forth/battle actions (STUB - TODO: implement)
- **/api/campaign**: Campaign management (STUB - TODO: implement)
- **/api/hunt**: Hunting activities (STUB - TODO: implement)

### Building

```bash
cd server
mkdir -p build && cd build
cmake ..
make
./server
```

### Docker

```bash
cd server
docker build -t ravenest-server .
docker run -p 2290:2290 ravenest-server
```

### Testing the Server

When testing or verifying the server, use the `agent_test_server.sh` script for automated testing:

```bash
./agent_test_server.sh -N 100        # Process 100 requests then exit
./agent_test_server.sh -M 30         # Run for 30 seconds then exit
./agent_test_server.sh -N 50 -M 60   # Exit on whichever limit reached first
```

**Script Options:**
- `-N N`: Exit after N requests (required or use -M)
- `-M M`: Exit after M seconds (required or use -N)
- `-p PORT`: Port to bind (default: 3290)
- `-l FILE`: Write logs to file
- `-q`: Quiet mode (minimal output)
- `-v`: Verbose mode
- `--keep-db`: Keep `.agent_test_db/` directory after exit (default: cleanup)

**Exit Codes:**
- `0`: Success (limit reached and server exited cleanly)
- `1`: Build failure
- `2`: Server startup failed

**Behavior:**
- Uses `.agent_test_db/` directory for databases (automatically wiped and recreated)
- Runs on port 3290 by default
- Exits immediately when request or time limit is reached
- Cuts off in-flight requests on timeout
- Cleans up test databases automatically (use `--keep-db` to preserve)

**Usage Examples:**
- Test specific number of requests: `./agent_test_server.sh -N 50`
- Run for fixed duration: `./agent_test_server.sh -M 60`
- Combined limits: `./agent_test_server.sh -N 100 -M 120`
- With logging: `./agent_test_server.sh -N 25 -l test.log`
- Verbose debugging: `./agent_test_server.sh -N 10 -v`
- Keep databases for inspection: `./agent_test_server.sh -N 5 --keep-db`

### Deployment Notes

1. **HTTP Only**: The server listens on HTTP (port 2290). TLS/HTTPS is handled by a reverse proxy (nginx, etc.)
2. **Databases**: `game.db` and `messages.db` are created in the working directory on first run
3. **Concurrent Access**: Two independent database connections allow simultaneous read/write operations
4. **Error Handling**: All errors return JSON with format `{ "error": "description" }`
5. **API Documentation**: Every endpoint has a corresponding `.md` file in `api/` directory with detailed request/response examples

## Client

TODO: Client side implementation details to be added

## API Documentation Files

Detailed API documentation is provided in the `api/` directory:
- Each API endpoint has its own corresponding `.md` file
- Complete request/response examples included
- Error codes and edge cases documented separately