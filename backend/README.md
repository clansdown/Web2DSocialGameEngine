# Web2D Social Game Engine - Backend

C++ backend server for the Web2D Social Game Engine with SQLite databases and JSON API support.

## Features

- **Dual SQLite Databases**:
  - `game_data.db`: Stores player data, positions, scores, and game state
  - `messages.db`: Stores chat messages and conversation data

- **JSON REST API**:
  - `/api/game_state` - GET/POST game state
  - `/api/messages` - GET/POST chat messages
  - `/api/player_action` - POST player actions

- **Lightweight HTTP Server**: Built-in web server for API endpoints

## Prerequisites

- CMake 3.10 or higher
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- SQLite3 development libraries

### Installing Dependencies

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install cmake g++ libsqlite3-dev
```

#### macOS
```bash
brew install cmake sqlite3
```

#### Windows
- Install Visual Studio 2017 or later with C++ support
- Install CMake from https://cmake.org/download/
- Install vcpkg and use it to install sqlite3:
  ```
  vcpkg install sqlite3:x64-windows
  ```

## Building

```bash
cd backend
mkdir build
cd build
cmake ..
make
```

## Running

```bash
# From the build directory
./game_server [port]

# Example with custom port
./game_server 8080
```

The server will:
1. Create database files in the `db/` directory
2. Initialize database schemas
3. Start listening on the specified port (default: 8080)

## API Endpoints

### GET /api/game_state
Returns the current game state including all players and world data.

**Response:**
```json
{
  "status": "ok",
  "game_state": {
    "players": [],
    "world": {}
  }
}
```

### POST /api/game_state
Updates the game state with new data.

**Request:**
```json
{
  "player": {
    "x": 100,
    "y": 200
  },
  "score": 50
}
```

### GET /api/messages
Retrieves recent chat messages.

**Response:**
```json
{
  "status": "ok",
  "messages": []
}
```

### POST /api/messages
Sends a new chat message.

**Request:**
```json
{
  "text": "Hello!",
  "timestamp": 1234567890
}
```

### POST /api/player_action
Processes a player action (movement, interaction, etc.).

**Request:**
```json
{
  "action": "key_press",
  "key": "w",
  "timestamp": 1234567890
}
```

## Database Schema

### game_data.db

**players table:**
- id (INTEGER PRIMARY KEY)
- username (TEXT UNIQUE)
- position_x (REAL)
- position_y (REAL)
- score (INTEGER)
- created_at (TIMESTAMP)

**game_state table:**
- id (INTEGER PRIMARY KEY)
- player_id (INTEGER FOREIGN KEY)
- state_data (TEXT)
- updated_at (TIMESTAMP)

### messages.db

**messages table:**
- id (INTEGER PRIMARY KEY)
- sender_id (INTEGER)
- receiver_id (INTEGER)
- message (TEXT)
- is_read (INTEGER)
- created_at (TIMESTAMP)

**chat_rooms table:**
- id (INTEGER PRIMARY KEY)
- room_name (TEXT UNIQUE)
- created_at (TIMESTAMP)

## Production Deployment

For production use, it's recommended to:

1. Place this backend behind nginx or another reverse proxy
2. Configure HTTPS at the reverse proxy level
3. Set up proper CORS headers for your frontend domain
4. Implement authentication and session management
5. Add rate limiting and request validation
6. Configure database backups

Example nginx configuration:
```nginx
location /api/ {
    proxy_pass http://localhost:8080/api/;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection 'upgrade';
    proxy_set_header Host $host;
    proxy_cache_bypass $http_upgrade;
}
```

## Development

The code is organized as follows:
- `include/` - Header files
- `src/` - Implementation files
- `db/` - Database files (created at runtime)

## License

See the LICENSE file in the repository root.
