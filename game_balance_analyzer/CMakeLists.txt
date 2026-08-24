cmake_minimum_required(VERSION 3.20)
project(game_balance_analyzer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Reuse the nlohmann/json header vendored by the server so the analyzer needs
# no network access. The header is header-only and included by path.
set(NLOHMANN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../server/vendor/nlohmann")

# Vendored third-party headers (pcg-cpp random number generator, MIT/Apache).
set(VENDOR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")

add_executable(game_balance_analyzer
    src/main.cpp
    src/config_loader.cpp
    src/report.cpp
    src/statistics.cpp
    src/manor/manor_model.cpp
    src/manor/manor_economy.cpp
    src/manor/manor_analyzer.cpp
    src/manor/manor_strategy.cpp
    src/manor/network.cpp
    src/manor/units.cpp
    src/td/td_analyzer.cpp
    src/rts/rts_analyzer.cpp
    src/weeding/weeding_analyzer.cpp
)

target_include_directories(game_balance_analyzer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${VENDOR_INCLUDE_DIR}"
    "${NLOHMANN_INCLUDE_DIR}"
)
