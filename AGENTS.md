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

#### Quick Compile Check

Use `compile_server.sh` to compile the server without running it:

```bash
./compile_server.sh          # Compile with normal output
./compile_server.sh -q       # Compile silently (quiet mode)
./compile_server.sh -v       # Compile with verbose output
./compile_server.sh --help   # Show all options
```

Exit codes:
- `0`: Compilation successful
- `1`: Build failure

#### Manual Build

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

### Coding Standards

**Naming Conventions:**
- Use snake_case for ALL names (variables, functions, classes, types)
- Examples:
  - `function_name()` not `functionName()`
  - `class_name` not `ClassName`
  - `struct_name` not `StructName`
  - `const session_token = ...` not `sessionToken`

**Code Organization:**
- Put logically separate chunks of code into their own functions
  - Unless this would require an absurd number of arguments
- Put closely related functions that are not closely related to other functions into their own files
- Use extremely descriptive variable and function names
- Do not use short names unless they are sufficiently descriptive
  - Exception: iteration variables like `i`, `j`, `k` are acceptable

**Memory Management:**
- Use RAII (Resource Acquisition Is Initialization) wherever possible
- Create RAII wrappers for memory management of code that doesn't natively support RAII
- Follow C++ standard best practices for resource management

**Rationale:** These standards ensure:
- Consistent, readable code across codebase
- Automatic resource cleanup via RAII prevents leaks
- Logical code organization improves maintainability
- Descriptive names serve as inline documentation

## Client

The client is a Vite + Svelte 5 + TypeScript application that provides the game UI.

**Documentation:** See `client/README.md` for complete information about:
- Project structure and development workflow
- Game engine integration (simplegame)
- OPFS storage (Origin Private File System)
- Bootstrap 5.3.8 integration with dark mode
- Available storage functions and config storage API

### Coding Standards

**Naming Conventions:**
- Use snake_case for ALL names (variables, functions, classes, types, interfaces)
- Examples:
  - `function_name()` not `functionName()`
  - `user_store` not `userStore`
  - `interface auth_response` not `AuthResponse`
  - `type api_options` not `ApiOptions`
  - `const session_token = ...` not `sessionToken`

**Styling - Bootstrap First:**
- All UI styling MUST use Bootstrap 5.3.8 classes (loaded via CDN in `index.html`)
- Custom CSS is ONLY permitted for styling that Bootstrap does not provide
- Bootstrap is required for all common UI patterns it supports:
  - Forms, inputs, buttons
  - Cards, alerts, modals
  - Grid, layout, spacing
  - Navigation, dropdowns
  - Loading spinners, badges
- Use Bootstrap's dark mode via `data-bs-theme="dark"` on `<html>` tag
- Reference: https://getbootstrap.com/docs/5.3/

**TypeScript - Strict Typing:**
- Everything must have explicit types - no implicit or inferred types except in trivial cases
- All function parameters must have type annotations
- All function return values must have type annotations
- All variables must have type annotations when the type is not immediately obvious
- Use `interface` or `type` for complex data structures
- Use generic types where appropriate (`<T>`, `<K, V>`, etc.)

**The `any` Type:**
- `any` may ONLY be used when absolutely necessary
- When `any` is used, it MUST be documented with a comment explaining:
  - WHY it's necessary (what prevents using a proper type)
  - WHAT the expected structure is
  - WHY alternatives were not sufficient
- Example of properly documented `any`:
  ```typescript
  // any: Required because this config is loaded dynamically from server
  // Structure matches ApiResponse<T> but T is unknown at compile time
  const response: ApiResponse<any> = await fetchData();
  ```

**Code Organization:**
- Use extremely descriptive variable and function names
- Do not use short names unless they are sufficiently descriptive
  - Exception: iteration variables like `i`, `j`, `k` are acceptable

**Rationale:** These standards ensure:
- Consistent, readable code across codebase
- Type safety catches bugs at compile time
- Explicit types serve as documentation
- Limited `any` usage prevents type safety erosion

When working on the client, always consult `client/README.md` first for architectural context and usage patterns.

## API Documentation Files

Detailed API documentation is provided in the `api/` directory:
- Each API endpoint has its own corresponding `.md` file
- Complete request/response examples included
- Error codes and edge cases documented separately

## Config Linting

### `tools/check_configs.py`

Validates all JSON configuration files against their schema rules. Written in Python 3.13 with full static type annotations.

**Usage:**
```bash
./tools/check_configs.py              # Show errors and warnings
./tools/check_configs.py --no-warnings  # Show errors only
./tools/check_configs.py -h           # Show help
```

**Options:**
- `--no-warnings, -w`: Suppress warnings, only show errors
- `--config-dir, -c`: Directory containing config files (default: `server/config`)

**Exit Codes:**
- `0`: All configs valid (no errors found)
- `1`: Errors found (warnings return 0)

**What It Validates:**
- JSON syntax with helpful line numbers
- Required fields for each config type
- Field type validation (integers for levels, numbers for costs, etc.)
- Value range validation (max_level >= 1, positive speeds, etc.)
- Cross-reference validation (damage types must be defined in damage_types.json)
- Naming convention suggestions (lowercase snake_case IDs)
- Duplicate ID detection
- Missing required damage types (melee, ranged, magical)

**Config Files Validated:**
- `server/config/damage_types.json` - Damage type definitions
- `server/config/player_combatants.json` - Player unit definitions
- `server/config/enemy_combatants.json` - Enemy unit definitions
- `server/config/fiefdom_building_types.json` - Building type definitions
- `server/config/heroes.json` - Hero definitions with equipment, skills, and status effects
- `server/config/fiefdom_officials.json` - Fiefdom official templates with stats and roles

**Image Directory Validation:**
- `server/images/` - Game images (auto-detected from directory structure)
- Linter validates: required directories exist and are non-empty, file naming convention
- See README.md "Images" section for directory structure specification

## Config File Changes Rule

**When any JSON config file is modified:**

1. Run `./tools/check_configs.py` to verify validity before committing
2. If errors exist, fix them before proceeding
3. Update the corresponding documentation in `server/docs/`:
   - `fiefdom_building_types.json` → `server/docs/fiefdom_building_types.md`
   - `damage_types.json` or combatant files → `server/docs/combat_system.md`
   - `heroes.json` → `server/docs/heroes.md`
   - `fiefdom_officials.json` → `server/docs/fiefdom_officials.md`
4. Update AGENTS.md if the change affects config structure or validation rules
5. **Image directory updates required:** If adding new combatants, buildings, heroes, or officials:
   - Combatants: Create `images/combatants/{id}/idle/`, `attack/`, `defend/`, `die/` subdirectories
   - Buildings: Create `images/buildings/{id}/construction/`, `idle/` subdirectories
   - Heroes: Create `images/heroes/{id}/idle/`, `attack/` subdirectories and `skills/{skill_id}/` for icons
   - Officials: Create `images/portraits/{portrait_id}/` directories with portrait images
   - Add at least one image file (1.png, etc.) to each required subdirectory
   - Linter will warn if required directories are missing or empty

This ensures:
- Config files remain syntactically valid
- Documentation stays synchronized with actual config structure
- The linter accurately reflects validation requirements
- Images directory matches config changes