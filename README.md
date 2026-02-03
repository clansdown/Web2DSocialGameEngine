# Web2DSocialGameEngine

A game engine for Social 2D (Sprite-based) Web games. Client + Server.

## Features

- **Client-Server Architecture**: Authoritative server with lightweight clients
- **2D Sprite-Based Rendering**: Optimized for sprite-based games
- **Networking**: Built-in client-server communication
- **Multiplayer Support**: Designed for social and multiplayer experiences
- **Cross-Platform**: Written in C++ for portability

## Project Structure

```
Web2DSocialGameEngine/
├── client/         # Client-side code (rendering, input, networking)
├── server/         # Server-side code (game logic, database, networking)
├── shared/         # Shared code (protocol, data types, utilities)
├── examples/       # Example games and demos
└── docs/           # Documentation
```

## Building

### Prerequisites
- C++ compiler with C++11 support (GCC, Clang, or MSVC)
- CMake 3.10+ (optional, can use Makefile instead)

### Build with CMake
```bash
mkdir build
cd build
cmake ..
make
```

### Build with Make
```bash
make
```

## Getting Started

See [documentation](docs/README.md) for detailed information about the engine architecture and API.

## License

See [LICENSE](LICENSE) file for details.
