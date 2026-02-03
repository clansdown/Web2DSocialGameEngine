cmake_minimum_required(VERSION 3.10)
project(Web2DSocialGameEngine VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/client/include
    ${PROJECT_SOURCE_DIR}/server/include
    ${PROJECT_SOURCE_DIR}/shared/include
)

# Shared library
file(GLOB_RECURSE SHARED_SOURCES "shared/src/*.cpp")
add_library(web2d_shared STATIC ${SHARED_SOURCES})

# Client library
file(GLOB_RECURSE CLIENT_SOURCES "client/src/*.cpp")
add_library(web2d_client STATIC ${CLIENT_SOURCES})
target_link_libraries(web2d_client web2d_shared)

# Server library
file(GLOB_RECURSE SERVER_SOURCES "server/src/*.cpp")
add_library(web2d_server STATIC ${SERVER_SOURCES})
target_link_libraries(web2d_server web2d_shared)

# TODO: Add client executable
# add_executable(game_client client/main.cpp)
# target_link_libraries(game_client web2d_client)

# TODO: Add server executable
# add_executable(game_server server/main.cpp)
# target_link_libraries(game_server web2d_server)

# Installation rules
install(TARGETS web2d_shared web2d_client web2d_server
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY client/include/ DESTINATION include/web2d/client)
install(DIRECTORY server/include/ DESTINATION include/web2d/server)
install(DIRECTORY shared/include/ DESTINATION include/web2d/shared)
