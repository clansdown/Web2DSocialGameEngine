cmake_minimum_required(VERSION 3.10)
project(Web2DGameBackend VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find SQLite3
find_package(SQLite3 REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${SQLite3_INCLUDE_DIRS})

# Source files
set(SOURCES
    src/main.cpp
    src/database.cpp
    src/web_server.cpp
    src/api_handler.cpp
)

# Create executable
add_executable(game_server ${SOURCES})

# Link libraries
target_link_libraries(game_server ${SQLite3_LIBRARIES} pthread)

# Installation
install(TARGETS game_server DESTINATION bin)
