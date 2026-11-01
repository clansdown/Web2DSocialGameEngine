cmake_minimum_required(VERSION 3.20)
project(RavenestServer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# PostgreSQL and Argon2 runtime libraries. The deployment image installs the
# -dev packages; this fallback also finds the runtime sonames in minimal
# sandboxes.
find_file(RAVENEST_LIBPQ_LIBRARY
    NAMES libpq.so libpq.so.5
    PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib /usr/local/pgsql/lib)
find_file(RAVENEST_ARGON2_LIBRARY
    NAMES libargon2.so libargon2.so.1
    PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib)
if(NOT RAVENEST_LIBPQ_LIBRARY)
    message(FATAL_ERROR "libpq not found (install libpq-dev / postgresql-client)")
endif()
if(NOT RAVENEST_ARGON2_LIBRARY)
    message(FATAL_ERROR "libargon2 not found (install libargon2-dev)")
endif()

include(FetchContent)
include(ExternalProject)

# ── uWebSockets / uSockets ─────────────────────────────────────────────
# CACHE so a second build tree (e.g. --tsan) can reuse the same source clone
# and prebuilt uSockets.a instead of re-downloading: -DUWS_SRC_DIR=<abs path>.
set(UWS_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/uwebsockets-src" CACHE PATH "uWebSockets source directory")
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

add_executable(server main.cpp ImageReader.cpp SafeNameGenerator.cpp init_db.cpp FiefdomFetcher.cpp combatants.cpp heroes.cpp fiefdom_officials.cpp GameConfigCache.cpp MoraleCalculator.cpp WaterNetwork.cpp ActionHandlers.cpp game_logic.cpp DigitalCredentialsVerifier.cpp images/ImageCache.cpp PlayerStateDB.cpp Letters.cpp Progression.cpp TowerDefenseMapCache.cpp TextManager.cpp GridCollision.cpp UnitUnlockCalculator.cpp TDPlacementValidator.cpp TDGoldCalculator.cpp mini_games/tower_defense_handler.cpp WeedingGameLogic.cpp OngoingRewards.cpp Money.cpp RetinueDB.cpp RetinueMarket.cpp RetinueStrength.cpp MissionGenerator.cpp MissionOfferings.cpp AvatarPool.cpp AvatarUrl.cpp Visibility.cpp Hash.cpp Base64.cpp ThreadPool.cpp avatars/AvatarKey.cpp avatars/AvatarValidation.cpp avatars/AvatarStore.cpp avatars/R2Client.cpp avatars/AvatarUploadManager.cpp avatars/CloudflarePurge.cpp combat/CombatCodecJson.cpp combat/CombatCodecBinary.cpp combat/CombatMatch.cpp combat/CombatantStats.cpp combat/CombatMatchManager.cpp combat/CombatMapCache.cpp combat/CombatPathfinding.cpp CosmeticsDB.cpp MonthlyPass.cpp ReeveConfig.cpp ReeveAdvisor.cpp ReevePlacement.cpp ReeveHandlers.cpp chat/ChatDB.cpp chat/ChatService.cpp chat/ChatHandlers.cpp chat/MadladTranslationEngine.cpp chat/TranslationQueue.cpp chat/Presence.cpp chat/PushSender.cpp staff/StaffStore.cpp staff/StaffAuth.cpp staff/StaffActionEngine.cpp staff/StaffHandlers.cpp registration/RegistrationSettings.cpp registration/InvitationStore.cpp registration/InvitationService.cpp registration/RegistrationRateLimiter.cpp registration/InvitationHandlers.cpp db/Schema.cpp auth/Argon2.cpp auth/SessionManager.cpp runtime/LoopDispatcher.cpp db/PostgresConnection.cpp http/StaticAssets.cpp)

target_include_directories(server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${UWS_INCLUDE_DIR}"
    "${UWS_SRC_DIR}/uSockets/src"
)
target_link_libraries(server PRIVATE
    "${RAVENEST_LIBPQ_LIBRARY}"
    "${RAVENEST_ARGON2_LIBRARY}"
    OpenSSL::SSL
    OpenSSL::Crypto
    "${UWS_SRC_DIR}/uSockets/uSockets.a"
    z
    CURL::libcurl
)

# Staff grant/revoke CLI. It only needs the database layer and staff store.
add_executable(staff_admin StaffAdminMain.cpp init_db.cpp db/Schema.cpp staff/StaffStore.cpp staff/StaffAuth.cpp)
target_include_directories(staff_admin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(staff_admin PRIVATE "${RAVENEST_LIBPQ_LIBRARY}" OpenSSL::Crypto)

# Invitation code administration (pre-alpha invite-only onboarding).
add_executable(invite_admin InviteAdminMain.cpp init_db.cpp db/Schema.cpp registration/InvitationStore.cpp)
target_include_directories(invite_admin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(invite_admin PRIVATE "${RAVENEST_LIBPQ_LIBRARY}" OpenSSL::Crypto)
