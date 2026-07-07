cmake_minimum_required(VERSION 3.20)
project(RavenestServer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

include(FetchContent)
include(ExternalProject)

FetchContent_Declare(sqlite_modern_cpp
    GIT_REPOSITORY https://github.com/SqliteModernCpp/sqlite_modern_cpp.git
    GIT_TAG master
)
FetchContent_MakeAvailable(sqlite_modern_cpp)

# ── uWebSockets / uSockets ─────────────────────────────────────────────
set(UWS_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/uwebsockets-src")
set(UWS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-include")

if(NOT EXISTS "${UWS_SRC_DIR}")
    message(STATUS "Downloading uWebSockets...")
    execute_process(
        COMMAND git clone --recursive https://github.com/uNetworking/uWebSockets.git "${UWS_SRC_DIR}"
        RESULT_VARIABLE UWS_CLONE_RESULT
    )
    if(NOT UWS_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to clone uWebSockets")
    endif()
endif()

if(NOT EXISTS "${UWS_SRC_DIR}/uSockets/uSockets.a")
    message(STATUS "Building uSockets...")
    execute_process(
        COMMAND make default
        WORKING_DIRECTORY "${UWS_SRC_DIR}/uSockets"
        RESULT_VARIABLE UWS_BUILD_RESULT
    )
    if(NOT UWS_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build uSockets")
    endif()
endif()

# Symlink headers into include directory
file(MAKE_DIRECTORY "${UWS_INCLUDE_DIR}")
file(GLOB UWS_HEADERS "${UWS_SRC_DIR}/src/*.h")
foreach(header IN LISTS UWS_HEADERS)
    get_filename_component(header_name "${header}" NAME)
    execute_process(COMMAND ln -sf "${UWS_SRC_DIR}/src/${header_name}" "${UWS_INCLUDE_DIR}/${header_name}" OUTPUT_QUIET ERROR_QUIET)
endforeach()

add_executable(server main.cpp ImageReader.cpp SafeNameGenerator.cpp init_db.cpp FiefdomFetcher.cpp combatants.cpp heroes.cpp fiefdom_officials.cpp GameConfigCache.cpp MoraleCalculator.cpp ActionHandlers.cpp game_logic.cpp DigitalCredentialsVerifier.cpp images/ImageCache.cpp PlayerStateDB.cpp TowerDefenseMapCache.cpp TextManager.cpp GridCollision.cpp UnitUnlockCalculator.cpp mini_games/mini_game_registry.cpp mini_games/tower_defense_handler.cpp mini_games/weeding_handler.cpp)

target_include_directories(server PRIVATE
    "${sqlite_modern_cpp_SOURCE_DIR}/hdr"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${UWS_INCLUDE_DIR}"
    "${UWS_SRC_DIR}/uSockets/src"
)
target_link_libraries(server PRIVATE
    sqlite3
    OpenSSL::SSL
    OpenSSL::Crypto
    "${UWS_SRC_DIR}/uSockets/uSockets.a"
    z
    CURL::libcurl
)
