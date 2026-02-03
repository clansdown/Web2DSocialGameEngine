# Web2D Social Game Engine
A complete game engine for Social 2D (Sprite-based) Web games with C++ backend and JavaScript frontend.

## Overview

This project provides a full-stack solution for creating web-based 2D social games:

- **Backend (C++)**: High-performance server with SQLite databases and JSON REST API
- **Frontend (JavaScript)**: Browser-based game client using SimpleGame engine

## Architecture

```
┌─────────────────┐         ┌─────────────────┐
│   Web Browser   │         │  Nginx/HTTPS    │
│   (Frontend)    │◄────────┤   (Optional)    │
│   SimpleGame    │         │                 │
└────────┬────────┘         └────────┬────────┘
         │                           │
         │  JSON/HTTP API            │
         │                           │
         ▼                           ▼
┌─────────────────────────────────────────────┐
│         C++ Backend Server                  │
│  ┌──────────────┐    ┌──────────────┐      │
│  │  Web Server  │    │ API Handler  │      │
│  └──────┬───────┘    └──────┬───────┘      │
│         │                   │               │
│         ▼                   ▼               │
│  ┌──────────────┐    ┌──────────────┐      │
│  │ game_data.db │    │ messages.db  │      │
│  │   (SQLite)   │    │   (SQLite)   │      │
│  └──────────────┘    └──────────────┘      │
└─────────────────────────────────────────────┘
```

## Features

### Backend
- **Dual SQLite databases**: Separate databases for game data and messages
- **JSON REST API**: Clean API endpoints for frontend communication
- **Lightweight HTTP server**: Built-in server (designed to work behind nginx)
- **Player management**: Track players, positions, scores
- **Message system**: Chat and social features

### Frontend
- **SimpleGame engine**: Sprite-based 2D game rendering
- **Real-time updates**: Periodic sync with backend
- **Social features**: Integrated chat system
- **Responsive design**: Works on various screen sizes

## Quick Start

### Backend Setup

```bash
cd backend
mkdir build
cd build
cmake ..
make
./game_server 8080
```

See [backend/README.md](backend/README.md) for detailed instructions.

### Frontend Setup

```bash
cd frontend
python3 -m http.server 8000
# or
npx http-server -p 8000
```

Open http://localhost:8000 in your browser.

See [frontend/README.md](frontend/README.md) for detailed instructions.

## Project Structure

```
Web2DSocialGameEngine/
├── backend/                 # C++ backend server
│   ├── CMakeLists.txt      # Build configuration
│   ├── include/            # Header files
│   │   ├── database.h      # Database management
│   │   ├── web_server.h    # HTTP server
│   │   └── api_handler.h   # API endpoints
│   ├── src/                # Implementation
│   │   ├── main.cpp        # Entry point
│   │   ├── database.cpp
│   │   ├── web_server.cpp
│   │   └── api_handler.cpp
│   └── db/                 # Database files (created at runtime)
│
├── frontend/               # JavaScript frontend
│   ├── index.html         # Main HTML file
│   ├── src/
│   │   ├── api-client.js  # Backend API client
│   │   ├── game.js        # Game logic
│   │   ├── main.js        # Entry point
│   │   └── styles.css     # Styling
│   └── assets/            # Game assets
│
└── README.md              # This file
```

## API Endpoints

The backend provides the following JSON API endpoints:

- `GET/POST /api/game_state` - Get/update game state
- `GET/POST /api/messages` - Get/send chat messages
- `POST /api/player_action` - Send player actions

See [backend/README.md](backend/README.md) for detailed API documentation.

## Technologies

### Backend
- **Language**: C++17
- **Database**: SQLite3
- **Build System**: CMake
- **HTTP**: Custom lightweight server

### Frontend
- **Game Engine**: SimpleGame (https://github.com/clansdown/SimpleGame)
- **Language**: JavaScript (ES6+)
- **APIs**: Canvas API, Fetch API

## Development

### Prerequisites

- C++17 compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.10+
- SQLite3 development libraries
- Modern web browser
- Python 3 or Node.js (for local HTTP server)

### Building

```bash
# Backend
cd backend
mkdir build && cd build
cmake ..
make

# Frontend
cd frontend
# No build step required - serve static files
```

## Production Deployment

For production use:

1. **Backend**: Deploy behind nginx with HTTPS
2. **Frontend**: Host on static file server (GitHub Pages, Netlify, etc.)
3. **Security**: Implement authentication, rate limiting, input validation
4. **Performance**: Enable compression, caching, CDN
5. **Monitoring**: Add logging, metrics, error tracking

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

See LICENSE file for details.

## Links

- SimpleGame Engine: https://github.com/clansdown/SimpleGame
- Repository: https://github.com/clansdown/Web2DSocialGameEngine
