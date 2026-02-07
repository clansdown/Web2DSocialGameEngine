cmake_minimum_required(VERSION 3.20)
project(RavenestServer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

include(FetchContent)

FetchContent_Declare(sqlite_modern_cpp
    GIT_REPOSITORY https://github.com/SqliteModernCpp/sqlite_modern_cpp.git
    GIT_TAG master
)
FetchContent_MakeAvailable(sqlite_modern_cpp)

set(UWS_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/_deps/uwebsockets-src")
set(UWS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets-include")

if(NOT EXISTS "${UWS_DEPS_DIR}/src/App.h")
    message(FATAL_ERROR "uWebSockets not found at ${UWS_DEPS_DIR}. Please run:\n  cd ${UWS_DEPS_DIR} && git submodule update --init --recursive && make default")
endif()

file(MAKE_DIRECTORY "${UWS_INCLUDE_DIR}")
file(GLOB UWS_HEADERS "${UWS_DEPS_DIR}/src/*.h")
foreach(header IN LISTS UWS_HEADERS)
    get_filename_component(header_name "${header}" NAME)
    execute_process(COMMAND ln -sf "${UWS_DEPS_DIR}/src/${header_name}" "${UWS_INCLUDE_DIR}/${header_name}" OUTPUT_QUIET ERROR_QUIET)
endforeach()

add_executable(server main.cpp SafeNameGenerator.cpp init_db.cpp FiefdomFetcher.cpp combatants.cpp)

target_include_directories(server PRIVATE
    "${sqlite_modern_cpp_SOURCE_DIR}/hdr"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann"
    "${UWS_INCLUDE_DIR}"
    "${UWS_DEPS_DIR}/uSockets/src"
)
target_link_libraries(server PRIVATE
    sqlite3
    OpenSSL::SSL
    OpenSSL::Crypto
    "${UWS_DEPS_DIR}/uSockets/uSockets.a"
    z
    crypt
)