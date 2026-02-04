cmake_minimum_required(VERSION 3.20)
project(RavenestServer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG develop
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(uWebSockets
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG master
)
FetchContent_MakeAvailable(uWebSockets)

FetchContent_Declare(sqlite_modern_cpp
    GIT_REPOSITORY https://github.com/SRombauts/sqlite_modern_cpp.git
    GIT_TAG master
)
FetchContent_MakeAvailable(sqlite_modern_cpp)

add_subdirectory(${uWebSockets_SOURCE_DIR} ${uWebSockets_BINARY_DIR})

add_executable(server main.cpp)
target_include_directories(server PRIVATE ${sqlite_modern_cpp_SOURCE_DIR}/src)
target_link_libraries(server PRIVATE 
    uwebsockets 
    nlohmann_json::nlohmann_json
    sqlite3
)