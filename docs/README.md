# Web2D Social Game Engine Documentation

## Overview

The Web2D Social Game Engine is a game engine designed for creating social 2D (sprite-based) web games with client-server architecture.

## Architecture

### Client
The client handles:
- **Rendering**: 2D sprite-based rendering system
- **Input**: Keyboard and mouse input handling
- **Networking**: Connection to game server
- **UI**: User interface management

### Server
The server manages:
- **Game World**: Game state and logic
- **Networking**: Client connection management
- **Database**: Persistent player data storage
- **Game Logic**: Server-authoritative game rules

### Shared
Common code used by both client and server:
- **Protocol**: Network message definitions
- **Data Types**: Common data structures
- **Utilities**: Logging and helper functions

## Directory Structure

```
Web2DSocialGameEngine/
├── client/                 # Client-side code
│   ├── include/           # Public headers
│   └── src/               # Implementation files
│       ├── core/          # Core client logic
│       ├── rendering/     # Rendering system
│       ├── networking/    # Client networking
│       ├── input/         # Input handling
│       └── ui/            # User interface
├── server/                # Server-side code
│   ├── include/           # Public headers
│   └── src/               # Implementation files
│       ├── core/          # Core server logic
│       ├── game/          # Game world management
│       ├── networking/    # Server networking
│       └── database/      # Database management
├── shared/                # Shared code
│   ├── include/           # Public headers
│   └── src/               # Implementation files
│       ├── protocol/      # Network protocol
│       ├── data/          # Data structures
│       └── utils/         # Utility functions
├── examples/              # Example games and demos
└── docs/                  # Documentation

```

## Building

### Using CMake
```bash
mkdir build
cd build
cmake ..
make
```

### Using Make
```bash
make
```

## Getting Started

(TODO: Add getting started guide)

## API Reference

(TODO: Add API documentation)

## Examples

(TODO: Add example projects)
