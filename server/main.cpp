#include <iostream>
#include <fstream>
#include <optional>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <chrono>
#include <iomanip>
#include <typeinfo>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <random>
#include <map>
#include <set>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <uWebSockets/App.h>
#include "Database.hpp"
#include "ApiResponse.hpp"
#include "AuthManager.hpp"
#include "ApiHandlers.hpp"
#include "PasswordHash.hpp"
#include "SafeNameGenerator.hpp"
#include "init_db.hpp"
#include "FiefdomFetcher.hpp"
#include "mini_games.hpp"
#include "images/ImageCache.hpp"
#include "ActionHandler.hpp"
#include "ActionHandlers.hpp"
#include "GridCollision.hpp"
#include "MoraleCalculator.hpp"
#include "WaterNetwork.hpp"
#include "DigitalCredentialsVerifier.hpp"
#include "game_logic.hpp"
#include "PlayerStateDB.hpp"
#include "Letters.hpp"
#include "Progression.hpp"
#include "TowerDefenseMapCache.hpp"
#include "TextManager.hpp"
#include "ImageReader.hpp"
#include "UnitUnlockCalculator.hpp"
#include "TDPlacementValidator.hpp"
#include "TDGoldCalculator.hpp"
#include "WeedingGameLogic.hpp"
#include "OngoingRewards.hpp"
#include "combat/CombatTypes.hpp"
#include "combat/CombatCodec.hpp"
#include "combat/CombatMatch.hpp"
#include "combat/CombatMatchManager.hpp"
#include "combat/CombatMapCache.hpp"
#include "RetinueDB.hpp"
#include "MissionGenerator.hpp"
#include "MissionOfferings.hpp"
#include "RetinueMarket.hpp"
#include "AvatarPool.hpp"
#include "AvatarUrl.hpp"
#include "CosmeticsDB.hpp"
#include "chat/ChatService.hpp"
#include <sqlite_modern_cpp/errors.h>

using json = nlohmann::json;

static const bool login_debug = false;
static MiniGames* g_mini_games = nullptr;
static combat::combat_match_manager g_combat_manager;
// Deterministic combat seed (D27): 0 = random per match; non-zero (from
// --combat-seed) makes every match reproducible for replay/tests.
static uint32_t g_combat_seed = 0;

// ── Graceful shutdown (uWS "fall-through") ──────────────────────────────────
// uWS is single-threaded: only Loop::defer is safe to call off the loop thread.
// A monitor thread watches g_shutdown_requested and posts the close-all action
// to the loop; closing every socket + the listener lets App::run() return, at
// which point producers are stopped before the App is destroyed.
static std::atomic<bool> g_shutdown_requested{false};
static std::atomic<bool> g_run_returned{false};
static us_listen_socket_t* g_listen_socket = nullptr;

struct ws_combat_user_data {
    int64_t character_id = -1;
    std::string match_id;
    std::string username;
    bool authed = false;
};

// Live combat sockets. Touched only on the loop thread (open/close/deferred).
static std::set<uWS::WebSocket<false, true, ws_combat_user_data>*> g_open_combat_sockets;

// Signal handler: async-signal-safe (a lock-free atomic store only). The monitor
// thread notices the flag and posts the close-all to the loop.
static void handle_shutdown_signal(int) {
    g_shutdown_requested.store(true, std::memory_order_relaxed);
}

void log_error(const std::string& context, const std::string& message) {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::cerr << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] "
              << "[ERROR] " << context << ": " << message << std::endl;
}

void log_sql(const std::string& context, const std::string& sql) {
    if (true) { // Always log SQL for now; could gate on g_verbose later
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::cout << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] "
                  << "[SQL] " << context << ": " << sql << std::endl;
    }
}

/**
 * Fetches the stored difficulty and size for the character's most recent
 * ongoing (level 0) session of the given mini-game. TD reads game_sessions
 * (difficulty + total_rounds); weeding reads the weeding session JSON
 * (difficulty + grid_size). Used by the generic endMiniGame path to compute
 * ongoing rewards from server-owned session data.
 */
static std::optional<std::pair<int, int>> fetch_ongoing_session_params(sqlite::database& db,
                                                                       int character_id,
                                                                       const std::string& mini_game) {    try {
        if (mini_game == "tower_defense") {
            int difficulty = 1;
            int rounds = 0;
            bool found = false;
            db << "SELECT difficulty, total_rounds FROM game_sessions WHERE character_id = ? AND level_id = 0 "
                  "ORDER BY id DESC LIMIT 1;"
               << character_id
               >> [&](int d, int r) { difficulty = d; rounds = r; found = true; };
            if (!found || rounds <= 0) return std::nullopt;
            return std::make_pair(difficulty, rounds);
        }
        if (mini_game == "weeding") {
            std::string session_json;
            bool found = false;
            db << "SELECT session_json FROM weeding_sessions WHERE character_id = ? AND level_id = 0 "
                  "ORDER BY id DESC LIMIT 1;"
               << character_id
               >> [&](std::string sj) { session_json = sj; found = true; };
            if (!found) return std::nullopt;
            json j = json::parse(session_json);
            int difficulty = j.value("difficulty", 1);
            int grid_size = j.value("grid_size", 0);
            if (grid_size <= 0) return std::nullopt;
            return std::make_pair(difficulty, grid_size);
        }
    } catch (const std::exception& e) {
        log_error("fetch_ongoing_session_params", e.what());
    }
    return std::nullopt;
}

/**
 * Reads a fiefdom starting resource value from economy.json's
 * "starting_resources" block (single source of truth for what a new fiefdom
 * begins with — currently 5 gold and 0 of everything else). Missing resources
 * default to 0. Values are whole units (matching the fiefdoms schema columns).
 */
static double starting_resource_value(GameConfigCache& config_cache, const char* resource) {
    const auto& economy = config_cache.getEconomyConfig();
    if (economy.contains("starting_resources") && economy["starting_resources"].is_object()) {
        const auto& sr = economy["starting_resources"];
        if (sr.contains(resource) && sr[resource].is_number()) {
            return sr[resource].get<double>();
        }
    }
    return 0.0;
}

/**
 * Returns the character's existing fiefdom id, creating one if none exists.
 * A character owns at most one fiefdom, and it persists across tenure changes
 * (freehold ↔ barony member ↔ honor founder) — so joining/leaving/founding
 * reuses the same manor rather than minting a new one.
 */
static int ensure_fiefdom_for_character(sqlite::database& db, GameConfigCache& config_cache,
                                        int character_id, const std::string& username, int64_t now) {
    int existing = 0;
    db << "SELECT id FROM fiefdoms WHERE owner_id = ? LIMIT 1;"
       << character_id
       >> [&](int id) { existing = id; };
    if (existing) return existing;

    static std::atomic<int> fiefdom_counter(0);
    int counter = fiefdom_counter++;
    std::string fiefdom_name = "Manor of " + username;
    int fx = (counter % 100) * 10;
    int fy = (counter / 100) * 10;

    db << "INSERT INTO fiefdoms (owner_id, name, x, y, gold, grain, wood, steel, bronze, stone, leather, mana, charcoal, iron, ironwork, wall_count, morale, last_update_time, manor_level) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0.0, ?, 0);"
       << character_id << fiefdom_name << fx << fy
       << starting_resource_value(config_cache, "gold")
       << starting_resource_value(config_cache, "grain")
       << starting_resource_value(config_cache, "wood")
       << starting_resource_value(config_cache, "steel")
       << starting_resource_value(config_cache, "bronze")
       << starting_resource_value(config_cache, "stone")
       << starting_resource_value(config_cache, "leather")
       << starting_resource_value(config_cache, "mana")
       << starting_resource_value(config_cache, "charcoal")
       << starting_resource_value(config_cache, "iron")
       << starting_resource_value(config_cache, "ironwork")
       << now;

    return static_cast<int>(db.last_insert_rowid());
}

/**
 * Verifies that `character_id` belongs to `username` (the authenticated user).
 * Returns true on success; on failure `error` is filled in.
 */
static bool character_owned_by(sqlite::database& db, const std::optional<std::string>& username,
                               int character_id, std::string& error) {
    int user_id = 0;
    int owner_id = 0;
    db << "SELECT id FROM users WHERE username = ?;" << *username >> [&](int id) { user_id = id; };
    db << "SELECT user_id FROM characters WHERE id = ?;" << character_id >> [&](int uid) { owner_id = uid; };
    if (owner_id != user_id) {
        error = "Character does not belong to this user";
        return false;
    }
    return true;
}

// Defined later in this file; used by the lazy baron-promotion check in
// handleGetPlayerState.
void checkBaronPromotion(int barony_id);

/**
 * Resolves the ongoing config reference for a mini-game, or nullptr.
 */
static const json* resolve_ongoing_config(GameConfigCache& config_cache, const std::string& mini_game) {
    if (mini_game == "tower_defense" && config_cache.getTowerDefenseOngoing().is_object()) {
        return &config_cache.getTowerDefenseOngoing();
    }
    if (mini_game == "weeding" && config_cache.getWeedingOngoing().is_object()) {
        return &config_cache.getWeedingOngoing();
    }
    return nullptr;
}

/**
 * Computes the ongoing-mode silver reward for a won game, consumes the shared
 * reward pool, and credits the fiefdom's silver_pence balance when one exists.
 * Returns a JSON object with silver_pence / silver_formatted / reward_tier, or
 * an empty object when no ongoing reward applies (e.g. campaign levels, losses).
 */
static json apply_ongoing_reward(GameConfigCache& config_cache, sqlite::database& db,
                                 int character_id, const std::string& mini_game,
                                 int difficulty, int size, bool won, int64_t now) {
    json result = json::object();
    if (!won) return result;

    const json* ongoing_config = resolve_ongoing_config(config_cache, mini_game);
    if (!ongoing_config) return result;

    int base = ongoing_rewards::compute_base_reward_pence(*ongoing_config, difficulty, size);
    if (base <= 0) return result;

    const auto& economy = config_cache.getEconomyConfig();
    const json reward_pools_cfg = economy.value("reward_pools", json::object());
    auto consumed = ongoing_rewards::consume_pool(db, character_id, base, reward_pools_cfg, now);

    // Credit the fiefdom when the character has one (none exists pre-manor).
    int fiefdom_id = 0;
    db << "SELECT id FROM fiefdoms WHERE owner_id = ? LIMIT 1;" << character_id
       >> [&](int id) { fiefdom_id = id; };
    if (fiefdom_id > 0) {
        auto fiefdom = FiefdomFetcher::fetchFiefdomById(fiefdom_id);
        if (fiefdom.has_value()) {
            FiefdomFetcher::FiefdomResources res;
            res.gold = fiefdom->gold;
            res.silver_pence = fiefdom->silver_pence + consumed.reward_pence;
            res.grain = fiefdom->grain;
            res.wood = fiefdom->wood;
            res.steel = fiefdom->steel;
            res.bronze = fiefdom->bronze;
            res.stone = fiefdom->stone;
            res.leather = fiefdom->leather;
            res.mana = fiefdom->mana;
            res.charcoal = fiefdom->charcoal;
            res.iron = fiefdom->iron;
            res.ironwork = fiefdom->ironwork;
            res.fancy_ironwork = fiefdom->fancy_ironwork;
            res.beams = fiefdom->beams;
            res.boards = fiefdom->boards;
            FiefdomFetcher::updateFiefdomResources(fiefdom_id, res);
        }
    }

    const json currency_cfg = economy.value("currency", json::object());
    result["silver_pence"] = consumed.reward_pence;
    result["silver_formatted"] = ongoing_rewards::format_silver_pence(consumed.reward_pence, currency_cfg);
    result["reward_tier"] = consumed.tier;
    return result;
}

/**
 * Fills the rewards object for an ongoing (level 0) won game by computing and
 * consuming the shared reward pool. Writes silver_pence into rewards and sets
 * response.data["silver_formatted"]. No-op when ongoing result is empty.
 */
static void fill_ongoing_rewards(GameConfigCache& config_cache, sqlite::database& db,
                                 json& level_rewards, json& response_data,
                                 int character_id, const std::string& mini_game,
                                 int difficulty, int size, bool won, int64_t now) {
    json ongoing = apply_ongoing_reward(config_cache, db, character_id, mini_game,
                                        difficulty, size, won, now);
    if (ongoing.empty()) return;
    level_rewards["silver_pence"] = ongoing["silver_pence"];
    response_data["silver_formatted"] = ongoing["silver_formatted"];
    response_data["reward_tier"] = ongoing["reward_tier"];
}

std::atomic<int> g_request_count(0);
std::atomic<bool> g_test_complete(false);
int g_test_num_requests = 0;
int g_test_timeout_seconds = 0;
bool g_verbose = false;
std::string g_db_dir = ".";
// Read-only asset root (config/, images/, text/). Defaults to the working
// directory; `--content` points it at game/ so the server can run from a
// scratch directory without polluting the synced assets with databases.
std::string g_content_dir = ".";
std::string g_config_dir = "config";
std::string g_images_dir = "images";
std::string g_text_dir = "";  // derived from g_content_dir unless --text-dir overrides

// Public endpoints that don't require authentication
const std::unordered_set<std::string> PUBLIC_ENDPOINTS = {
    "createAccount",
    "getTexts",
    "verifyAgeOverride",
    "getUITextures"
};

void check_test_limits() {
    if (g_test_num_requests > 0 || g_test_timeout_seconds > 0) {
        std::thread([]() {
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

            while (!g_test_complete.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                if (g_test_num_requests > 0 && g_request_count.load() >= g_test_num_requests) {
                    g_test_complete.store(true);
                    std::cout << "Test complete: " << g_request_count.load() << " requests processed" << std::endl;
                    std::quick_exit(0);
                }

                if (g_test_timeout_seconds > 0) {
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                    if (seconds >= g_test_timeout_seconds) {
                        g_test_complete.store(true);
                        std::cout << "Test complete: timeout reached after " << seconds << " seconds, " << g_request_count.load() << " requests" << std::endl;
                        std::quick_exit(0);
                    }
                }
            }
        }).detach();
    }
}

void increment_request_count() {
    g_request_count++;
}

void sendJsonResponse(auto* res, const ApiResponse& response) {
    res->end(response.toJson().dump());
}

std::string extractEndpointName(std::string_view path) {
    if (path.size() > 5 && path.substr(0, 5) == "/api/") {
        return std::string(path.substr(5));
    }
    return std::string(path);
}

struct AuthResult {
    std::optional<std::string> username;
    std::optional<std::string> new_token;
    bool needs_auth = false;
    bool auth_failed = false;
    std::optional<std::string> error;

    bool isOk() const {
        return username.has_value() && !auth_failed && !needs_auth && !error.has_value();
    }
};

AuthResult handleAuth(const std::string& endpoint,
                      const json& auth_object,
                      const std::string& ip_address)
{
    AuthResult result;

    if (endpoint == "createAccount") {
        result.username = std::string();
        return result;
    }

    if (!auth_object.is_object() || auth_object.empty()) {
        std::cout << "[DEBUG] handleAuth: no auth object for endpoint=" << endpoint << std::endl;
        result.needs_auth = true;
        return result;
    }

    std::string username = auth_object.value("username", "");
    if (username.empty()) {
        result.error = "username required";
        return result;
    }

    bool has_password = auth_object.contains("password");
    bool has_token = auth_object.contains("token");

    if (has_password) {
        std::string password = auth_object["password"];

        auto& db = Database::getInstance().gameDB();
        std::string stored_hash;

        db << "SELECT password_hash FROM users WHERE username = ?;"
           << username
           >> [&](std::string hash) { stored_hash = hash; };

        if (stored_hash.empty()) {
            result.auth_failed = true;
            return result;
        }

        if (!verifyPassword(password, stored_hash)) {
            result.auth_failed = true;
            return result;
        }

        std::string token = AuthManager::getInstance().authenticateWithPassword(
            username, password, ip_address);

        result.username = std::string(username);
        result.new_token = std::string(token);
        return result;
    }

    if (has_token) {
        std::string token = auth_object["token"];

        if (!AuthManager::getInstance().authenticateWithToken(username, token)) {
            result.needs_auth = true;
            return result;
        }

        result.username = std::string(username);
        return result;
    }

    result.needs_auth = true;
    return result;
}

ApiResponse handleLogin(GameConfigCache& config_cache, const json& body,
                       const std::optional<std::string>& username,
                       const ClientInfo& client,
                       const std::optional<std::string>& new_token)
{
    if (login_debug) std::cout << "[DEBUG] handleLogin: username=" << (username ? *username : "(nullopt)")
                               << " has_token=" << (new_token ? "yes" : "no") << std::endl;
    ApiResponse response;

    auto& db = Database::getInstance().gameDB();

    int user_id = 0;
    bool adult = false;
    std::string language = "en";
    db << "SELECT id, adult, COALESCE(language, 'en') FROM users WHERE username = ?;"
       << *username
       >> [&](int id, bool a, std::string lang) { user_id = id; adult = a; language = lang; };

    if (user_id == 0) {
        response.error = "User not found";
        return response;
    }

    std::vector<json> characters_list;
    db << "SELECT id, display_name, safe_display_name, level, archetype, sex, avatar_safe_id FROM characters WHERE user_id = ?;"
       << user_id
       >> [&](int id, std::string display_name, std::string safe_display_name, int level, std::unique_ptr<std::string> archetype, std::unique_ptr<std::string> sex, std::unique_ptr<std::string> avatar_safe_id) {
             json character;
             character["id"] = id;
             character["display_name"] = display_name;
             character["safe_display_name"] = safe_display_name;
             character["level"] = level;
             if (archetype) {
                 character["archetype"] = *archetype;
             } else {
                 character["archetype"] = nullptr;
             }
             if (sex) {
                 character["sex"] = *sex;
             } else {
                 character["sex"] = nullptr;
             }
             character["avatar_safe_id"] = avatar_safe_id ? json(*avatar_safe_id) : json(nullptr);
             characters_list.push_back(character);
         };

    response.data["user_id"] = user_id;
    response.data["username"] = *username;
    response.data["adult"] = adult;
    response.data["language"] = language;
    response.data["characters"] = characters_list;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

// ASCII lowercase copy (used for case-insensitive name-pool comparisons).
static std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Account-level adult flag (false for an unknown/absent username).
static bool user_is_adult(const std::optional<std::string>& username) {
    if (!username || username->empty()) return false;
    bool adult = false;
    auto& db = Database::getInstance().gameDB();
    db << "SELECT adult FROM users WHERE username = ?;" << *username
       >> [&](bool a) { adult = a; };
    return adult;
}

// PFP URL resolution (safe/custom layering) lives in AvatarUrl.hpp/.cpp so the
// chat system can share it; see that file for the model.

// Ownership check for character-profile handlers defined before the combat
// block's anonymous-namespace `verify_character_ownership` (which is declared
// later in this file and therefore not visible here). Same semantics.
static bool character_owned_by(const json& body,
                               const std::optional<std::string>& username,
                               int64_t& character_id, std::string& display_name) {
    if (!username) return false;
    character_id = body.value("character_id", 0);
    if (character_id == 0) return false;
    auto& db = Database::getInstance().gameDB();
    int owner_id = 0;
    int user_id = 0;
    db << "SELECT user_id FROM characters WHERE id = ?;" << character_id
       >> [&](int uid) { owner_id = uid; };
    db << "SELECT id FROM users WHERE username = ?;" << *username
       >> [&](int id) { user_id = id; };
    if (owner_id == 0 || owner_id != user_id) return false;
    db << "SELECT display_name FROM characters WHERE id = ?;" << character_id
       >> [&](std::string dn) { display_name = dn; };
    return true;
}

ApiResponse handleGetCharacter(GameConfigCache& config_cache, const json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token)
{
    ApiResponse response;

    int character_id = body.value("character_id", 0);
    if (character_id == 0) {
        response.error = "character_id required";
        return response;
    }

    auto& db = Database::getInstance().gameDB();

    std::string display_name;
    std::string safe_display_name;
    int level;
    std::unique_ptr<std::string> archetype;
    std::unique_ptr<std::string> sex;
    std::unique_ptr<std::string> avatar_safe_id;
    std::unique_ptr<std::string> avatar_custom_id;
    int64_t avatar_changed_at = 0;

    db << "SELECT display_name, safe_display_name, level, archetype, sex, "
          "avatar_safe_id, avatar_custom_id, avatar_changed_at FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::string dn, std::string sdn, int l, std::unique_ptr<std::string> a,
              std::unique_ptr<std::string> s, std::unique_ptr<std::string> av_safe,
              std::unique_ptr<std::string> av_custom, int64_t changed) {
             display_name = dn;
             safe_display_name = sdn;
             level = l;
             archetype = std::move(a);
             sex = std::move(s);
             avatar_safe_id = std::move(av_safe);
             avatar_custom_id = std::move(av_custom);
             avatar_changed_at = changed;
         };

    response.data["id"] = character_id;
    response.data["display_name"] = display_name;
    response.data["safe_display_name"] = safe_display_name;
    response.data["level"] = level;
    if (archetype) {
        response.data["archetype"] = *archetype;
    } else {
        response.data["archetype"] = nullptr;
    }
    if (sex) {
        response.data["sex"] = *sex;
    } else {
        response.data["sex"] = nullptr;
    }
    response.data["avatar_safe_id"] = avatar_safe_id ? json(*avatar_safe_id) : json(nullptr);
    response.data["avatar_custom_id"] = avatar_custom_id ? json(*avatar_custom_id) : json(nullptr);
    response.data["avatar_changed_at"] = avatar_changed_at;
    response.data["avatar_url"] = resolve_avatar_url(character_id, user_is_adult(username));

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleBuild(GameConfigCache& config_cache, const json& body,
                         const std::optional<std::string>& username,
                         const ClientInfo& client,
                         const std::optional<std::string>& new_token)
{
    ApiResponse response;

    std::string action = body.value("action", "create");

    // Map request action names to registry action types
    std::string registry_type = action;
    if (action == "create") registry_type = "build";

    GameLogic::ActionContext ctx;
    ctx.request_id = std::string(client.request_id);
    ctx.ip_address = std::string(client.real_ip);

    int character_id = body.value("character_id", 0);
    int fiefdom_id = body.value("fiefdom_id", 0);

    ctx.requesting_character_id = character_id;
    ctx.requesting_fiefdom_id = fiefdom_id;
    ctx.config_cache = &config_cache;

    auto& registry = GameLogic::ActionRegistry::getInstance();
    if (!registry.hasType(registry_type)) {
        response.error = "Invalid action: must be 'create', 'upgrade', 'demolish', 'move', 'upgrade_pond_type', or 'convert'";
        return response;
    }

    auto result = registry.validateAndExecute(registry_type, body, ctx);

    if (result.status == GameLogic::ActionStatus::OK) {
        response.data = result.result;
    } else {
        response.error = result.error_message + " (" + result.error_code + ")";
    }

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleGetWorld(GameConfigCache& config_cache, const json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token)
{
    ApiResponse response;
    response.data["message"] = "getWorld endpoint received";
    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

ApiResponse handleGetFiefdom(GameConfigCache& config_cache, const json& body,
                             const std::optional<std::string>& username,
                             const ClientInfo& client,
                             const std::optional<std::string>& new_token)
{
    ApiResponse response;

    int fiefdom_id = body.value("fiefdom_id", 0);

    // If character_id provided instead, look up fiefdom by character ownership
    if (fiefdom_id == 0 && body.contains("character_id")) {
        int character_id = body["character_id"].get<int>();
        auto& db = Database::getInstance().gameDB();
        db << "SELECT id FROM fiefdoms WHERE owner_id = ?;" << character_id
           >> [&](int fid) { fiefdom_id = fid; };
    }

    if (fiefdom_id == 0) {
        response.error = "fiefdom_id or character_id required";
        return response;
    }

    bool include_buildings = body.value("include_buildings", false);
    bool include_officials = body.value("include_officials", false);
    bool include_heroes = body.value("include_heroes", false);
    bool include_combatants = body.value("include_combatants", false);

    auto& db = Database::getInstance().gameDB();
    int64_t last_update_time = 0;
    db << "SELECT last_update_time FROM fiefdoms WHERE id = ?;" << fiefdom_id
       >> [&](int64_t ts) { last_update_time = ts; };

    auto time_result = GameLogic::updateStateSince(config_cache, last_update_time, std::to_string(fiefdom_id));

    auto fiefdom_opt = FiefdomFetcher::fetchFiefdomById(
        fiefdom_id,
        include_buildings,
        include_officials,
        include_heroes,
        include_combatants
    );
    if (!fiefdom_opt.has_value()) {
        response.error = "fiefdom not found";
        return response;
    }

    FiefdomData& fiefdom = fiefdom_opt.value();
    response.data = fiefdom.toJson();

    // Merge per-fiefdom reserve overrides with economy.json defaults so the
    // client always sees the effective reserve for every resource.
    {
        json effective_reserves = json::object();
        auto economy_cfg = config_cache.getEconomyConfig();
        if (economy_cfg.contains("default_reserves") && economy_cfg["default_reserves"].is_object()) {
            for (auto& [res, val] : economy_cfg["default_reserves"].items()) {
                effective_reserves[res] = val;
            }
        }
        if (fiefdom.reserves.is_object()) {
            for (auto& [res, val] : fiefdom.reserves.items()) {
                effective_reserves[res] = val;
            }
        }
        response.data["reserves"] = effective_reserves;
    }

    auto report_it = time_result.economy_reports.find(fiefdom_id);
    if (report_it != time_result.economy_reports.end()) {
        response.data["economy_report"] = report_it->second;
    }

    // Per-building road-morale points (for client display). Only populated when
    // buildings were requested — the morale computation needs the footprints.
    if (include_buildings) {
        json road_morale = json::object();
        auto building_types = config_cache.getFiefdomBuildingTypes();
        auto road_points = Morale::computeRoadMoralePoints(building_types, fiefdom.buildings);
        for (const auto& [bld_id, points] : road_points) {
            if (points > 0.0) {
                road_morale[std::to_string(bld_id)] = points;
            }
        }
        response.data["road_morale"] = road_morale;

        // Derived household morale (computed, never stored): funded/unfunded
        // retinue + chapel buildings + recent battle results -> capped percent.
        {
            int morale_funded = 0, morale_unfunded = 0;
            db << "SELECT COALESCE(SUM(maintained),0), "
                  "COALESCE(SUM(CASE WHEN maintained = 0 THEN 1 ELSE 0 END),0) "
                  "FROM retinue_members WHERE character_id = ? AND is_knight = 0 AND status = 'active';"
               << fiefdom.owner_id
               >> [&](int f, int u) { morale_funded = f; morale_unfunded = u; };
            int64_t last_victory_ts = 0, last_defeat_ts = 0;
            db << "SELECT last_victory_ts, last_defeat_ts FROM fiefdoms WHERE id = ?;"
               << fiefdom_id
               >> [&](int64_t v, int64_t d) { last_victory_ts = v; last_defeat_ts = d; };
            const int64_t now_ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            const auto hm = Morale::computeHouseholdMorale(
                building_types, config_cache.getRetinueConfig(),
                fiefdom.buildings, morale_funded, morale_unfunded,
                now_ts, last_victory_ts, last_defeat_ts);
            response.data["household_morale"] = {
                {"percent", hm.percent},
                {"points", hm.points},
                {"chapel_points", hm.chapel_points},
                {"funded_members", hm.funded_count},
                {"unfunded_members", hm.unfunded_count},
                {"victory_decay", hm.victory_decay},
                {"defeat_decay", hm.defeat_decay}
            };
        }
    }

    // River cells + water power. The river is lazy-seeded from manor_river.json
    // templates (rotated per fiefdom) so existing fiefdoms get one too.
    {
        FiefdomFetcher::ensureFiefdomRiver(fiefdom_id, config_cache.getManorRiver());
        json river_cells = json::array();
        for (const auto& [rx, ry] : FiefdomFetcher::fetchRiverCells(fiefdom_id)) {
            json cell = json::array();
            cell.push_back(rx);
            cell.push_back(ry);
            river_cells.push_back(std::move(cell));
        }
        response.data["river_cells"] = river_cells;

        json water_power = json::object();
        json water_power_detail = json::object();
        if (include_buildings) {
            std::set<std::pair<int, int>> river_set;
            for (const auto& [rx, ry] : FiefdomFetcher::fetchRiverCells(fiefdom_id)) {
                river_set.insert({rx, ry});
            }
            auto building_types = config_cache.getFiefdomBuildingTypes();
            auto wp = Water::computeWaterPower(building_types, fiefdom.buildings, river_set);
            for (const auto& [bld_id, ok] : wp.powered) {
                water_power[std::to_string(bld_id)] = ok;
            }
            json powered_by = json::object();
            for (const auto& [bld_id, pond_id] : wp.powered_by) {
                powered_by[std::to_string(bld_id)] = pond_id;
            }
            json pond_load = json::object();
            for (const auto& [pond_id, load] : wp.pond_load) {
                pond_load[std::to_string(pond_id)] = load;
            }
            water_power_detail["powered_by"] = powered_by;
            water_power_detail["pond_load"] = pond_load;
        }
        response.data["water_power"] = water_power;
        response.data["water_power_detail"] = water_power_detail;
    }

    // Arable land (abstract resource limiting manor growth). Total scales with
    // the manor (home-base) level; used is the sum of each building's acres.
    {
        json arable = json::object();
        arable["total"] = GameLogic::Validation::getTotalArableAcres(config_cache, fiefdom_id);
        arable["used"] = GameLogic::Validation::getUsedArableAcres(config_cache, fiefdom_id);
        arable["available"] = GameLogic::Validation::getAvailableArableAcres(config_cache, fiefdom_id);
        response.data["arable_land"] = std::move(arable);
    }

    // Forest land (off-map resource limiting the wood producers). Scales with
    // manor level like arable; used is the sum of each wood producer's acres.
    {
        json forest = json::object();
        forest["total"] = GameLogic::Validation::getTotalForestAcres(config_cache, fiefdom_id);
        forest["used"] = GameLogic::Validation::getUsedForestAcres(config_cache, fiefdom_id);
        forest["available"] = GameLogic::Validation::getAvailableForestAcres(config_cache, fiefdom_id);
        response.data["forest_land"] = std::move(forest);
    }

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleSally(GameConfigCache& config_cache, const json& body,
                        const std::optional<std::string>& username,
                        const ClientInfo& client,
                        const std::optional<std::string>& new_token)
{
    ApiResponse response;
    response.data["message"] = "sally endpoint received";
    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

ApiResponse handleCampaign(GameConfigCache& config_cache, const json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token)
{
    ApiResponse response;
    response.data["message"] = "campaign endpoint received";
    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

ApiResponse handleHunt(GameConfigCache& config_cache, const json& body,
                       const std::optional<std::string>& username,
                       const ClientInfo& client,
                       const std::optional<std::string>& new_token)
{
    ApiResponse response;
    response.data["message"] = "hunt endpoint received";
    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

ApiResponse handleCreateAccount(GameConfigCache& config_cache, const json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token)
{
    ApiResponse response;

    std::string new_username = body.value("username", "");
    std::string password = body.value("password", "");
    bool adult_request = body.value("adult", false);
    std::string word1 = body.value("word1", "");
    std::string word2 = body.value("word2", "");
    std::string displayName = body.value("displayName", "");

    // Interface / display language, chosen at first launch. Shape-validated and
    // stored on the account; missing text dirs fall back to English downstream.
    std::string language = body.value("language", "en");
    if (language.empty() || language.size() > 12) {
        language = "en";
    } else {
        for (char c : language) {
            const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                 (c >= '0' && c <= '9') || c == '-' || c == '_';
            if (!allowed) {
                language = "en";
                break;
            }
        }
    }

    std::cout << "[DEBUG] handleCreateAccount: starting for user=" << new_username << std::endl;

    if (new_username.empty() || password.empty()) {
        std::cout << "[DEBUG] handleCreateAccount: empty username or password" << std::endl;
        response.error = "username and password required";
        return response;
    }

    if (word1.empty() || word2.empty()) {
        std::cout << "[DEBUG] handleCreateAccount: empty word1 or word2" << std::endl;
        response.error = "word1 and word2 required for safe display name";
        return response;
    }

    bool has_digital_credential = body.contains("digitalCredential");
    std::string override_code = body.value("override_code", "");

    if (adult_request && !has_digital_credential && override_code != "a1b2c3d4") {
        std::cout << "[DEBUG] handleCreateAccount: digital_cred_required" << std::endl;
        response.error = "digital_cred_required";
        return response;
    }

    if (!adult_request && has_digital_credential) {
        std::cout << "[DEBUG] handleCreateAccount: digital_cred_not_allowed" << std::endl;
        response.error = "digital_cred_not_allowed";
        return response;
    }

    if (!adult_request && !displayName.empty()) {
        std::cout << "[DEBUG] handleCreateAccount: displayName without adult" << std::endl;
        response.error = "displayName can only be set if adult is true";
        return response;
    }

    std::cout << "[DEBUG] handleCreateAccount: passed validation checks" << std::endl;

    std::string existing;
    try {
        std::cout << "[DEBUG] handleCreateAccount: calling Database::getInstance().gameDB()..." << std::endl;
        auto& db = Database::getInstance().gameDB();
        std::cout << "[DEBUG] handleCreateAccount: gameDB() OK, checking username uniqueness..." << std::endl;
        db << "SELECT username FROM users WHERE username = ?;"
           << new_username
           >> [&](std::string u) { existing = u; };
        std::cout << "[DEBUG] handleCreateAccount: SELECT username check done, existing=" << (existing.empty() ? "(none)" : existing) << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[DEBUG] handleCreateAccount: SELECT/gameDB exception type=" << typeid(e).name() << " what=" << e.what() << std::endl;
        log_error("handleCreateAccount",
            std::string("SELECT/gameDB exception: type=") + typeid(e).name() +
            " what=" + e.what());
        response.error = std::string("Account creation failed: ") + e.what();
        return response;
    }

    if (!existing.empty()) {
        std::cout << "[DEBUG] handleCreateAccount: username already exists" << std::endl;
        response.error = "Username already exists";
        return response;
    }

    std::cout << "[DEBUG] handleCreateAccount: calling SafeNameGenerator..." << std::endl;
    std::optional<std::string> safeDisplayNameOpt = SafeNameGenerator::getInstance().generateSafeDisplayName(
        word1, word2, new_username);
    std::cout << "[DEBUG] handleCreateAccount: SafeNameGenerator done" << std::endl;

    if (!safeDisplayNameOpt) {
        std::cout << "[DEBUG] handleCreateAccount: invalid word1 or word2" << std::endl;
        response.error = "Invalid word1 or word2 - words must exist in safe word lists";
        return response;
    }

    std::string safe_display_name = *safeDisplayNameOpt;

    bool adult = false;
    std::string display_name = safe_display_name;

    if (adult_request && has_digital_credential) {
        std::cout << "[DEBUG] handleCreateAccount: verifying digital credential..." << std::endl;
        auto digitalCredential = body["digitalCredential"];
        std::string protocol = digitalCredential.value("protocol", "");
        auto credential_data = digitalCredential.value("data", json{});

        auto verifier_result = DigitalCredentialsVerifier::getInstance().verifyDigitalCredential(
            protocol, credential_data);

        if (!verifier_result.success) {
            adult = false;
        } else {
            adult = verifier_result.is_adult;
        }

        if (adult && !displayName.empty()) {
            display_name = displayName;
        }
        std::cout << "[DEBUG] handleCreateAccount: digital credential adult=" << adult << std::endl;
    }

    if (adult_request && override_code == "a1b2c3d4") {
        std::cout << "[INFO] Account " << new_username << " verified via override code" << std::endl;
        adult = true;
        if (!displayName.empty()) {
            display_name = displayName;
        }
    }

    std::cout << "[DEBUG] handleCreateAccount: adult=" << adult << " display_name=" << display_name << std::endl;

    std::string password_hash;
    try {
        std::cout << "[DEBUG] handleCreateAccount: hashing password..." << std::endl;
        password_hash = hashPassword(password);
        std::cout << "[DEBUG] handleCreateAccount: password hashing done" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[DEBUG] handleCreateAccount: password hash exception type=" << typeid(e).name() << " what=" << e.what() << std::endl;
        log_error("handleCreateAccount", std::string("Password hashing failed: ") + e.what());
        response.error = std::string("Password hashing failed: ") + e.what();
        return response;
    }

    int user_id = 0;
    int character_id = 0;

    std::cout << "[DEBUG] handleCreateAccount: starting DB writes..." << std::endl;

    {
        std::cout << "[DEBUG] handleCreateAccount: getting gameDB() for writes..." << std::endl;
        auto& db = Database::getInstance().gameDB();
        std::cout << "[DEBUG] handleCreateAccount: gameDB() OK for writes" << std::endl;
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        // Step 1: insert user
        {
            std::string sql = "INSERT INTO users (username, password_hash, created_at, adult, language) VALUES (?, ?, ?, ?, ?)";
            std::cout << "[DEBUG] handleCreateAccount: Step 1 - INSERT user" << std::endl;
            log_sql("handleCreateAccount", sql);
            try {
                db << sql << new_username << password_hash << now << (adult ? 1 : 0) << language;
                std::cout << "[DEBUG] handleCreateAccount: Step 1 done" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[DEBUG] handleCreateAccount: Step 1 exception type=" << typeid(e).name() << " what=" << e.what() << std::endl;
                log_error("handleCreateAccount",
                    std::string("Step 1 (insert user) exception: type=") + typeid(e).name() +
                    " what=" + e.what());
                response.error = std::string("Account creation failed: ") + e.what();
                return response;
            }
        }

        user_id = db.last_insert_rowid();
        std::cout << "[DEBUG] handleCreateAccount: user_id=" << user_id << std::endl;

        // Step 2: insert character
        {
            std::string sex_str = body.value("sex", "");
            std::unique_ptr<std::string> sex_ptr;
            if (sex_str == "male" || sex_str == "female") {
                sex_ptr = std::make_unique<std::string>(sex_str);
            }

            std::string sql = "INSERT INTO characters (user_id, display_name, safe_display_name, level, sex) VALUES (?, ?, ?, 1, ?)";
            std::cout << "[DEBUG] handleCreateAccount: Step 2 - INSERT character" << std::endl;
            log_sql("handleCreateAccount", sql);
            try {
                db << sql << user_id << display_name << safe_display_name << sex_ptr;
                std::cout << "[DEBUG] handleCreateAccount: Step 2 done" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[DEBUG] handleCreateAccount: Step 2 exception type=" << typeid(e).name() << " what=" << e.what() << std::endl;
                log_error("handleCreateAccount",
                    std::string("Step 2 (insert character) exception: type=") + typeid(e).name() +
                    " what=" + e.what());
                response.error = std::string("Account creation failed: ") + e.what();
                return response;
            }
        }

        character_id = db.last_insert_rowid();
        std::cout << "[DEBUG] handleCreateAccount: character_id=" << character_id << std::endl;

        // Step 3: create player game state
        {
            std::cout << "[DEBUG] handleCreateAccount: Step 3 - player_game_state" << std::endl;
            log_sql("handleCreateAccount", "INSERT OR IGNORE INTO player_game_state ...");
            try {
                player_state_db::create_player_game_state(db, character_id);
                std::cout << "[DEBUG] handleCreateAccount: Step 3 done" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[DEBUG] handleCreateAccount: Step 3 exception type=" << typeid(e).name() << " what=" << e.what() << std::endl;
                log_error("handleCreateAccount",
                    std::string("Step 3 (player game state) exception: type=") + typeid(e).name() +
                    " what=" + e.what());
                response.error = std::string("Account creation failed: ") + e.what();
                return response;
            }
        }

        std::cout << "[DEBUG] handleCreateAccount: authenticating..." << std::endl;
        std::string ip = std::string(client.real_ip);
        std::string token = AuthManager::getInstance().authenticateWithPassword(
            new_username, password, ip);

        json character;
        character["id"] = character_id;
        character["display_name"] = display_name;
        character["safe_display_name"] = safe_display_name;
        character["level"] = 1;
        character["archetype"] = nullptr;
        character["sex"] = body.value("sex", "");
        character["avatar_safe_id"] = nullptr;

        response.data["user_id"] = user_id;
        response.data["username"] = new_username;
        response.data["adult"] = adult;
        response.data["language"] = language;
        response.data["characters"] = std::vector<json>{character};
        response.data["token"] = token;
    }

    std::cout << "[DEBUG] handleCreateAccount: complete for user=" << new_username << std::endl;
    return response;
}

ApiResponse handleUpdateUserProfile(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username || username->empty()) {
        response.error = "authentication required";
        return response;
    }

    if (!body.contains("adult")) {
        response.error = "adult field required";
        return response;
    }

    bool adult = body["adult"];
    auto& db = Database::getInstance().gameDB();

    int user_id = 0;
    db << "SELECT id FROM users WHERE username = ?;"
       << *username
       >> [&](int id) { user_id = id; };

    if (user_id == 0) {
        response.error = "user not found";
        return response;
    }

    db << "UPDATE users SET adult = ? WHERE id = ?;"
       << (adult ? 1 : 0) << user_id;

    response.data["adult"] = adult;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleUpdateCharacterProfile(GameConfigCache& config_cache, const json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token)
{
    ApiResponse response;

    int character_id = body.value("character_id", 0);
    if (character_id == 0) {
        response.error = "character_id required";
        return response;
    }

    std::string display_name = body.value("display_name", "");
    std::string safe_display_name = body.value("safe_display_name", "");
    std::string word1 = body.value("word1", "");
    std::string word2 = body.value("word2", "");

    bool regenerateSafeName = !word1.empty() && !word2.empty();

    auto& db = Database::getInstance().gameDB();

    int character_user_id = 0;
    db << "SELECT user_id FROM characters WHERE id = ?;"
       << character_id
       >> [&](int uid) { character_user_id = uid; };

    if (character_user_id == 0) {
        response.error = "character not found";
        return response;
    }

    bool adult = false;
    db << "SELECT adult FROM users WHERE id = ?;"
       << character_user_id
       >> [&](bool a) { adult = a; };

    if (!display_name.empty() && !adult) {
        response.error = "display_name can only be set if account is adult";
        return response;
    }

    // Admin identity lock: a locked display name may only be changed by an admin.
    bool name_locked = false;
    db << "SELECT name_locked FROM characters WHERE id = ?;" << character_id
       >> [&](int v) { name_locked = v; };
    if (!display_name.empty() && name_locked) {
        response.error = "name_locked";
        return response;
    }

    if (regenerateSafeName) {
        std::optional<std::string> safeNameOpt = SafeNameGenerator::getInstance().generateSafeDisplayName(
            word1, word2, *username);

        if (!safeNameOpt) {
            response.error = "Invalid word1 or word2 - words must exist in safe word lists";
            return response;
        }
        safe_display_name = *safeNameOpt;

        db << "UPDATE characters SET safe_display_name = ? WHERE id = ?;"
           << safe_display_name << character_id;
    }

    if (!display_name.empty()) {
        // Record the prior name so an admin can force a revert later.
        std::string prior_name;
        db << "SELECT display_name FROM characters WHERE id = ?;" << character_id >> prior_name;
        db << "INSERT INTO character_identity_history (character_id, kind, value, changed_at) "
              "VALUES (?, 'name', ?, ?);"
           << character_id << prior_name << static_cast<long long>(std::time(nullptr));
        db << "UPDATE characters SET display_name = ? WHERE id = ?;"
           << display_name << character_id;
    }

    std::string current_display_name, current_safe_display_name;
    int level;
    db << "SELECT display_name, safe_display_name, level FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::string dn, std::string sdn, int l) {
            current_display_name = dn;
            current_safe_display_name = sdn;
            level = l;
        };

    response.data["id"] = character_id;
    response.data["display_name"] = current_display_name;
    response.data["safe_display_name"] = current_safe_display_name;
    response.data["level"] = level;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleGetGameInfo(GameConfigCache& config_cache, const json& body,
                              const std::optional<std::string>& username,
                              const ClientInfo& client,
                              const std::optional<std::string>& new_token)
{
    ApiResponse response;

    auto& img_cache = ImageCache::getInstance();

    nlohmann::json configs;
    nlohmann::json images;

    if (body.contains("filters") && body["filters"].is_object()) {
        const json& filters = body["filters"];

        nlohmann::json filtered_configs = json::object();
        nlohmann::json filtered_images = json::object();

        if (filters.contains("asset_types") && filters["asset_types"].is_array()) {
            for (const auto& type : filters["asset_types"]) {
                std::string asset_type = type.get<std::string>();

                if (asset_type == "damage_types") {
                    filtered_configs["damage_types"] = config_cache.getDamageTypes();
                } else if (asset_type == "fiefdom_building_types") {
                    // Strip internal design notes (`descriptions`) from the
                    // building configs — they never reach the client.
                    json bf_cfgs = json::array();
                    for (const auto& entry : config_cache.getFiefdomBuildingTypes()) {
                        json cfg = entry;
                        if (cfg.is_object()) {
                            for (auto b_it = cfg.begin(); b_it != cfg.end(); ++b_it) {
                                if (b_it.value().is_object()) {
                                    b_it.value().erase("descriptions");
                                }
                            }
                        }
                        bf_cfgs.push_back(std::move(cfg));
                    }
                    filtered_configs["fiefdom_building_types"] = std::move(bf_cfgs);
                } else if (asset_type == "player_combatants") {
                    filtered_configs["player_combatants"] = config_cache.getPlayerCombatants();
                } else if (asset_type == "enemy_combatants") {
                    filtered_configs["enemy_combatants"] = config_cache.getEnemyCombatants();
                } else if (asset_type == "heroes") {
                    filtered_configs["heroes"] = config_cache.getHeroes();
                } else if (asset_type == "fiefdom_officials") {
                    filtered_configs["fiefdom_officials"] = config_cache.getFiefdomOfficials();
                } else if (asset_type == "wall_config") {
                    filtered_configs["wall_config"] = config_cache.getWallConfig();
                } else if (asset_type == "buildings") {
                    filtered_images["buildings"] = img_cache.getImagesByType("buildings");
                } else if (asset_type == "combatants") {
                    filtered_images["combatants"] = img_cache.getImagesByType("combatants");
                } else if (asset_type == "heroes") {
                    filtered_images["heroes"] = img_cache.getImagesByType("heroes");
                } else if (asset_type == "portraits") {
                    filtered_images["portraits"] = img_cache.getImagesByType("portraits");
                }
            }
        }

        if (filters.contains("asset_ids") && filters["asset_ids"].is_array()) {
            std::vector<std::string> ids;
            for (const auto& id : filters["asset_ids"]) {
                ids.push_back(id.get<std::string>());
            }
            filtered_images = img_cache.getImagesByIds(ids);
        }

        configs = filtered_configs;
        images = filtered_images;

        if (images.is_null() || images.empty()) {
            images = json::object();
        }
    } else {
        configs = config_cache.getAllConfigs();
        images = img_cache.getImages();
    }

    response.data["configs"] = configs;
    response.data["images"] = images;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleGetPlayerState(GameConfigCache& config_cache, const json& body,
                                  const std::optional<std::string>& username,
                                  const ClientInfo& client,
                                  const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    int character_id = body["character_id"].get<int>();

    try {
        auto& db = Database::getInstance().gameDB();
        auto state = player_state_db::get_player_game_state(db, character_id);

        // Phase transition recovery: all levels done but game phase stuck
        if (state.game_phase == "initial_mission") {
            auto now_ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            if (!player_state_db::get_next_incomplete_level(db, character_id, "weeding", 9) ||
                !player_state_db::get_next_incomplete_level(db, character_id, "tower_defense", 9)) {
                player_state_db::earn_land_patent(db, character_id, now_ts);
                player_state_db::clear_current_mini_game(db, character_id, now_ts);
                state = player_state_db::get_player_game_state(db, character_id);
            }
        }

        auto now_ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        // Lazy freehold → freehold_qualified transition. The qualification is
        // monotonic (manor level + RTS milestones), so checking on state fetch
        // covers both the manor-upgrade and combat paths without a background job.
        const auto mesne_lord_reqs = progression::parse_mesne_lord_requirements(config_cache.getProgression());
        if (state.game_phase == "freehold") {
            if (progression::check_freehold_qualification(db, character_id, now_ts, mesne_lord_reqs)) {
                state = player_state_db::get_player_game_state(db, character_id);
            }
        }

        // Available activities — phase-based access control, plus tenure- and
        // content-derived entries (manor when a fiefdom exists, letters when any
        // have been received, honor while a qualified freeholder).
        // TODO: replace with config-driven logic for monthly pass etc.
        auto base_activities = [](const std::string& phase) -> std::vector<std::string> {
            if (phase == "initial_mission")
                return {"tasks", "profile", "chat"};
            if (phase == "land_patent")
                return {"tasks", "profile", "land_patent", "chat", "combat"};
            if (phase == "baron_track")
                return {"tasks", "profile", "chat", "combat"};
            if (phase == "baron_right")
                return {"tasks", "profile", "chat", "tournament", "combat"};
            if (phase == "freehold" || phase == "freehold_qualified")
                return {"tasks", "profile", "chat", "tournament", "adventure", "combat"};
            if (phase == "sandbox")
                return {"tasks", "manor", "profile", "chat", "tournament", "adventure", "combat"};
            return {"tasks", "profile", "chat"};  // fallback
        };
        std::vector<std::string> activities = base_activities(state.game_phase);
        auto ensure_activity = [&](const std::string& id) {
            if (std::find(activities.begin(), activities.end(), id) == activities.end()) {
                activities.push_back(id);
            }
        };
        if (progression::has_fiefdom(db, character_id)) ensure_activity("manor");
        if (letters::has_any_letter(db, character_id)) ensure_activity("letters");
        if (state.game_phase == "freehold_qualified") ensure_activity("honor");

        // Barony membership (drives the hub's Barony panel / leave action).
        int member_barony_id = 0;
        std::string member_role;
        db << "SELECT barony_id, role FROM barony_members WHERE character_id = ?;"
           << character_id >> [&](int b, std::string r) { member_barony_id = b; member_role = r; };
        if (member_barony_id != 0) {
            ensure_activity("barony");
            // Baron promotion is evaluated lazily (fixing the old gap where it
            // only ran when someone joined): once a member's manor reaches the
            // threshold, the barony can be promoted on the next state fetch.
            if (progression::get_manor_level(db, character_id) >= 3) {
                checkBaronPromotion(member_barony_id);
            }
        }
        state.available_activities = activities;
        response.data = state.toJson();

        // Flag for client: land patent needs acknowledgment
        if (state.game_phase == "land_patent" && !state.land_patent_acknowledged) {
            response.data["land_patent_earned"] = true;
        }

        // Barony membership summary (name + role + headcount).
        if (member_barony_id != 0) {
            std::string barony_name;
            db << "SELECT name FROM baronies WHERE id = ?;"
               << member_barony_id >> [&](std::string n) { barony_name = n; };
            int member_count = 0;
            db << "SELECT COUNT(*) FROM barony_members WHERE barony_id = ?;"
               << member_barony_id >> [&](int c) { member_count = c; };
            response.data["barony"] = {
                {"id", member_barony_id},
                {"name", barony_name},
                {"role", member_role},
                {"member_count", member_count}
            };
        }

        // Letters: the hub shows the overlay for the oldest unread letter and a
        // Letters button once any letter has been received.
        response.data["has_letters"] = letters::has_any_letter(db, character_id);
        if (auto unread = letters::oldest_unread_letter(db, character_id)) {
            response.data["unread_letter"] = {
                {"id", unread->id},
                {"letter_key", unread->letter_key}
            };
        }

        // Barony join cooldown (0 when the character never left a barony).
        int64_t barony_left_at = 0;
        db << "SELECT barony_left_at FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](int64_t v) { barony_left_at = v; };
        response.data["barony_join_cooldown_until"] =
            barony_left_at > 0
                ? barony_left_at + progression::join_cooldown_seconds(config_cache.getProgression())
                : 0;

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleGetPlayerState", e.what());
        response.error = std::string("Failed to get player state: ") + e.what();
    }

    return response;
}

static void resolve_td_image_urls(nlohmann::json& entries, const std::string& category) {
    for (auto& [id, entry] : entries.items()) {
        std::string img = entry.value("image_file", id);
        entry["image_url"] = "/images/tower_defense/" + category + "/" + img;
    }
}

/** Thread-local RNG for procedural generation. */
static std::mt19937& get_rng() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng;
}

/**
 * Deterministic FNV-1a hash of a string, used to seed a per-user RNG.
 *
 * std::hash is implementation-defined and may differ across runs/platforms, so
 * it cannot give a user a stable shuffle order. FNV-1a is fixed and portable.
 */
static uint32_t stable_string_seed(const std::string& value) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

/** Random integer in [min, max] inclusive. */
static int random_int(int min, int max) {
    if (min > max) std::swap(min, max);
    if (min == max) return min;
    std::uniform_int_distribution<int> dist(min, max);
    return dist(get_rng());
}

/** Random double in [0, val). */
static double random_double(double val) {
    if (val <= 0.0) return 0.0;
    std::uniform_real_distribution<double> dist(0.0, val);
    return dist(get_rng());
}

/** Pick a spawn point by weighted priority from the list. */
static json pick_spawn_point(const json& spawn_points) {
    if (!spawn_points.is_array() || spawn_points.empty()) {
        return json::object();
    }
    double total_weight = 0;
    for (auto& sp : spawn_points) {
        total_weight += sp.value("priority", 1.0);
    }
    if (total_weight <= 0) {
        return spawn_points[0];
    }
    double roll = random_double(total_weight);
    for (auto& sp : spawn_points) {
        roll -= sp.value("priority", 1.0);
        if (roll <= 0) {
            return sp;
        }
    }
    return spawn_points.back();
}

/** Resolve a min/max range JSON object to a random value. */
static int resolve_range(const json& range_obj) {
    if (range_obj.is_number()) return range_obj.get<int>();
    if (range_obj.is_object()) {
        int mn = range_obj.value("min", 0);
        int mx = range_obj.value("max", 10);
        return random_int(mn, mx);
    }
    return 10;
}

/**
 * Apply escalation to generate the next round from a previous round's groups.
 * Follows the compounding escalation pattern.
 */
static json escalate_round(
    const json& previous_groups,       // the full round that just finished
    const json& escalation_cfg,        // {waves_per_round, total_escalation, default_escalation}
    const json& escalation_base,       // round 13's groups from template (for recycling)
    int extra)                         // number of rounds beyond the escalation base
{
    // 1. Deep copy previous groups (only entries with mobs)
    json new_groups = json::array();
    for (auto& g : previous_groups) {
        if (g.contains("mobs")) {
            new_groups.push_back(g);
        }
    }

    // 2. Add recycled groups from escalation_base (sequential cycle)
    json base_mob_groups = json::array();
    for (auto& g : escalation_base) {
        if (g.contains("mobs")) {
            base_mob_groups.push_back(g);
        }
    }

    double waves_per_round = escalation_cfg.value("waves_per_round", 0.0);
    int extra_waves = static_cast<int>(std::floor(extra * waves_per_round));
    json default_esc = escalation_cfg.value("default_escalation", json::object());

    for (int i = 0; i < extra_waves; ++i) {
        int src_idx = i % static_cast<int>(base_mob_groups.size());
        json recycled = base_mob_groups[src_idx];
        if (!recycled.contains("escalation") && !default_esc.is_null()) {
            recycled["escalation"] = default_esc;
        }
        new_groups.push_back(recycled);
    }

    // 3. Calculate escalation budget
    int total_points = resolve_range(escalation_cfg["total_escalation"]);

    // 4. Distribute points across groups (one pass then refill)
    std::vector<int> pool;
    for (int i = 0; i < static_cast<int>(new_groups.size()); ++i) {
        pool.push_back(i);
    }

    for (int p = 0; p < total_points; ++p) {
        if (pool.empty()) {
            for (int i = 0; i < static_cast<int>(new_groups.size()); ++i) {
                pool.push_back(i);
            }
        }
        int pick = random_int(0, static_cast<int>(pool.size()) - 1);
        int idx = pool[pick];
        pool.erase(pool.begin() + pick);

        auto& group = new_groups[idx];
        json esc;
        if (group.contains("escalation") && group["escalation"].is_object()) {
            esc = group["escalation"];
        } else if (!default_esc.is_null()) {
            esc = default_esc;
        } else {
            continue;
        }

        if (esc.contains("count")) {
            group["count"]["min"] = group["count"]["min"].get<int>() + resolve_range(esc["count"]);
            group["count"]["max"] = group["count"]["max"].get<int>() + resolve_range(esc["count"]);
        }
        if (esc.contains("interval_ms")) {
            int e_int = resolve_range(esc["interval_ms"]);
            group["interval_ms"]["min"] = std::max(650, group["interval_ms"]["min"].get<int>() + e_int);
            group["interval_ms"]["max"] = std::max(650, group["interval_ms"]["max"].get<int>() + e_int);
        }
        if (esc.contains("initial_delay_ms")) {
            int e_del = resolve_range(esc["initial_delay_ms"]);
            group["initial_delay_ms"]["min"] = std::max(100, group["initial_delay_ms"]["min"].get<int>() + e_del);
            group["initial_delay_ms"]["max"] = std::max(100, group["initial_delay_ms"]["max"].get<int>() + e_del);
        }
    }

    // 5. Resolve randomized values for each group and emit entries
    json schedule = json::array();
    for (auto& group : new_groups) {
        if (!group.contains("mobs")) continue;

        std::vector<std::string> available;
        for (auto& m : group["mobs"]) {
            available.push_back(m.get<std::string>());
        }
        if (available.empty()) continue;

        int mi = random_int(0, static_cast<int>(available.size()) - 1);
        json entry;
        entry["enemy_id"] = available[mi];
        entry["count"] = resolve_range(group["count"]);
        entry["interval_ms"] = resolve_range(group["interval_ms"]);
        entry["initial_delay_ms"] = resolve_range(group["initial_delay_ms"]);
        schedule.push_back(entry);
    }

    return schedule;
}

/** Resolve a template round into a spawn schedule (randomizes ranges, filters mobs). */
static json resolve_template_round(
    const json& round_groups,
    const std::set<std::string>& unlocked_mobs,
    const json& spawn_points)
{
    json schedule = json::array();
    json round_escalation;
    for (auto& g : round_groups) {
        if (!g.contains("mobs") && g.contains("escalation")) {
            round_escalation = g["escalation"];
        }
    }

    for (auto& group : round_groups) {
        if (!group.contains("mobs")) continue;

        std::vector<std::string> available;
        for (auto& m : group["mobs"]) {
            std::string mid = m.get<std::string>();
            if (unlocked_mobs.count(mid)) {
                available.push_back(mid);
            }
        }
        if (available.empty()) continue;

        int mi = random_int(0, static_cast<int>(available.size()) - 1);
        json entry;
        entry["enemy_id"] = available[mi];
        entry["count"] = resolve_range(group["count"]);
        entry["interval_ms"] = resolve_range(group["interval_ms"]);
        entry["initial_delay_ms"] = resolve_range(group["initial_delay_ms"]);

        json sp = pick_spawn_point(spawn_points);
        if (sp.contains("id")) {
            entry["spawn_point_id"] = sp["id"];
        }
        schedule.push_back(entry);
    }
    return schedule;
}

/** Generate a spawn schedule using wave templates. */
static json generate_td_spawn_schedule(
    GameConfigCache& config,
    int difficulty,
    int round_number,
    int completed_count,
    const json& spawn_points,
    const json& previous_schedule = json::array(),
    int total_rounds = 0)
{
    const auto& wt = config.getTowerDefenseWaveTemplates();

    auto fallback = [&]() -> json {
        json entry;
        entry["enemy_id"] = "dire_rat";
        entry["count"] = 5 + difficulty;
        entry["interval_ms"] = 2000;
        entry["initial_delay_ms"] = 1000;
        json fb = json::array();
        fb.push_back(entry);
        return fb;
    };

    if (wt.is_null() || !wt.contains("difficulty_templates")) {
        return fallback();
    }

    // Build unlocked mob set from completed_count
    std::set<std::string> unlocked_mobs;
    if (wt.contains("mob_unlocks")) {
        for (auto& [key, val] : wt["mob_unlocks"].items()) {
            int threshold = std::stoi(key);
            if (completed_count >= threshold) {
                for (auto& m : val) {
                    unlocked_mobs.insert(m.get<std::string>());
                }
            }
        }
    }
    if (unlocked_mobs.empty()) {
        unlocked_mobs.insert("dire_rat");
    }

    // Find the template for this difficulty
    const auto& templates = wt["difficulty_templates"];
    const json* tmpl = nullptr;
    for (int d = difficulty; d >= 1; --d) {
        std::string dk = std::to_string(d);
        if (templates.contains(dk)) {
            tmpl = &templates[dk];
            break;
        }
    }
    if (!tmpl) {
        tmpl = &templates.begin().value();
    }

    if (!tmpl->contains("rounds") || !(*tmpl)["rounds"].is_array()) {
        return fallback();
    }

    const auto& rounds = (*tmpl)["rounds"];
    int num_template_rounds = static_cast<int>(rounds.size());
    int escalation_base_round = num_template_rounds - 1;

    // Escalation round (beyond escalation base)
    if (!previous_schedule.is_null() && !previous_schedule.empty() && round_number > escalation_base_round) {
        // Load escalation config from the template's last round
        const auto& base_groups = rounds[escalation_base_round];
        json escalation_cfg;
        for (auto& g : base_groups) {
            if (!g.contains("mobs") && g.contains("escalation")) {
                escalation_cfg = g["escalation"];
                break;
            }
        }
        if (escalation_cfg.is_null()) {
            return fallback();
        }

        int extra = round_number - escalation_base_round;
        return escalate_round(previous_schedule, escalation_cfg, base_groups, extra);
    }

    // Template round
    int template_index = round_number;
    if (template_index >= num_template_rounds) {
        template_index = escalation_base_round;
    }

    const auto& round_groups = rounds[template_index];
    if (!round_groups.is_array()) {
        return fallback();
    }

    json result = resolve_template_round(round_groups, unlocked_mobs, spawn_points);
    if (result.empty()) {
        return fallback();
    }
    return result;
}

/**
 * Resolve the schedule file name for a given level ID from the mini-games config.
 */
static std::string resolve_schedule_file(const nlohmann::json& mg_config, int level_id) {
    if (mg_config.contains("levels")) {
        for (const auto& lvl : mg_config["levels"]) {
            if (lvl["id"] == level_id) {
                return mg_config.value("schedule_file", std::string());
            }
        }
    }
    if (mg_config.contains("baron_levels")) {
        for (const auto& lvl : mg_config["baron_levels"]) {
            if (lvl["id"] == level_id) {
                return mg_config.value("baron_schedule_file", std::string());
            }
        }
    }
    return std::string();
}

/** Load map metadata for a given level from mini-games config. */
static json load_map_for_level(const json& mg_config, int level_id) {
    auto find_map = [&](const json& levels) -> std::string {
        for (const auto& lvl : levels) {
            if (lvl["id"] == level_id && lvl.contains("map")) {
                return lvl["map"].get<std::string>();
            }
        }
        return std::string();
    };
    std::string map_file = find_map(mg_config.value("levels", json::array()));
    if (map_file.empty()) map_file = find_map(mg_config.value("baron_levels", json::array()));

    if (!map_file.empty()) {
        auto map_meta = TowerDefenseMapCache::get_instance().get_map(map_file);
        if (map_meta.has_value()) return *map_meta;
    }
    return json::object();
}

/** Extract spawn_points array from map metadata (normalized). */
static json extract_spawn_points(const json& map_metadata) {
    if (map_metadata.contains("spawn_points") && map_metadata["spawn_points"].is_array()) {
        return map_metadata["spawn_points"];
    }
    return json::array();
}

/** Query tower_defense completion count for a character. */
static int query_td_completed_count(sqlite::database& db, int character_id) {
    int count = 0;
    db << "SELECT COUNT(*) FROM mini_game_progress "
          "WHERE character_id = ? AND mini_game = 'tower_defense' AND completed = 1;"
       << character_id
       >> [&](int c) { count = c; };
    return count;
}

/**
 * Load spawn schedule for a specific round from a schedule file.
 * Falls back to procedural generation if file/level/round not found.
 * Overrides out_total_rounds and out_display_name_key from the schedule data.
 */
static json load_spawn_schedule_for_round(
    GameConfigCache& config_cache,
    const std::string& schedule_file,
    int level_id,
    int round_index,
    int difficulty,
    int& out_total_rounds,
    std::string& out_display_name_key,
    int completed_count = 0,
    const json& spawn_points = json::array())
{
    if (schedule_file.empty() || level_id <= 0) {
        return generate_td_spawn_schedule(config_cache, difficulty, round_index, completed_count, spawn_points);
    }

    auto schedule_opt = config_cache.getTowerDefenseSpawnSchedule(schedule_file);
    if (!schedule_opt.has_value()) {
        std::cerr << "[tdRound] Schedule file '" << schedule_file << "' not found, falling back" << std::endl;
        return generate_td_spawn_schedule(config_cache, difficulty, round_index, completed_count, spawn_points);
    }

    const json& data = *schedule_opt;
    std::string level_key = std::to_string(level_id);

    if (data.contains("levels") && data["levels"].contains(level_key)) {
        const json& level = data["levels"][level_key];
        if (level.contains("rounds") && level["rounds"].is_array()) {
            int total = static_cast<int>(level["rounds"].size());
            out_total_rounds = total;
            out_display_name_key = data.value("display_name_key", std::string());

            if (round_index >= 0 && round_index < total) {
                json round = level["rounds"][round_index];
                for (auto& entry : round) {
                    if (!entry.contains("spawn_point_id")) {
                        json sp = pick_spawn_point(spawn_points);
                        if (sp.contains("id")) {
                            entry["spawn_point_id"] = sp["id"];
                        }
                    }
                }
                return round;
            }
        }
    }

    std::cerr << "[tdRound] Schedule file '" << schedule_file
              << "' missing level " << level_key << " round " << round_index
              << ", falling back to procedural" << std::endl;
    return generate_td_spawn_schedule(config_cache, difficulty, round_index, completed_count, spawn_points);
}

ApiResponse handleTDRound(GameConfigCache& config_cache, const json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    int character_id = body["character_id"].get<int>();
    // If session_id provided, this is a round completion report
    if (body.contains("session_id") && body["session_id"].is_number_integer()) {
        int session_id = body["session_id"].get<int>();

        try {
            auto& db = Database::getInstance().gameDB();
            auto session = player_state_db::get_game_session(db, session_id);

            if (!session) {
                response.error = "Session not found or already completed";
                return response;
            }

            if (session->character_id != character_id) {
                response.error = "Session does not belong to this character";
                return response;
            }

            std::cerr << "[tdRound] Completion: "
                      << "session=" << session_id
                      << " db_lives=" << session->lives
                      << " db_gold=" << session->gold
                      << " db_round=" << session->current_round
                      << " db_total=" << session->total_rounds
                      << " db_difficulty=" << session->difficulty
                      << " db_state=" << session->state
                      << std::endl;

            int lives_lost = body.value("lives_lost", 0);

            // Calculate gold earned from spawn schedule and leaked enemies
            nlohmann::json leaked = body.value("leaked_enemies", nlohmann::json::object());
            nlohmann::json schedule = player_state_db::load_spawn_schedule(db, session_id);
            int gold_earned = 0;
            if (!schedule.empty()) {
                std::unordered_map<std::string, int> schedule_totals;
                for (auto& entry : schedule) {
                    std::string eid = entry["enemy_id"].get<std::string>();
                    schedule_totals[eid] += entry["count"].get<int>();
                }
                auto& mobs = config_cache.getTowerDefenseMobs()["mobs"];
                for (auto& [eid, total] : schedule_totals) {
                    int leaked_count = leaked.contains(eid) ? leaked[eid].get<int>() : 0;
                    int killed = std::max(0, total - leaked_count);
                    if (killed > 0 && mobs.contains(eid) && mobs[eid].contains("reward_gold")) {
                        gold_earned += killed * mobs[eid]["reward_gold"].get<int>();
                    }
                }
            }

            if (gold_earned > 1000) gold_earned = 1000;

            std::cerr << "[tdRound] Completion body: lives_lost=" << lives_lost
                      << " gold_earned=" << gold_earned
                      << std::endl;

            // Read client placements and calculate gold with placement costs/refunds
            json client_placements = body.value("placements", json::array());
            json old_placements_json = json::parse(session->placements);

            json map_meta_for_gold;
            {
                const auto& mini_games_config = config_cache.getMiniGames();
                if (mini_games_config.contains(session->mini_game)) {
                    map_meta_for_gold = load_map_for_level(mini_games_config[session->mini_game], session->level_id);
                }
            }

            GoldCalculationResult gc = calculateGoldForRound(
                session->gold, gold_earned,
                old_placements_json,
                client_placements,
                config_cache.getTowerDefenseTowers(),
                config_cache.getTowerDefenseUnits(),
                map_meta_for_gold);

            if (!gc.error.empty()) {
                response.error = gc.error;
                return response;
            }

            int new_lives = session->lives - lives_lost;
            int new_gold = gc.new_gold;

            if (new_lives < 0) new_lives = 0;
            if (new_gold < 0) new_gold = 0;

            bool won = (new_lives > 0);
            int old_round = session->current_round;
            int total = session->total_rounds;

            std::cerr << "[tdRound] Computed: new_lives=" << new_lives
                      << " new_gold=" << new_gold
                      << " won=" << won
                      << " old_round=" << old_round
                      << " total=" << total
                      << " take_next_round=" << (won && old_round + 1 < total)
                      << std::endl;

            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

            if (won && old_round + 1 < total) {
                // More rounds remain — advance to next round (don't end game)
                player_state_db::update_game_session(db, session_id, new_lives, new_gold, "active", now, client_placements.dump());

                int advance_completed = query_td_completed_count(db, character_id);
                json advance_spawn_points = json::array();
                std::string next_display_name_key;
                json next_schedule;
                {
                    const auto& mini_games_config = config_cache.getMiniGames();
                    if (mini_games_config.contains(session->mini_game)) {
                        const auto& mg = mini_games_config[session->mini_game];
                        json map_meta = load_map_for_level(mg, session->level_id);
                        advance_spawn_points = extract_spawn_points(map_meta);

                        std::string sched_file = resolve_schedule_file(mg, session->level_id);
                        if (!sched_file.empty()) {
                            json sched = load_spawn_schedule_for_round(config_cache, sched_file, session->level_id, old_round + 1, session->difficulty, total, next_display_name_key, advance_completed, advance_spawn_points);
                            next_schedule = sched;
                        }
                    }
                    if (next_schedule.empty()) {
                        int new_round = old_round + 1;
                        next_schedule = generate_td_spawn_schedule(config_cache, session->difficulty, new_round, advance_completed, advance_spawn_points, schedule, total);
                    }
                }
                player_state_db::store_spawn_schedule(db, session_id, next_schedule);

                response.data["session_id"] = session_id;
                response.data["next_round"] = true;
                response.data["game_over"] = false;
                response.data["won"] = true;
                response.data["round_number"] = old_round + 2;
                response.data["total_rounds"] = total;
                response.data["lives"] = new_lives;
                response.data["gold"] = new_gold;
                response.data["spawn_schedule"] = next_schedule;
                response.data["display_name_key"] = next_display_name_key;
            } else {
                // Game over: won last round or lost — end session normally
                std::string new_state = won ? "won" : "lost";
                player_state_db::update_game_session(db, session_id, new_lives, new_gold, new_state, now, client_placements.dump());

                std::cerr << "[tdRound] Game over: state=" << new_state
                          << " score=" << (new_gold + new_lives * 10)
                          << std::endl;

                int score = new_gold + new_lives * 10;
                auto end_result = player_state_db::end_mini_game(
                    db, character_id, session->mini_game, session->level_id, won, score, now,
                    (session->level_id <= 9) ? 9 : 25
                );

                // Apply level rewards from config
                json level_rewards = json::object();
                if (won) {
                    const auto& mini_games_config = config_cache.getMiniGames();
                    if (mini_games_config.contains(session->mini_game)) {
                        const auto& mg_config = mini_games_config[session->mini_game];
                        auto check_rewards = [&](const json& levels) {
                            for (const auto& lvl : levels) {
                                if (lvl["id"] == session->level_id && lvl.contains("reward")) {
                                    level_rewards = lvl["reward"];
                                    return true;
                                }
                            }
                            return false;
                        };
                        if (mg_config.contains("levels")) {
                            check_rewards(mg_config["levels"]);
                        }
                        if (level_rewards.empty() && mg_config.contains("baron_levels")) {
                            check_rewards(mg_config["baron_levels"]);
                        }
                    }
                }

                // Ongoing (level 0) games pay silver from the shared reward pool
                if (level_rewards.empty() && session->level_id == 0) {
                    fill_ongoing_rewards(config_cache, db, level_rewards, response.data,
                                         character_id, session->mini_game,
                                         session->difficulty, session->total_rounds, won, now);
                }

                // Check and grant milestone unlocks
                if (won && session->mini_game == "tower_defense") {
                    int completed_count = 0;
                    db << "SELECT COUNT(*) FROM mini_game_progress "
                          "WHERE character_id = ? AND mini_game = 'tower_defense' AND completed = 1;"
                       << character_id
                       >> [&](int count) { completed_count = count; };

                    nlohmann::json new_unlocks = UnitUnlockCalculator::check_and_grant_milestones(
                        config_cache, db, character_id, completed_count, now);

                    if (!new_unlocks["new_units"].empty() || !new_unlocks["new_towers"].empty()) {
                        response.data["new_unlocks"] = new_unlocks;
                    }
                }

                player_state_db::clear_current_mini_game(db, character_id, now);

                response.data["session_id"] = session_id;
                response.data["game_over"] = true;
                response.data["won"] = won;
                response.data["lives"] = new_lives;
                response.data["gold"] = new_gold;
                response.data["score"] = score;
                response.data["rewards"] = level_rewards;
                response.data["completed"] = end_result.completed;
                response.data["new_best_score"] = end_result.new_best_score;
                response.data["times_played"] = end_result.new_times_played;
                response.data["all_levels_done"] = end_result.all_levels_done;

                if (end_result.all_levels_done && won) {
                    auto state = player_state_db::get_player_game_state(db, character_id);
                    if (state.game_phase == "initial_mission" && !state.base_unlocked) {
                        player_state_db::earn_land_patent(db, character_id, now);
                        response.data["land_patent_earned"] = true;
                    } else if (state.game_phase == "baron_track") {
                        player_state_db::earn_baron_right(db, character_id, now);
                        response.data["baron_right_earned"] = true;
                    }
                    state = player_state_db::get_player_game_state(db, character_id);
                    response.data["game_phase"] = state.game_phase;
                }
            }

            if (new_token) {
                response.data["token"] = *new_token;
            }
        } catch (const std::exception& e) {
            log_error("handleTDRound(completion)", e.what());
            response.error = std::string("Failed to process round: ") + e.what();
        }

        return response;
    }

    // No session_id: create a new game session (kickoff)
    if (!body.contains("mini_game") || !body["mini_game"].is_string()) {
        response.error = "mini_game required for new session";
        return response;
    }

    std::string mini_game = body["mini_game"].get<std::string>();

    try {
        auto& db = Database::getInstance().gameDB();
        auto state = player_state_db::get_player_game_state(db, character_id);

        // If already in this mini-game, resume the existing session
        if (state.current_mini_game.has_value()) {
            if (*state.current_mini_game == mini_game) {
                auto existing = player_state_db::get_active_session(db, character_id, mini_game);
                if (existing.has_value()) {
                    // Guard against stale sessions where current_round >= total_rounds
                    // (leftover from previous double-call bug or interrupted game)
                    if (existing->current_round >= existing->total_rounds) {
                        std::cerr << "[tdRound] Stale session " << existing->id
                                  << " at round " << existing->current_round
                                  << "/" << existing->total_rounds
                                  << " — clearing and starting fresh" << std::endl;
                        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                        player_state_db::update_game_session(db, existing->id,
                            existing->lives, existing->gold, "interrupted", now);
                        player_state_db::clear_current_mini_game(db, character_id, now);
                    } else {
                        int level_id = existing->level_id;
                    int difficulty = existing->difficulty;

                    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    player_state_db::start_mini_game(db, character_id, mini_game, level_id, now);

                    int resume_completed = query_td_completed_count(db, character_id);

                    std::string resume_display_name_key;
                    json resume_spawn_schedule;
                    json map_metadata = json::object();

                    // First check for a stored schedule from the previous session
                    resume_spawn_schedule = player_state_db::load_spawn_schedule(db, existing->id);

                    if (resume_spawn_schedule.empty()) {
                        const auto& mini_games_config = config_cache.getMiniGames();
                        if (mini_games_config.contains(mini_game)) {
                            const auto& mg = mini_games_config[mini_game];
                            map_metadata = load_map_for_level(mg, level_id);
                            json sp = extract_spawn_points(map_metadata);

                            std::string sched_file = resolve_schedule_file(mg, level_id);
                            if (!sched_file.empty()) {
                                json sched = load_spawn_schedule_for_round(config_cache, sched_file, level_id, existing->current_round, difficulty, existing->total_rounds, resume_display_name_key, resume_completed, sp);
                                resume_spawn_schedule = sched;
                            }
                        }
                        if (resume_spawn_schedule.empty()) {
                            json sp = extract_spawn_points(map_metadata);
                            resume_spawn_schedule = generate_td_spawn_schedule(config_cache, difficulty, existing->current_round, resume_completed, sp);
                        }
                    } else {
                        // Load map metadata separately for the response
                        const auto& mini_games_config = config_cache.getMiniGames();
                        if (mini_games_config.contains(mini_game)) {
                            const auto& mg = mini_games_config[mini_game];
                            map_metadata = load_map_for_level(mg, level_id);
                        }
                    }

                    std::cerr << "[tdRound] Resume: character=" << character_id
                              << " session=" << existing->id
                              << " level_id=" << level_id;
                    {
                        const auto& mini_games_config = config_cache.getMiniGames();
                        bool has_td = mini_games_config.contains(mini_game);
                        std::cerr << " has_td_config=" << has_td;
                        if (has_td) {
                            const auto& mg = mini_games_config[mini_game];
                            std::string mf;
                            if (mg.contains("levels")) {
                                for (const auto& lvl : mg["levels"]) {
                                    if (lvl["id"] == level_id) {
                                        mf = lvl.value("map", std::string());
                                        break;
                                    }
                                }
                            }
                            std::cerr << " map_file='" << mf << "'";
                            if (!mf.empty()) {
                                auto meta = TowerDefenseMapCache::get_instance().get_map(mf);
                                std::cerr << " meta_has=" << meta.has_value();
                                if (meta.has_value()) {
                                    map_metadata = *meta;
                                    std::cerr << " keys=" << map_metadata.size()
                                              << " fn=" << map_metadata.value("image_filename", std::string("?"));
                                }
                            }
                        }
                    }
                    std::cerr << std::endl;

                    response.data["session_id"] = existing->id;
                    response.data["character_id"] = character_id;
                    response.data["mini_game"] = mini_game;
                    response.data["level_id"] = level_id;
                    response.data["difficulty"] = difficulty;
                    int resume_round = existing->current_round + 1;
                    response.data["spawn_schedule"] = resume_spawn_schedule;
                    response.data["round_number"] = resume_round;
                    response.data["total_rounds"] = existing->total_rounds;
                    response.data["lives"] = existing->lives;
                    response.data["gold"] = existing->gold;
                    response.data["map_metadata"] = map_metadata;
                    response.data["resumed"] = true;
                    response.data["display_name_key"] = resume_display_name_key;

                    response.data["mobs"] = config_cache.getTowerDefenseMobs();
                    response.data["towers"] = config_cache.getTowerDefenseTowers();
                    response.data["units"] = config_cache.getTowerDefenseUnits();
                    response.data["projectiles"] = config_cache.getTowerDefenseProjectiles();

                    // Filter towers/units by player unlocks
                    {
                        json unlocks = UnitUnlockCalculator::get_player_unlocks(db, character_id);
                        std::set<std::string> allowed_towers;
                        std::set<std::string> allowed_units;
                        if (unlocks.contains("towers")) {
                            for (const auto& id : unlocks["towers"]) allowed_towers.insert(id.get<std::string>());
                        }
                        if (unlocks.contains("units")) {
                            for (const auto& id : unlocks["units"]) allowed_units.insert(id.get<std::string>());
                        }
                        if (response.data["towers"].contains("towers") && response.data["towers"]["towers"].is_object()) {
                            json filtered = json::object();
                            for (auto it = response.data["towers"]["towers"].begin(); it != response.data["towers"]["towers"].end(); ++it) {
                                if (allowed_towers.count(it.key())) filtered[it.key()] = it.value();
                            }
                            response.data["towers"]["towers"] = filtered;
                        }
                        if (response.data["units"].contains("units") && response.data["units"]["units"].is_object()) {
                            json filtered = json::object();
                            for (auto it = response.data["units"]["units"].begin(); it != response.data["units"]["units"].end(); ++it) {
                                if (allowed_units.count(it.key())) filtered[it.key()] = it.value();
                            }
                            response.data["units"]["units"] = filtered;
                        }
                    }

                    std::cerr << "[tdRound] Resume filtered: "
                              << response.data["towers"]["towers"].size() << " towers, "
                              << response.data["units"]["units"].size() << " units"
                              << std::endl;

                    resolve_td_image_urls(response.data["mobs"]["mobs"], "mobs");
                    resolve_td_image_urls(response.data["towers"]["towers"], "towers");
                    resolve_td_image_urls(response.data["units"]["units"], "units");

                    if (new_token) response.data["token"] = *new_token;
                    return response;
                    }
                }
                // current_mini_game is set to this mini_game but no active session exists
                // stale state from previous version — clear it and proceed fresh
                auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                std::cerr << "[tdRound] Stale current_mini_game cleared for character "
                          << character_id << " at phase " << state.game_phase << std::endl;
                player_state_db::clear_current_mini_game(db, character_id, now);
            } else {
                response.error = "Already in a mini-game: " + *state.current_mini_game;
                return response;
            }
        }

        // Phase transition recovery: all levels done but game phase stuck
        if (state.game_phase == "initial_mission") {
            auto remaining = player_state_db::get_next_incomplete_level(db, character_id, mini_game, 9);
            if (!remaining) {
                auto now_ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                player_state_db::earn_land_patent(db, character_id, now_ts);
                player_state_db::clear_current_mini_game(db, character_id, now_ts);
                response.data["land_patent_earned"] = true;
                response.data["game_phase"] = "land_patent";
                return response;
            }
        }

        // Auto-determine level_id if not provided
        int level_id = 0;
        bool is_random = false;
        json map_metadata = json::object();
        if (body.contains("level_id") && body["level_id"].is_number_integer() && body["level_id"].get<int>() > 0) {
            level_id = body["level_id"].get<int>();
        } else if (state.game_phase == "initial_mission") {
            auto next = player_state_db::get_next_incomplete_level(db, character_id, mini_game, 9);
            if (!next) {
                response.error = "All levels completed for " + mini_game;
                return response;
            }
            level_id = *next;
            if (level_id > 1) {
                bool prev = player_state_db::has_completed_previous_level(db, character_id, mini_game, level_id);
                if (!prev) {
                    response.error = "Previous level not completed";
                    return response;
                }
            }
        } else if (state.game_phase == "baron_track") {
            auto next = player_state_db::get_next_incomplete_level(db, character_id, mini_game, 25);
            if (!next) {
                response.error = "All baron levels completed";
                return response;
            }
            level_id = *next;
            if (level_id > 1) {
                bool prev = player_state_db::has_completed_previous_level(db, character_id, mini_game, level_id);
                if (!prev) {
                    response.error = "Previous level not completed";
                    return response;
                }
            }
        } else {
            is_random = true;
            level_id = 0;
        }

        // Determine difficulty and rounds from level config
        int difficulty = 1;
        int total_rounds = 1;
        json spawn_schedule;
        std::string campaign_display_name_key;
        const auto& mini_games_config = config_cache.getMiniGames();

        // Custom game override: rounds and difficulty provided by client
        if (body.contains("rounds") && body["rounds"].is_number_integer() &&
            body.contains("difficulty") && body["difficulty"].is_number_integer()) {
            level_id = 0;
            difficulty = body["difficulty"].get<int>();
            total_rounds = body["rounds"].get<int>();
            is_random = true;

            // Validate against the ongoing config so clients can't pick settings
            // the server hasn't offered
            const json* ongoing_config = resolve_ongoing_config(config_cache, mini_game);
            if (ongoing_config && !ongoing_rewards::validate_ongoing_options(*ongoing_config, difficulty, total_rounds)) {
                response.error = "Invalid rounds or difficulty for ongoing game";
                return response;
            }

            json empty_sp = json::array();
            int kickoff_completed = 0;
            if (character_id > 0) kickoff_completed = query_td_completed_count(db, character_id);
            spawn_schedule = generate_td_spawn_schedule(config_cache, difficulty, 0, kickoff_completed, empty_sp);
            // Load default map metadata for custom games
            if (mini_games_config.contains(mini_game)) {
                const auto& mg = mini_games_config[mini_game];
                if (mg.contains("levels") && mg["levels"].is_array() && !mg["levels"].empty()) {
                    int first_id = mg["levels"][0]["id"].get<int>();
                    map_metadata = load_map_for_level(mg, first_id);
                }
            }
        } else {
        if (mini_games_config.contains(mini_game)) {
            const auto& mg_config = mini_games_config[mini_game];
            auto find_field = [&](const json& levels, const std::string& field, int def) -> int {
                for (const auto& lvl : levels) {
                    if (lvl["id"] == level_id) {
                        return lvl.value(field, def);
                    }
                }
                return -1;
            };
            bool is_baron = false;
            int d = find_field(mg_config.value("levels", json::array()), "difficulty", 1);
            if (d < 0) {
                d = find_field(mg_config.value("baron_levels", json::array()), "difficulty", 1);
                is_baron = true;
            }
            if (d >= 0) difficulty = d;
            int r = find_field(mg_config.value("levels", json::array()), "rounds", 1);
            if (r < 0) r = find_field(mg_config.value("baron_levels", json::array()), "rounds", 1);
            if (r >= 0) total_rounds = r;

            // Resolve schedule file for this level
            std::string schedule_file;
            if (!is_baron && mg_config.contains("schedule_file")) {
                schedule_file = mg_config["schedule_file"].get<std::string>();
            } else if (is_baron && mg_config.contains("baron_schedule_file")) {
                schedule_file = mg_config["baron_schedule_file"].get<std::string>();
            }

            // Load map metadata and query completion count
            map_metadata = load_map_for_level(mg_config, level_id);
            json local_spawn_points = extract_spawn_points(map_metadata);
            int kickoff_completed = query_td_completed_count(db, character_id);

            if (!schedule_file.empty()) {
                json sched = load_spawn_schedule_for_round(config_cache, schedule_file, level_id, 0, difficulty, total_rounds, campaign_display_name_key, kickoff_completed, local_spawn_points);
                spawn_schedule = sched;
                std::cerr << "[tdRound] Using schedule file '" << schedule_file
                          << "' for level " << level_id
                          << " total_rounds=" << total_rounds
                          << " display_name_key=" << campaign_display_name_key << std::endl;
            } else {
                spawn_schedule = generate_td_spawn_schedule(config_cache, difficulty, 0, kickoff_completed, local_spawn_points);
            }
        } else {
            json empty_sp = json::array();
            int kickoff_completed = 0;
            if (character_id > 0) kickoff_completed = query_td_completed_count(db, character_id);
            spawn_schedule = generate_td_spawn_schedule(config_cache, difficulty, 0, kickoff_completed, empty_sp);
        }
        }

        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        // Grant starting unlocks on first play
        UnitUnlockCalculator::grant_starting_unlocks(config_cache, db, character_id, now);

        player_state_db::start_mini_game(db, character_id, mini_game, level_id, now);

        auto session = player_state_db::create_game_session(db, character_id, mini_game, level_id, difficulty, total_rounds, now);
        player_state_db::store_spawn_schedule(db, session.id, spawn_schedule);

        // (map_metadata already loaded above for spawn schedule generation)

        // Log diagnostics for map loading
        std::cerr << "[tdRound] Kickoff: character=" << character_id
                  << " phase=" << state.game_phase
                  << " level_id=" << level_id
                  << " is_random=" << is_random
                  << " map_file=";
        if (mini_games_config.contains(mini_game)) {
            const auto& mg_config = mini_games_config[mini_game];
            std::string mf;
            for (const auto& lvl : mg_config.value("levels", json::array())) {
                if (lvl["id"] == level_id && lvl.contains("map")) {
                    mf = lvl["map"].get<std::string>();
                    break;
                }
            }
            std::cerr << (mf.empty() ? "(none)" : mf);
        } else {
            std::cerr << "no_mini_games_config";
        }
        std::cerr << " map_metadata_keys=" << map_metadata.size()
                  << " image_filename=";
        if (map_metadata.contains("image_filename")) {
            std::cerr << map_metadata["image_filename"].get<std::string>();
        } else {
            std::cerr << "(missing)";
        }
        std::cerr << std::endl;

        response.data["session_id"] = session.id;
        response.data["character_id"] = character_id;
        response.data["mini_game"] = mini_game;
        response.data["level_id"] = level_id;
        response.data["difficulty"] = difficulty;
        response.data["round_number"] = 1;
        response.data["total_rounds"] = total_rounds;
        response.data["lives"] = session.lives;
        response.data["gold"] = session.gold;
        response.data["spawn_schedule"] = spawn_schedule;
        response.data["display_name_key"] = campaign_display_name_key;
        response.data["map_metadata"] = map_metadata;

        // Load full catalogs for the client
        response.data["mobs"] = config_cache.getTowerDefenseMobs();
        response.data["towers"] = config_cache.getTowerDefenseTowers();
        response.data["units"] = config_cache.getTowerDefenseUnits();
        response.data["projectiles"] = config_cache.getTowerDefenseProjectiles();

        // Filter towers/units by player unlocks
        {
            json unlocks = UnitUnlockCalculator::get_player_unlocks(db, character_id);
            std::set<std::string> allowed_towers;
            std::set<std::string> allowed_units;
            if (unlocks.contains("towers")) {
                for (const auto& id : unlocks["towers"]) allowed_towers.insert(id.get<std::string>());
            }
            if (unlocks.contains("units")) {
                for (const auto& id : unlocks["units"]) allowed_units.insert(id.get<std::string>());
            }
            if (response.data["towers"].contains("towers") && response.data["towers"]["towers"].is_object()) {
                json filtered = json::object();
                for (auto it = response.data["towers"]["towers"].begin(); it != response.data["towers"]["towers"].end(); ++it) {
                    if (allowed_towers.count(it.key())) filtered[it.key()] = it.value();
                }
                response.data["towers"]["towers"] = filtered;
            }
            if (response.data["units"].contains("units") && response.data["units"]["units"].is_object()) {
                json filtered = json::object();
                for (auto it = response.data["units"]["units"].begin(); it != response.data["units"]["units"].end(); ++it) {
                    if (allowed_units.count(it.key())) filtered[it.key()] = it.value();
                }
                response.data["units"]["units"] = filtered;
            }
        }

        std::cerr << "[tdRound] Filtered: "
                  << response.data["towers"]["towers"].size() << " towers, "
                  << response.data["units"]["units"].size() << " units"
                  << std::endl;

        resolve_td_image_urls(response.data["mobs"]["mobs"], "mobs");
        resolve_td_image_urls(response.data["towers"]["towers"], "towers");
        resolve_td_image_urls(response.data["units"]["units"], "units");
        resolve_td_image_urls(response.data["projectiles"]["projectiles"], "projectiles");

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleTDRound(kickoff)", e.what());
        response.error = std::string("Failed to start TD session: ") + e.what();
    }

    return response;
}

ApiResponse handleWeedingStart(GameConfigCache& config_cache, const json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    int character_id = body["character_id"].get<int>();

    try {
        auto& db = Database::getInstance().gameDB();
        auto state = player_state_db::get_player_game_state(db, character_id);

        // Check not already in another mini-game
        if (state.current_mini_game.has_value()) {
            if (*state.current_mini_game == "weeding") {
                // Try to resume active session
                auto existing = player_state_db::get_active_weeding_session(db, character_id);
                if (existing.has_value()) {
                    auto& sess = *existing;
                    auto& sj = sess.session_json;

                    // Load fresh configs
                    const auto& plants = config_cache.getWeedingPlants();
                    const auto& tools = config_cache.getWeedingTools();
                    const auto& specials = config_cache.getWeedingSpecials();

                    // Reload map metadata
                    std::string map_id = sj.value("map_id", std::string("map_1.json"));
                    nlohmann::json map_meta;
                    auto map_opt = config_cache.loadWeedingMap(map_id);
                    if (map_opt.has_value()) {
                        map_meta = *map_opt;
                    } else {
                        map_meta["id"] = "map_1";
                        map_meta["image_file"] = "map_1.png";
                        map_meta["grid_bounds"] = {{"x", 0.18}, {"y", 0.18}, {"width", 0.59}, {"height", 0.73}};
                        map_meta["grid_size"] = 4;
                        map_meta["out_of_bounds"] = json::array();
                        map_meta["par"] = sj.value("par", 12);
                    }

                    // Convert configs to arrays
                    json tools_arr = json::array();
                    for (auto it = tools.begin(); it != tools.end(); ++it) {
                        json entry = it.value();
                        entry["id"] = it.key();
                        tools_arr.push_back(entry);
                    }
                    json plants_arr = json::array();
                    for (auto it = plants.begin(); it != plants.end(); ++it) {
                        json entry = it.value();
                        entry["id"] = it.key();
                        plants_arr.push_back(entry);
                    }
                    json specials_arr = json::array();
                    for (auto it = specials.begin(); it != specials.end(); ++it) {
                        json entry = it.value();
                        entry["id"] = it.key();
                        specials_arr.push_back(entry);
                    }

                    // Load level config to apply filtering and text
                    json allowed_weeds;
                    json allowed_tools_list;
                    json allowed_smother_crops;
                    json text_intro_val;
                    json text_outro_val;
                    {
                        const auto& mini_games_config = config_cache.getMiniGames();
                        if (mini_games_config.contains("weeding")) {
                            const auto& weeding_config = mini_games_config["weeding"];
                            std::string sched_file = resolve_schedule_file(weeding_config, sess.level_id);
                            if (!sched_file.empty()) {
                                auto level_config_opt = config_cache.loadWeedingLevelConfig(sched_file);
                                if (level_config_opt.has_value()) {
                                    const auto& level_config = *level_config_opt;
                                    std::string lid_str = std::to_string(sess.level_id);
                                    if (level_config.contains("levels") && level_config["levels"].contains(lid_str)) {
                                        const auto& lvl = level_config["levels"][lid_str];
                                        if (lvl.contains("allowed_weeds") && lvl["allowed_weeds"].is_array()) {
                                            allowed_weeds = lvl["allowed_weeds"];
                                        }
                                        if (lvl.contains("allowed_tools") && lvl["allowed_tools"].is_array()) {
                                            allowed_tools_list = lvl["allowed_tools"];
                                        }
                                        if (lvl.contains("allowed_smother_crops") && lvl["allowed_smother_crops"].is_array()) {
                                            allowed_smother_crops = lvl["allowed_smother_crops"];
                                        }
                                        if (lvl.contains("text_intro")) text_intro_val = lvl["text_intro"];
                                        if (lvl.contains("text_outro")) text_outro_val = lvl["text_outro"];
                                    }
                                }
                            }
                        }
                    }

                    // Filter tools by allowed_tools if specified
                    if (!allowed_tools_list.is_null() && allowed_tools_list.is_array()) {
                        json filtered = json::array();
                        for (auto& t : tools_arr) {
                            for (auto& a : allowed_tools_list) {
                                if (t["id"] == a) {
                                    filtered.push_back(t);
                                    break;
                                }
                            }
                        }
                        tools_arr = std::move(filtered);
                    }

                    // Filter plants by allowed_weeds / allowed_smother_crops if specified
                    if (!allowed_weeds.is_null() || !allowed_smother_crops.is_null()) {
                        json filtered = json::array();
                        for (auto& p : plants_arr) {
                            bool is_smother = p.contains("is_smother_crop") && p["is_smother_crop"].get<bool>();
                            bool include = false;
                            if (is_smother) {
                                if (allowed_smother_crops.is_null()) {
                                    include = true;
                                } else {
                                    for (auto& a : allowed_smother_crops) {
                                        if (p["id"] == a) { include = true; break; }
                                    }
                                }
                            } else {
                                if (allowed_weeds.is_null()) {
                                    include = true;
                                } else {
                                    for (auto& a : allowed_weeds) {
                                        if (p["id"] == a) { include = true; break; }
                                    }
                                }
                            }
                            if (include) filtered.push_back(p);
                        }
                        plants_arr = std::move(filtered);
                    }

                    // Build response from session state + fresh configs
                    response.data["session_id"] = sess.id;
                    response.data["character_id"] = character_id;
                    response.data["level_id"] = sess.level_id;
                    response.data["board"] = sj["board"];
                    response.data["grid_size"] = sj["grid_size"];
                    response.data["round"] = sj.value("round", 1);
                    response.data["actions_remaining"] = sj.value("actions_remaining", 2);
                    response.data["equipped_tool"] = sj.value("equipped_tool", "sickle");
                    response.data["par"] = sj.value("par", 12);
                    response.data["won"] = sj.value("won", false);
                    response.data["score"] = sj.value("score", 0);
                    response.data["available_tools"] = tools_arr;
                    response.data["available_plants"] = plants_arr;
                    response.data["available_specials"] = specials_arr;
                    response.data["map_metadata"] = map_meta;
                    response.data["message"] = "Weeding session resumed";

                    if (!text_intro_val.is_null()) {
                        response.data["text_intro_id"] = text_intro_val;
                    }
                    if (!text_outro_val.is_null()) {
                        response.data["text_outro_id"] = text_outro_val;
                    }

                    if (new_token) {
                        response.data["token"] = *new_token;
                    }
                    return response;
                }
                // Stale current_mini_game — clear it and start fresh
                auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                player_state_db::clear_current_mini_game(db, character_id, now);
            } else {
                response.error = "Already in a mini-game: " + *state.current_mini_game;
                return response;
            }
        }

        // Phase transition recovery: all levels done but game phase stuck
        if (state.game_phase == "initial_mission") {
            auto remaining = player_state_db::get_next_incomplete_level(db, character_id, "weeding", 9);
            if (!remaining) {
                auto now_ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                player_state_db::earn_land_patent(db, character_id, now_ts);
                player_state_db::clear_current_mini_game(db, character_id, now_ts);
                response.data["land_patent_earned"] = true;
                response.data["game_phase"] = "land_patent";
                return response;
            }
        }

        // Determine level_id based on game phase
        int level_id = 0;
        if (body.contains("level_id") && body["level_id"].is_number_integer() && body["level_id"].get<int>() > 0) {
            level_id = body["level_id"].get<int>();
        } else if (state.game_phase == "initial_mission") {
            auto next = player_state_db::get_next_incomplete_level(db, character_id, "weeding", 9);
            if (!next) {
                response.error = "All weeding levels completed";
                return response;
            }
            level_id = *next;
            if (level_id > 1) {
                bool prev = player_state_db::has_completed_previous_level(db, character_id, "weeding", level_id);
                if (!prev) {
                    response.error = "Previous level not completed";
                    return response;
                }
            }
        } else if (state.game_phase == "baron_track") {
            auto next = player_state_db::get_next_incomplete_level(db, character_id, "weeding", 25);
            if (!next) {
                response.error = "All baron weeding levels completed";
                return response;
            }
            level_id = *next;
            if (level_id > 1) {
                bool prev = player_state_db::has_completed_previous_level(db, character_id, "weeding", level_id);
                if (!prev) {
                    response.error = "Previous level not completed";
                    return response;
                }
            }
        } else {
            level_id = 0;
        }

        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        // Set current_mini_game
        player_state_db::start_mini_game(db, character_id, "weeding", level_id, now);

        // Load weeding configs
        const auto& plants = config_cache.getWeedingPlants();
        const auto& tools = config_cache.getWeedingTools();
        const auto& specials = config_cache.getWeedingSpecials();

        // Determine difficulty and map
        int difficulty = 1;
        int ongoing_grid_size_override = 0;
        std::string map_filename = "map_1.json";
        nlohmann::json map_meta;
        json allowed_weeds;
        json allowed_tools_list;
        json allowed_smother_crops;
        json text_intro_val;
        json text_outro_val;

        if (level_id > 0) {
            // Resolve level config file (wildlands_marche.json or great_wildlands_marche.json)
            const auto& mini_games_config = config_cache.getMiniGames();
            if (mini_games_config.contains("weeding")) {
                const auto& weeding_config = mini_games_config["weeding"];
                std::string sched_file = resolve_schedule_file(weeding_config, level_id);
                if (!sched_file.empty()) {
                    auto level_config_opt = config_cache.loadWeedingLevelConfig(sched_file);
                    if (level_config_opt.has_value()) {
                        const auto& level_config = *level_config_opt;
                        std::string lid_str = std::to_string(level_id);
                        if (level_config.contains("levels") && level_config["levels"].contains(lid_str)) {
                            const auto& lvl = level_config["levels"][lid_str];
                            map_filename = lvl.value("map", map_filename);
                            difficulty = lvl.value("difficulty", 1);
                            if (lvl.contains("allowed_weeds") && lvl["allowed_weeds"].is_array()) {
                                allowed_weeds = lvl["allowed_weeds"];
                            }
                            if (lvl.contains("allowed_tools") && lvl["allowed_tools"].is_array()) {
                                allowed_tools_list = lvl["allowed_tools"];
                            }
                            if (lvl.contains("allowed_smother_crops") && lvl["allowed_smother_crops"].is_array()) {
                                allowed_smother_crops = lvl["allowed_smother_crops"];
                            }
                            if (lvl.contains("text_intro")) text_intro_val = lvl["text_intro"];
                            if (lvl.contains("text_outro")) text_outro_val = lvl["text_outro"];
                        }
                    }
                }
            }
            if (difficulty < 1) difficulty = 1;
        } else {
            // Ongoing (level 0): difficulty and grid size selected by the player,
            // validated against the server's ongoing config
            difficulty = body.value("difficulty", 1);
            int requested_grid_size = body.value("grid_size", 0);
            const json* ongoing_config = resolve_ongoing_config(config_cache, "weeding");
            if (!ongoing_config) {
                response.error = "Ongoing weeding config unavailable";
                return response;
            }
            int default_size = ongoing_config->value("default_size", 4);
            if (requested_grid_size <= 0) requested_grid_size = default_size;
            if (!ongoing_rewards::validate_ongoing_options(*ongoing_config, difficulty, requested_grid_size)) {
                response.error = "Invalid difficulty or grid size for ongoing weeding";
                return response;
            }
            map_filename = "map_1.json";
            ongoing_grid_size_override = requested_grid_size;
        }

        auto map_opt = config_cache.loadWeedingMap(map_filename);
        if (map_opt.has_value()) {
            map_meta = *map_opt;
        } else {
            // Fallback map metadata
            map_meta["id"] = "map_1";
            map_meta["image_file"] = "map_1.png";
            map_meta["grid_bounds"] = {{"x", 0.18}, {"y", 0.18}, {"width", 0.59}, {"height", 0.73}};
            map_meta["grid_size"] = 4;
            map_meta["out_of_bounds"] = json::array();
            map_meta["par"] = 12;
        }

        int grid_size = map_meta["grid_size"].get<int>();
        if (ongoing_grid_size_override > 0) {
            grid_size = ongoing_grid_size_override;
            map_meta["grid_size"] = grid_size;
        }

        // Parse out_of_bounds
        std::vector<std::pair<int, int>> out_of_bounds;
        if (map_meta.contains("out_of_bounds") && map_meta["out_of_bounds"].is_array()) {
            for (const auto& oob : map_meta["out_of_bounds"]) {
                if (oob.contains("x") && oob.contains("y")) {
                    out_of_bounds.push_back({oob["x"].get<int>(), oob["y"].get<int>()});
                }
            }
        }

        // Initialize RNG
        std::mt19937 rng(static_cast<unsigned>(now + character_id + level_id));

        // Initialize board
        nlohmann::json board = weeding_logic::initialize_board(config_cache, grid_size, out_of_bounds, difficulty, allowed_weeds, rng);

        // Compute par
        int par = weeding_logic::estimate_weed_clearing_rounds(board, grid_size, plants, difficulty);
        if (par < 1) par = 1;

        // Build initial session state JSON
        json session_json;
        session_json["board"] = board;
        session_json["grid_size"] = grid_size;
        session_json["difficulty"] = difficulty;
        session_json["round"] = 1;
        session_json["actions_remaining"] = 2;
        session_json["last_action_was_switch"] = false;
        session_json["equipped_tool"] = "sickle";
        session_json["par"] = par;
        session_json["won"] = false;
        session_json["score"] = 0;
        session_json["map_id"] = map_filename;

        // Create weeding session in DB
        auto session = player_state_db::create_weeding_session(db, character_id, level_id, now, session_json);

        // Convert tools object to array
        json tools_arr = json::array();
        for (auto it = tools.begin(); it != tools.end(); ++it) {
            json tool_entry = it.value();
            tool_entry["id"] = it.key();
            tools_arr.push_back(tool_entry);
        }

        // Filter tools by allowed_tools if specified
        if (!allowed_tools_list.is_null() && allowed_tools_list.is_array()) {
            json filtered = json::array();
            for (auto& t : tools_arr) {
                for (auto& a : allowed_tools_list) {
                    if (t["id"] == a) {
                        filtered.push_back(t);
                        break;
                    }
                }
            }
            tools_arr = std::move(filtered);
        }

        // Convert plants object to array
        json plants_arr = json::array();
        for (auto it = plants.begin(); it != plants.end(); ++it) {
            json plant_entry = it.value();
            plant_entry["id"] = it.key();
            plants_arr.push_back(plant_entry);
        }

        // Filter plants by allowed_weeds / allowed_smother_crops if specified
        if (!allowed_weeds.is_null() || !allowed_smother_crops.is_null()) {
            json filtered = json::array();
            for (auto& p : plants_arr) {
                bool is_smother = p.contains("is_smother_crop") && p["is_smother_crop"].get<bool>();
                bool include = false;
                if (is_smother) {
                    if (allowed_smother_crops.is_null()) {
                        include = true;
                    } else {
                        for (auto& a : allowed_smother_crops) {
                            if (p["id"] == a) { include = true; break; }
                        }
                    }
                } else {
                    if (allowed_weeds.is_null()) {
                        include = true;
                    } else {
                        for (auto& a : allowed_weeds) {
                            if (p["id"] == a) { include = true; break; }
                        }
                    }
                }
                if (include) filtered.push_back(p);
            }
            plants_arr = std::move(filtered);
        }

        // Convert specials object to array
        json specials_arr = json::array();
        for (auto it = specials.begin(); it != specials.end(); ++it) {
            json special_entry = it.value();
            special_entry["id"] = it.key();
            specials_arr.push_back(special_entry);
        }

        // Build response
        response.data["session_id"] = session.id;
        response.data["character_id"] = character_id;
        response.data["level_id"] = level_id;
        response.data["board"] = board;
        response.data["grid_size"] = grid_size;
        response.data["round"] = 1;
        response.data["actions_remaining"] = 2;
        response.data["equipped_tool"] = "sickle";
        response.data["par"] = par;
        response.data["won"] = false;
        response.data["score"] = 0;
        response.data["available_tools"] = tools_arr;
        response.data["available_plants"] = plants_arr;
        response.data["available_specials"] = specials_arr;
        response.data["map_metadata"] = map_meta;
        response.data["message"] = "Weeding session started";

        if (!text_intro_val.is_null()) {
            response.data["text_intro_id"] = text_intro_val;
        }
        if (!text_outro_val.is_null()) {
            response.data["text_outro_id"] = text_outro_val;
        }

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleWeedingStart", e.what());
        response.error = std::string("Failed to start weeding: ") + e.what();
    }

    return response;
}

ApiResponse handleWeedingTurn(GameConfigCache& config_cache, const json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    if (!body.contains("session_id") || !body["session_id"].is_number_integer()) {
        response.error = "session_id required";
        return response;
    }

    int character_id = body["character_id"].get<int>();
    int session_id = body["session_id"].get<int>();

    try {
        auto& db = Database::getInstance().gameDB();

        // Get session
        auto session_opt = player_state_db::get_weeding_session(db, session_id);
        if (!session_opt.has_value()) {
            response.error = "Weeding session not found or already completed";
            return response;
        }

        auto session = *session_opt;
        if (session.character_id != character_id) {
            response.error = "Session does not belong to this character";
            return response;
        }

        if (session.state != "active") {
            response.error = "Session is not active (state: " + session.state + ")";
            return response;
        }

        if (!session.session_json.is_object() || session.session_json.empty()) {
            response.error = "Session has invalid state data";
            return response;
        }

        // Check forfeit separately
        if (body.contains("action_type") && body["action_type"].is_string() && body["action_type"].get<std::string>() == "forfeit") {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            player_state_db::update_weeding_session(db, session_id, session.session_json, "forfeited", now);
            player_state_db::clear_current_mini_game(db, character_id, now);

            // Process end_mini_game for forfeit
            auto end_result = player_state_db::end_mini_game(db, character_id, "weeding", session.level_id, false, 0, now,
                (session.level_id <= 9) ? 9 : 25);

            response.data["won"] = false;
            response.data["score"] = 0;
            response.data["completed"] = false;
            response.data["game_over"] = true;
            response.data["new_best_score"] = end_result.new_best_score;
            response.data["times_played"] = end_result.new_times_played;
            response.data["message"] = "Forfeited";

            auto updated_state = player_state_db::get_player_game_state(db, character_id);
            response.data["game_phase"] = updated_state.game_phase;

            if (new_token) {
                response.data["token"] = *new_token;
            }
            return response;
        }

        // Parse actions array
        if (!body.contains("actions") || !body["actions"].is_array()) {
            response.error = "actions array required";
            return response;
        }

        auto actions = body["actions"];
        if (actions.empty()) {
            response.error = "actions array cannot be empty";
            return response;
        }

        // Initialize RNG using session_id and round number
        int current_round = session.session_json.value("round", 1);
        std::mt19937 rng(static_cast<unsigned>(
            std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) +
            session_id * 1000 +
            current_round));

        // Save board before any actions for final diff
        nlohmann::json old_board = session.session_json["board"];

        // Process all actions sequentially
        std::optional<WeedingActionResult> last_result;
        for (const auto& action_body : actions) {
            WeedingActionRequest action_req;
            action_req.action_type = action_body.value("action_type", std::string(""));
            action_req.tool_id = action_body.value("tool_id", std::string(""));
            action_req.target_x = action_body.value("target_x", -1);
            action_req.target_y = action_body.value("target_y", -1);
            action_req.crop_id = action_body.value("crop_id", std::string(""));

            if (action_req.action_type.empty()) {
                response.error = "action_type required in action";
                return response;
            }

            auto result = weeding_logic::process_action(config_cache, session.session_json, action_req, rng, false);

            if (!result.valid) {
                response.error = result.error;
                return response;
            }

            last_result = result;
            if (result.won) break;
        }

        if (!last_result.has_value()) {
            response.error = "No actions processed";
            return response;
        }

        auto& result = *last_result;

        // Extract single diff from before all actions to after all actions
        int gs = session.session_json["grid_size"].get<int>();
        auto final_changes = weeding_logic::extract_board_changes(old_board, session.session_json["board"], gs);

        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        // One DB write
        player_state_db::update_weeding_session(db, session_id, session.session_json,
            result.won ? "won" : "active", now);

        response.data["round"] = result.round;
        response.data["actions_remaining"] = result.actions_remaining;
        response.data["equipped_tool"] = result.equipped_tool;
        response.data["board_changes"] = final_changes;
        response.data["won"] = result.won;
        response.data["score"] = result.score;
        response.data["par"] = result.par;
        response.data["message"] = result.won ? result.message : "";
        response.data["board"] = session.session_json["board"];

        if (result.won) {
            // Game over — finalize
            player_state_db::clear_current_mini_game(db, character_id, now);

            auto end_result = player_state_db::end_mini_game(db, character_id, "weeding", session.level_id, true, result.score, now,
                (session.level_id <= 9) ? 9 : 25);

            response.data["game_over"] = true;
            response.data["completed"] = end_result.completed;
            response.data["new_best_score"] = end_result.new_best_score;
            response.data["times_played"] = end_result.new_times_played;
            response.data["all_levels_done"] = end_result.all_levels_done;

            // Phase transitions
            if (end_result.all_levels_done) {
                auto pstate = player_state_db::get_player_game_state(db, character_id);
                if (pstate.game_phase == "initial_mission" && !pstate.base_unlocked) {
                    player_state_db::earn_land_patent(db, character_id, now);
                    response.data["land_patent_earned"] = true;
                    // Award completion bonus
                    const auto& mini_games_config = config_cache.getMiniGames();
                    if (mini_games_config.contains("weeding") && mini_games_config["weeding"].contains("completion_bonus")) {
                        const auto& bonus = mini_games_config["weeding"]["completion_bonus"];
                        if (bonus.contains("resources")) {
                            response.data["completion_bonus"] = bonus["resources"];
                        }
                    }
                } else if (pstate.game_phase == "baron_track") {
                    player_state_db::earn_baron_right(db, character_id, now);
                    response.data["baron_right_earned"] = true;
                }
                pstate = player_state_db::get_player_game_state(db, character_id);
                response.data["game_phase"] = pstate.game_phase;
            } else {
                auto pstate = player_state_db::get_player_game_state(db, character_id);
                response.data["game_phase"] = pstate.game_phase;
            }

            // Level rewards
            json level_rewards = json::object();
            const auto& mini_games_config = config_cache.getMiniGames();
            if (mini_games_config.contains("weeding")) {
                const auto& mg = mini_games_config["weeding"];
                auto check = [&](const json& levels) {
                    for (const auto& lvl : levels) {
                        if (lvl["id"] == session.level_id && lvl.contains("reward")) {
                            level_rewards = lvl["reward"];
                            return true;
                        }
                    }
                    return false;
                };
                if (mg.contains("levels")) check(mg["levels"]);
                if (level_rewards.empty() && mg.contains("baron_levels")) check(mg["baron_levels"]);
            }

            // Ongoing (level 0) games pay silver from the shared reward pool
            if (level_rewards.empty() && session.level_id == 0) {
                int wd_size = session.session_json.value("grid_size", 0);
                int wd_difficulty = session.session_json.value("difficulty", 1);
                fill_ongoing_rewards(config_cache, db, level_rewards, response.data,
                                     character_id, "weeding", wd_difficulty, wd_size, true, now);
            }
            response.data["rewards"] = level_rewards;
        }

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleWeedingTurn", e.what());
        response.error = std::string("Failed to process weeding turn: ") + e.what();
    }

    return response;
}

ApiResponse handleStartMiniGame(GameConfigCache& config_cache, const json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    if (!body.contains("mini_game") || !body["mini_game"].is_string()) {
        response.error = "mini_game required";
        return response;
    }

    int character_id = body["character_id"].get<int>();
    std::string mini_game = body["mini_game"].get<std::string>();

    MiniGameHandler* handler = g_mini_games->get(mini_game);

    if (!handler) {
        response.error = "Unknown mini_game: " + mini_game;
        return response;
    }

    try {
        auto& db = Database::getInstance().gameDB();
        auto state = player_state_db::get_player_game_state(db, character_id);

        if (state.current_mini_game.has_value()) {
            response.error = "Already in a mini-game: " + *state.current_mini_game;
            return response;
        }

        int level_id = 0;
        bool is_random = false;

        if (state.game_phase == "initial_mission") {
            auto next_level = player_state_db::get_next_incomplete_level(db, character_id, mini_game, 9);

            if (!next_level) {
                response.error = "All levels completed for " + mini_game;
                return response;
            }

            level_id = *next_level;

            if (level_id > 1) {
                bool prev_completed = player_state_db::has_completed_previous_level(db, character_id, mini_game, level_id);
                if (!prev_completed) {
                    response.error = "Previous level not completed";
                    return response;
                }
            }
        } else if (state.game_phase == "baron_track") {
            auto next_level = player_state_db::get_next_incomplete_level(db, character_id, mini_game, 25);

            if (!next_level) {
                response.error = "All baron levels completed";
                return response;
            }

            level_id = *next_level;

            if (level_id > 1) {
                bool prev_completed = player_state_db::has_completed_previous_level(db, character_id, mini_game, level_id);
                if (!prev_completed) {
                    response.error = "Previous level not completed";
                    return response;
                }
            }
        } else {
            is_random = true;
            level_id = 0;
        }

        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        player_state_db::start_mini_game(db, character_id, mini_game, level_id, now);

        MiniGameContext ctx;
        ctx.character_id = character_id;
        ctx.level_id = level_id;
        ctx.is_random_generation = is_random;

        nlohmann::json level_config = handler->start_level(ctx);
        response.data = level_config;
        response.data["character_id"] = character_id;

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleStartMiniGame", e.what());
        response.error = std::string("Failed to start mini-game: ") + e.what();
    }

    return response;
}

ApiResponse handleEndMiniGame(GameConfigCache& config_cache, const json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    if (!body.contains("mini_game") || !body["mini_game"].is_string()) {
        response.error = "mini_game required";
        return response;
    }

    if (!body.contains("won") || !body["won"].is_boolean()) {
        response.error = "won (boolean) required";
        return response;
    }

    int character_id = body["character_id"].get<int>();
    std::string mini_game = body["mini_game"].get<std::string>();
    bool won = body["won"].get<bool>();
    int score = body.value("score", 0);

        int level_id = 0;
        if (body.contains("level_id") && body["level_id"].is_number_integer()) {
            level_id = body["level_id"].get<int>();
        }

        MiniGameHandler* handler = g_mini_games->get(mini_game);

    if (!handler) {
        response.error = "Unknown mini_game: " + mini_game;
        return response;
    }

    try {
        auto& db = Database::getInstance().gameDB();
        auto state = player_state_db::get_player_game_state(db, character_id);

        // Determine expected total levels based on game phase
        int expected_total_levels = (state.game_phase == "baron_track") ? 25 : 9;

        if (!state.current_mini_game.has_value() || *state.current_mini_game != mini_game) {
            response.error = "Not currently playing " + mini_game;
            return response;
        }

        if (level_id == 0 && state.current_level_id.has_value()) {
            level_id = *state.current_level_id;
        }

        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        MiniGameContext ctx;
        ctx.character_id = character_id;
        ctx.level_id = level_id;
        ctx.is_random_generation = (state.game_phase != "initial_mission");

        MiniGameResult result;
        result.won = won;
        result.score = score;

        nlohmann::json outcome = handler->end_level(ctx, result);

        auto end_result = player_state_db::end_mini_game(db, character_id, mini_game, level_id, won, score, now, expected_total_levels);

        response.data["completed"] = end_result.completed;
        response.data["score"] = score;
        response.data["new_best_score"] = end_result.new_best_score;
        response.data["times_played"] = end_result.new_times_played;
        response.data["all_levels_done"] = end_result.all_levels_done;
        response.data["base_unlocked"] = false;
        response.data["rewards"] = nlohmann::json::object();

        bool base_unlocked_this_run = false;

        // Handle phase transitions based on all-levels-done
        response.data["land_patent_earned"] = false;
        response.data["baron_right_earned"] = false;

        if (won && end_result.all_levels_done) {
            if (state.game_phase == "initial_mission" && !state.base_unlocked) {
                // Transition to land_patent phase
                player_state_db::earn_land_patent(db, character_id, now);
                response.data["land_patent_earned"] = true;

                // Award completion bonus resources
                const auto& mini_games_config = config_cache.getMiniGames();
                if (mini_games_config.contains(mini_game)) {
                    const auto& mg_config = mini_games_config[mini_game];
                    if (mg_config.contains("completion_bonus") && mg_config["completion_bonus"].contains("resources")) {
                        response.data["completion_bonus"] = mg_config["completion_bonus"]["resources"];
                    }
                }
            } else if (state.game_phase == "baron_track") {
                // Transition to baron_right phase
                player_state_db::earn_baron_right(db, character_id, now);
                response.data["baron_right_earned"] = true;
            }
        }

        nlohmann::json level_rewards = nlohmann::json::object();
        if (won) {
            const auto& mini_games_config = config_cache.getMiniGames();

            if (mini_games_config.contains(mini_game)) {
                const auto& mg_config = mini_games_config[mini_game];

                // Check main levels first
                if (mg_config.contains("levels")) {
                    for (const auto& lvl : mg_config["levels"]) {
                        if (lvl["id"] == level_id && lvl.contains("reward")) {
                            level_rewards = lvl["reward"];
                            break;
                        }
                    }
                }

                // Check baron levels if not found in main levels
                if (level_rewards.empty() && mg_config.contains("baron_levels")) {
                    for (const auto& lvl : mg_config["baron_levels"]) {
                        if (lvl["id"] == level_id && lvl.contains("reward")) {
                            level_rewards = lvl["reward"];
                            break;
                        }
                    }
                }
            }

            // Ongoing (level 0) games pay silver from the shared reward pool
            if (level_rewards.empty() && level_id == 0) {
                auto params = fetch_ongoing_session_params(db, character_id, mini_game);
                if (params.has_value()) {
                    fill_ongoing_rewards(config_cache, db, level_rewards, response.data,
                                         character_id, mini_game,
                                         params->first, params->second, true, now);
                }
            }

            response.data["rewards"] = level_rewards;
        }

        player_state_db::clear_current_mini_game(db, character_id, now);

        // Determine game phase for response
        if (end_result.all_levels_done) {
            if (state.game_phase == "initial_mission") {
                response.data["game_phase"] = "land_patent";
            } else if (state.game_phase == "baron_track") {
                response.data["game_phase"] = "baron_right";
            } else if (state.game_phase == "land_patent") {
                response.data["game_phase"] = "land_patent";
            } else {
                response.data["game_phase"] = state.game_phase;
            }
        } else {
            response.data["game_phase"] = state.game_phase;
        }

        std::optional<int> next_level;
        if (!end_result.all_levels_done) {
            auto maybe_next = player_state_db::get_next_incomplete_level(db, character_id, mini_game, expected_total_levels);
            if (maybe_next) {
                next_level = *maybe_next;
            }
        }
        response.data["next_level_id"] = next_level ? nlohmann::json(*next_level) : nlohmann::json(nullptr);

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleEndMiniGame", e.what());
        response.error = std::string("Failed to end mini-game: ") + e.what();
    }

    return response;
}

ApiResponse handleGetMiniGameConfig(GameConfigCache& config_cache, const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (body.contains("mini_game") && body["mini_game"].is_string()) {
        std::string mini_game = body["mini_game"].get<std::string>();
        const auto& mini_games = config_cache.getMiniGames();

        if (mini_games.contains(mini_game)) {
            response.data[mini_game] = mini_games[mini_game];
            const json* ongoing_config = resolve_ongoing_config(config_cache, mini_game);
            if (ongoing_config) {
                response.data[mini_game]["ongoing"] = *ongoing_config;
            }
        } else {
            response.error = "Unknown mini_game: " + mini_game;
            return response;
        }
    } else {
        response.data = config_cache.getMiniGames();
        const json* td_ongoing = resolve_ongoing_config(config_cache, "tower_defense");
        const json* wd_ongoing = resolve_ongoing_config(config_cache, "weeding");
        if (td_ongoing && response.data.contains("tower_defense")) {
            response.data["tower_defense"]["ongoing"] = *td_ongoing;
        }
        if (wd_ongoing && response.data.contains("weeding")) {
            response.data["weeding"]["ongoing"] = *wd_ongoing;
        }
    }

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleEstimateOngoingRewards(GameConfigCache& config_cache, const json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }
    if (!body.contains("mini_game") || !body["mini_game"].is_string()) {
        response.error = "mini_game required";
        return response;
    }

    int character_id = body["character_id"].get<int>();
    std::string mini_game = body["mini_game"].get<std::string>();

    const json* ongoing_config = resolve_ongoing_config(config_cache, mini_game);
    if (!ongoing_config) {
        response.error = "No ongoing config for mini_game: " + mini_game;
        return response;
    }

    int difficulty = body.value("difficulty", ongoing_config->value("default_difficulty", 1));
    int size = body.value("size", ongoing_config->value("default_size", 0));

    int base = ongoing_rewards::compute_base_reward_pence(*ongoing_config, difficulty, size);

    const auto& economy = config_cache.getEconomyConfig();
    const json reward_pools_cfg = economy.value("reward_pools", json::object());
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto& db = Database::getInstance().gameDB();
    auto pools = ongoing_rewards::effective_pool_state(db, character_id, reward_pools_cfg, now);

    // Determine the multiplier that would apply without consuming the pool
    double mult = 1.0;
    if (pools.full_pool <= 0 && pools.half_pool <= 0) {
        mult = reward_pools_cfg.value("quarter_multiplier", 0.25);
    } else if (pools.full_pool <= 0) {
        mult = reward_pools_cfg.value("half_multiplier", 0.5);
    }

    int reward = static_cast<int>(base * mult);
    if (mult < 1.0) {
        int min_reward = reward_pools_cfg.value("min_reward_pence", 1);
        if (reward < min_reward) reward = min_reward;
    }

    const json currency_cfg = economy.value("currency", json::object());
    response.data["silver_pence"] = reward;
    response.data["silver_formatted"] = ongoing_rewards::format_silver_pence(reward, currency_cfg);
    response.data["base_silver_pence"] = base;
    response.data["reward_multiplier"] = mult;
    response.data["pool"] = {
        {"full", pools.full_pool},
        {"half", pools.half_pool}
    };

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

// Global TextManager instance, set during startup
TextManager* g_text_manager = nullptr;

ApiResponse handleGetUITextures(GameConfigCache& config_cache, const json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token)
{
    ApiResponse response;

    std::string component_id = body.value("component_id", "");
    if (component_id.empty()) {
        response.error = "component_id required";
        return response;
    }

    json textures = json::array();

    std::string texture_prefix = "text_silk_";
    int padding_vertical_px = 60;
    int padding_horizontal_px = 60;

    try {
        std::ifstream f(g_config_dir + "/ui_textures.json");
        if (f.is_open()) {
            json cfg = json::parse(f, nullptr, true, true, true);
            if (cfg.contains(component_id) && cfg[component_id].is_object()) {
                if (cfg[component_id].contains("texture_prefix")
                    && cfg[component_id]["texture_prefix"].is_string()) {
                    texture_prefix = cfg[component_id]["texture_prefix"].get<std::string>();
                }
                if (cfg[component_id].contains("padding_vertical_px")
                    && cfg[component_id]["padding_vertical_px"].is_number_integer()) {
                    padding_vertical_px = cfg[component_id]["padding_vertical_px"].get<int>();
                }
                if (cfg[component_id].contains("padding_horizontal_px")
                    && cfg[component_id]["padding_horizontal_px"].is_number_integer()) {
                    padding_horizontal_px = cfg[component_id]["padding_horizontal_px"].get<int>();
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[handleGetUITextures] Failed to load ui_textures.json: " << e.what() << std::endl;
    }

    try {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(g_images_dir + "/ui", ec)) {
            if (!entry.is_regular_file()) continue;
            std::string filename = entry.path().filename().string();
            if (filename.rfind(texture_prefix, 0) != 0) continue;

            auto dims = get_image_dimensions(entry.path().string());
            if (dims.width <= 0 || dims.height <= 0) continue;

            json tex;
            tex["url"] = "/images/ui/" + filename;
            tex["width"] = dims.width;
            tex["height"] = dims.height;
            textures.push_back(tex);
        }
    } catch (const std::exception& e) {
        std::cerr << "[handleGetUITextures] Failed to scan images/ui: " << e.what() << std::endl;
    }

    std::sort(textures.begin(), textures.end(), [](const json& a, const json& b) {
        return a["height"].get<int>() < b["height"].get<int>();
    });

    response.data["textures"] = textures;
    response.data["padding_vertical_px"] = padding_vertical_px;
    response.data["padding_horizontal_px"] = padding_horizontal_px;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleGetTexts(GameConfigCache& config_cache, const json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!g_text_manager) {
        response.error = "Text system not initialized";
        return response;
    }

    std::string language = body.value("language", "en");

    if (!body.contains("text_ids") || !body["text_ids"].is_array()) {
        response.error = "text_ids array required";
        return response;
    }

    json texts = json::object();

    for (const auto& id_val : body["text_ids"]) {
        if (!id_val.is_string()) continue;
        std::string text_id = id_val.get<std::string>();
        std::string raw = g_text_manager->get_text(language, text_id);
        // Public endpoint: no character context, so gender tokens resolve to the
        // male default form (TextManager::substitute_gender with no sex).
        texts[text_id] = TextManager::substitute_gender(raw, std::nullopt);
    }

    response.data["texts"] = texts;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleGetCharacterTexts(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!g_text_manager) {
        response.error = "Text system not initialized";
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    std::string language = body.value("language", "en");

    if (!body.contains("text_ids") || !body["text_ids"].is_array()) {
        response.error = "text_ids array required";
        return response;
    }

    int character_id = body["character_id"].get<int>();
    auto& db = Database::getInstance().gameDB();

    int owner_id = 0;
    db << "SELECT user_id FROM characters WHERE id = ?;"
       << character_id
       >> [&](int uid) { owner_id = uid; };

    if (owner_id == 0) {
        response.error = "Character not found";
        return response;
    }

    int user_id = 0;
    db << "SELECT id FROM users WHERE username = ?;"
       << *username
       >> [&](int id) { user_id = id; };

    if (owner_id != user_id) {
        response.error = "Character does not belong to this user";
        return response;
    }

    std::string display_name;
    std::unique_ptr<std::string> sex_ptr;
    db << "SELECT display_name, sex FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::string dn, std::unique_ptr<std::string> s) {
              display_name = dn;
              sex_ptr = std::move(s);
          };

    std::optional<std::string> sex;
    if (sex_ptr && !sex_ptr->empty()) {
        sex = *sex_ptr;
    }

    // Honor name chosen when the character started their own baron track —
    // substituted server-side so the client never sends it.
    std::optional<std::string> honor_name;
    db << "SELECT honor_name FROM player_game_state WHERE character_id = ?;"
       << character_id
       >> [&](std::unique_ptr<std::string> hn) {
              if (hn && !hn->empty()) {
                  honor_name = *hn;
              }
          };

    json texts = json::object();

    for (const auto& id_val : body["text_ids"]) {
        if (!id_val.is_string()) continue;
        std::string text_id = id_val.get<std::string>();
        std::string raw = g_text_manager->get_text(language, text_id);
        std::string substituted = TextManager::substitute_gender(raw, sex);
        if (!display_name.empty()) {
            // Character name is substituted server-side — the client never sends it.
            std::string token = "{character_name}";
            std::size_t pos = 0;
            while ((pos = substituted.find(token, pos)) != std::string::npos) {
                substituted.replace(pos, token.length(), display_name);
                pos += display_name.length();
            }
        }
        if (honor_name) {
            // The aspiring barony's honor name is substituted server-side as well.
            std::string token = "{honor_name}";
            std::size_t pos = 0;
            while ((pos = substituted.find(token, pos)) != std::string::npos) {
                substituted.replace(pos, token.length(), *honor_name);
                pos += honor_name->length();
            }
        }
        texts[text_id] = substituted;
    }

    response.data["texts"] = texts;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleVerifyAgeOverride(GameConfigCache& config_cache, const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token)
{
    ApiResponse response;

    std::string code = body.value("code", "");

    bool verified = (code == "a1b2c3d4");
    if (verified) {
        std::cout << "[INFO] Age override code accepted" << std::endl;
    } else {
        log_error("handleVerifyAgeOverride", "Invalid override code attempted: " + code);
    }

    response.data["verified"] = verified;

    return response;
}

ApiResponse handleSetCharacterArchetype(GameConfigCache& config_cache, const json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    std::string archetype = body.value("archetype", "");
    if (archetype != "wolf_warden" && archetype != "assarter") {
        response.error = "archetype must be 'wolf_warden' or 'assarter'";
        return response;
    }

    int character_id = body["character_id"].get<int>();

    auto& db = Database::getInstance().gameDB();

    // Verify the character belongs to the authenticated user
    int owner_id = 0;
    db << "SELECT user_id FROM characters WHERE id = ?;"
       << character_id
       >> [&](int uid) { owner_id = uid; };

    if (owner_id == 0) {
        response.error = "Character not found";
        return response;
    }

    int user_id = 0;
    db << "SELECT id FROM users WHERE username = ?;"
       << *username
       >> [&](int id) { user_id = id; };

    if (owner_id != user_id) {
        response.error = "Character does not belong to this user";
        return response;
    }

    db << "UPDATE characters SET archetype = ? WHERE id = ?;"
       << archetype << character_id;

    // Return updated character data
    std::string display_name, safe_display_name;
    int level;
    std::unique_ptr<std::string> sex;
    db << "SELECT display_name, safe_display_name, level, sex FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::string dn, std::string sdn, int l, std::unique_ptr<std::string> s) {
              display_name = dn;
              safe_display_name = sdn;
              level = l;
              sex = std::move(s);
          };

    response.data["id"] = character_id;
    response.data["display_name"] = display_name;
    response.data["safe_display_name"] = safe_display_name;
    response.data["level"] = level;
    response.data["archetype"] = archetype;
    if (sex) {
        response.data["sex"] = *sex;
    } else {
        response.data["sex"] = nullptr;
    }

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleSetFiefdomImport(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!body.contains("fiefdom_id") || !body.contains("resource") || !body.contains("auto_import")) {
        response.error = "fiefdom_id, resource, and auto_import required";
        return response;
    }

    int fiefdom_id = body["fiefdom_id"].get<int>();
    std::string resource = body["resource"].get<std::string>();
    bool auto_import = body["auto_import"].get<bool>();

    try {
        auto& db = Database::getInstance().gameDB();

        // Read current import_settings
        std::string import_str;
        db << "SELECT import_settings FROM fiefdoms WHERE id = ?;" << fiefdom_id
           >> [&](std::string s) { import_str = s; };

        if (import_str.empty()) {
            import_str = "{\"grain\":true,\"wood\":true,\"steel\":true,"
                         "\"bronze\":true,\"stone\":true,\"leather\":true,\"mana\":true,"
                         "\"charcoal\":true,\"iron\":true,\"ironwork\":true}";
        }

        auto settings = json::parse(import_str);
        settings[resource] = auto_import;

        db << "UPDATE fiefdoms SET import_settings = ? WHERE id = ?;"
           << settings.dump() << fiefdom_id;

        response.data["import_settings"] = settings;
    } catch (const std::exception& e) {
        response.error = std::string("Failed to update import settings: ") + e.what();
    }

    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleSetFiefdomReserve(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!body.contains("fiefdom_id") || !body.contains("resource") || !body.contains("reserve")) {
        response.error = "fiefdom_id, resource, and reserve required";
        return response;
    }

    int fiefdom_id = body["fiefdom_id"].get<int>();
    std::string resource = body["resource"].get<std::string>();
    double reserve = body["reserve"].get<double>();
    if (reserve < 0) reserve = 0;

    try {
        auto& db = Database::getInstance().gameDB();

        std::string reserves_str;
        db << "SELECT reserves FROM fiefdoms WHERE id = ?;" << fiefdom_id
           >> [&](std::string s) { reserves_str = s; };

        auto settings = json::object();
        if (!reserves_str.empty()) {
            try { settings = json::parse(reserves_str); }
            catch (...) { settings = json::object(); }
        }
        if (!settings.is_object()) settings = json::object();

        settings[resource] = reserve;

        db << "UPDATE fiefdoms SET reserves = ? WHERE id = ?;"
           << settings.dump() << fiefdom_id;

        response.data["reserves"] = settings;
    } catch (const std::exception& e) {
        response.error = std::string("Failed to update reserves: ") + e.what();
    }

    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleSetBuildingOutputRate(GameConfigCache& config_cache, const json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("building_id") || !body["building_id"].is_number_integer()) {
        response.error = "building_id required";
        return response;
    }
    if (!body.contains("output") || !body["output"].is_string()) {
        response.error = "output required";
        return response;
    }
    if (!body.contains("rate") || !body["rate"].is_number()) {
        response.error = "rate required";
        return response;
    }

    int building_id = body["building_id"].get<int>();
    std::string output = body["output"].get<std::string>();
    double rate = body["rate"].get<double>();
    if (rate < 0.0) rate = 0.0;
    if (rate > 1.0) rate = 1.0;

    try {
        auto& db = Database::getInstance().gameDB();

        // Load the building + its fiefdom and verify ownership (user owns the
        // character that owns the fiefdom).
        int fiefdom_id = 0;
        std::string building_name;
        int building_level = 0;
        db << "SELECT fiefdom_id, name, level FROM fiefdom_buildings WHERE id = ?;" << building_id
           >> [&](int fid, std::string bname, int blevel) {
                fiefdom_id = fid;
                building_name = bname;
                building_level = blevel;
            };
        if (fiefdom_id == 0) {
            response.error = "Building not found";
            return response;
        }

        int owner_character = 0;
        db << "SELECT owner_id FROM fiefdoms WHERE id = ?;" << fiefdom_id
           >> [&](int oid) { owner_character = oid; };
        int owner_user = 0;
        db << "SELECT user_id FROM characters WHERE id = ?;" << owner_character
           >> [&](int uid) { owner_user = uid; };
        int user_id = 0;
        db << "SELECT id FROM users WHERE username = ?;" << *username
           >> [&](int id) { user_id = id; };
        if (owner_user != user_id) {
            response.error = "Character does not belong to this user";
            return response;
        }

        // Validate the output against the building type config and level-gating.
        auto building_types = config_cache.getFiefdomBuildingTypes();
        json bld_cfg;
        for (const auto& type_obj : building_types) {
            if (type_obj.contains(building_name)) { bld_cfg = type_obj[building_name]; break; }
        }
        if (bld_cfg.is_null()) {
            response.error = "Unknown building type";
            return response;
        }

        bool output_found = false;
        int min_level = 1;
        // The `outputs` array is the canonical production schema (config lint
        // enforces it); every production building defines one.
        if (bld_cfg.contains("outputs") && bld_cfg["outputs"].is_array()) {
            for (const auto& out : bld_cfg["outputs"]) {
                if (out.is_object() && out.value("resource", "") == output) {
                    output_found = true;
                    min_level = out.value("min_level", 1);
                    break;
                }
            }
        }
        if (!output_found) {
            response.error = "That building does not produce " + output;
            return response;
        }
        if (building_level < min_level) {
            response.error = "That output is not unlocked until level " + std::to_string(min_level);
            return response;
        }

        // Persist the rate into the building's output_rates JSON.
        std::string rates_str;
        db << "SELECT output_rates FROM fiefdom_buildings WHERE id = ?;" << building_id
           >> [&](std::string s) { rates_str = s; };
        auto rates = json::object();
        if (!rates_str.empty()) {
            try { rates = json::parse(rates_str); }
            catch (...) { rates = json::object(); }
        }
        if (!rates.is_object()) rates = json::object();
        rates[output] = rate;

        db << "UPDATE fiefdom_buildings SET output_rates = ? WHERE id = ?;"
           << rates.dump() << building_id;

        response.data["output_rates"] = rates;
    } catch (const std::exception& e) {
        log_error("handleSetBuildingOutputRate", e.what());
        response.error = std::string("Failed to set building output rate: ") + e.what();
    }

    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleSetCharacterSex(GameConfigCache& config_cache, const json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    std::string sex = body.value("sex", "");
    if (sex != "male" && sex != "female") {
        response.error = "sex must be 'male' or 'female'";
        return response;
    }

    int character_id = body["character_id"].get<int>();
    auto& db = Database::getInstance().gameDB();

    int owner_id = 0;
    db << "SELECT user_id FROM characters WHERE id = ?;"
       << character_id
       >> [&](int uid) { owner_id = uid; };

    if (owner_id == 0) {
        response.error = "Character not found";
        return response;
    }

    int user_id = 0;
    db << "SELECT id FROM users WHERE username = ?;"
       << *username
       >> [&](int id) { user_id = id; };

    if (owner_id != user_id) {
        response.error = "Character does not belong to this user";
        return response;
    }

    db << "UPDATE characters SET sex = ? WHERE id = ?;"
       << sex << character_id;

    std::string display_name, safe_display_name;
    int level;
    std::unique_ptr<std::string> archetype;
    db << "SELECT display_name, safe_display_name, level, archetype FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::string dn, std::string sdn, int l, std::unique_ptr<std::string> a) {
             display_name = dn;
             safe_display_name = sdn;
             level = l;
             archetype = std::move(a);
         };

    response.data["id"] = character_id;
    response.data["display_name"] = display_name;
    response.data["safe_display_name"] = safe_display_name;
    response.data["level"] = level;
    if (archetype) {
        response.data["archetype"] = *archetype;
    } else {
        response.data["archetype"] = nullptr;
    }
    response.data["sex"] = sex;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

// ── PFP (player representative image) ─────────────────────────────────────
//
// Variety/identity only (no gameplay effect). Custom uploads are 18+ only,
// stored OUTSIDE the static image tree, and served solely by the age-gated
// /api/avatar/<character_id> route. Any avatar change is rate-limited per
// character; the first (creation) pick is exempt because avatar_changed_at
// starts at 0. Moderation of custom uploads is a documented TODO.

namespace {

constexpr int64_t kAvatarCooldownSeconds = 24 * 60 * 60;
constexpr size_t kAvatarMaxUploadBytes = 1024 * 1024;   // 1 MB

// Base64 decoder for upload payloads (standard alphabet; whitespace and data
// URI prefixes are tolerated by the caller).
std::string base64_decode(const std::string& input) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int lookup[256];
    for (int i = 0; i < 256; ++i) lookup[i] = -1;
    for (int i = 0; i < 64; ++i) lookup[static_cast<unsigned char>(alphabet[i])] = i;

    std::string out;
    unsigned int value = 0;
    int bits = -8;
    for (char c : input) {
        if (c == '=') break;
        const int decoded = lookup[static_cast<unsigned char>(c)];
        if (decoded < 0) continue;  // skip whitespace/newlines
        value = (value << 6) + static_cast<unsigned int>(decoded);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

bool bytes_match_format(const std::string& bytes, const std::string& format) {
    if (format == "png") {
        return bytes.size() >= 8 && static_cast<unsigned char>(bytes[0]) == 0x89 &&
               bytes.compare(0, 4, "\x89PNG") == 0;
    }
    if (format == "jpg") {
        return bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
               static_cast<unsigned char>(bytes[1]) == 0xD8;
    }
    if (format == "webp") {
        return bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 &&
               bytes.compare(8, 4, "WEBP") == 0;
    }
    if (format == "avif") {
        return bytes.size() >= 12 && bytes.compare(4, 4, "ftyp") == 0;
    }
    return false;
}

// True while the character is inside the avatar-change cooldown. Fills
// `remaining_seconds` when blocked.
bool avatar_cooldown_active(sqlite::database& db, int64_t character_id, int64_t now,
                            int64_t& remaining_seconds) {
    int64_t changed_at = 0;
    db << "SELECT avatar_changed_at FROM characters WHERE id = ?;"
       << character_id >> [&](int64_t t) { changed_at = t; };
    if (changed_at <= 0) return false;
    const int64_t elapsed = now - changed_at;
    if (elapsed < kAvatarCooldownSeconds) {
        remaining_seconds = kAvatarCooldownSeconds - elapsed;
        return true;
    }
    return false;
}

std::string avatar_cooldown_message(int64_t remaining_seconds) {
    const int64_t hours = (remaining_seconds + 3599) / 3600;
    return "You may only change your portrait once every 24 hours (" +
           std::to_string(hours) + "h remaining).";
}

} // namespace

// Lists the safe avatar pool (id + served URL) and the viewer's adult flag, so
// the client can render the creation picker. The pool is shuffled per user
// (seeded from the username) so the same portraits are not always first.
ApiResponse handleGetAvatarPool(GameConfigCache& config_cache, const json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token)
{
    (void)config_cache; (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    // The client requests the character's own gender pool; default to male when
    // absent/invalid (the safe art is public either way). The write path still
    // validates against the character's stored sex, never the request's gender.
    std::string gender = lower_ascii(body.value("gender", "male"));
    if (gender != "female") gender = "male";
    json safe = json::array();
    for (const auto& id : AvatarPool::getInstance().safe_avatars(gender)) {
        safe.push_back({{"id", id}, {"url", AvatarPool::safe_avatar_url(id)}});
    }
    // Randomize the display order so the same avatars are not always first, but
    // seed from the user so a given player sees a stable order across reloads.
    // The canonical pool (AvatarPool::safe_avatars) stays sorted for validation.
    std::mt19937 shuffle_rng(stable_string_seed(*username));
    std::shuffle(safe.begin(), safe.end(), shuffle_rng);
    response.data["safe"] = safe;
    response.data["adult"] = user_is_adult(username);
    if (new_token) response.data["token"] = *new_token;
    return response;
}

// Returns the decorative portrait-frame geometry (game/config/portrait_frames.json):
// for each variant, the frame image plus the rectangle (in source-image pixels)
// the portrait image is mapped into. The client overlays the frame on top, so
// the rectangle is the portrait's draw box, not a mask.
ApiResponse handleGetPortraitFrames(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token)
{
    (void)body; (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    response.data = config_cache.getPortraitFrames();
    if (new_token) response.data["token"] = *new_token;
    return response;
}

// Sets the character's safe-pool avatar. Rate-limited (24 h) except for the
// very first pick, which is the creation flow.
ApiResponse handleSetCharacterAvatar(GameConfigCache& config_cache, const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token)
{
    (void)config_cache; (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!character_owned_by(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const std::string safe_id = body.value("safe_id", "");
    if (safe_id.empty()) {
        response.error = "safe_id required";
        return response;
    }

    auto& db = Database::getInstance().gameDB();
    // The safe pool is gender-split; validate against the character's own pool.
    // The character's stored sex is authoritative — never the request's gender.
    std::string gender = "male";
    db << "SELECT sex FROM characters WHERE id = ?;" << character_id
       >> [&](std::unique_ptr<std::string> sex) {
              if (sex && *sex == "female") gender = "female";
          };
    if (!AvatarPool::getInstance().has_safe_avatar(gender, safe_id)) {
        response.error = "Unknown avatar.";
        return response;
    }

    // Admin identity lock: a locked avatar may only be changed by an admin.
    bool avatar_locked = false;
    db << "SELECT avatar_locked FROM characters WHERE id = ?;" << character_id
       >> [&](int v) { avatar_locked = v; };
    if (avatar_locked) {
        response.error = "avatar_locked";
        return response;
    }
    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    int64_t remaining = 0;
    if (avatar_cooldown_active(db, character_id, now, remaining)) {
        response.error = avatar_cooldown_message(remaining);
        return response;
    }

    // Record the prior safe avatar so an admin can force a revert later.
    std::string prior_avatar;
    db << "SELECT COALESCE(avatar_safe_id, '') FROM characters WHERE id = ?;" << character_id
       >> prior_avatar;
    db << "INSERT INTO character_identity_history (character_id, kind, value, changed_at) "
          "VALUES (?, 'avatar', ?, ?);"
       << character_id << prior_avatar << now;

    db << "UPDATE characters SET avatar_safe_id = ?, avatar_changed_at = ? WHERE id = ?;"
       << safe_id << now << character_id;
    response.data["avatar_safe_id"] = safe_id;
    response.data["avatar_changed_at"] = now;
    response.data["avatar_url"] = resolve_avatar_url(character_id, user_is_adult(username));
    if (new_token) response.data["token"] = *new_token;
    return response;
}

// Uploads a custom PFP (18+ only). The client is expected to pre-scale to
// 256x256; the server enforces the format, a 1 MB size cap, an identity magic-
// byte check, and (for PNG/JPEG) a 256x256 dimension cap. TODO: moderation —
// there is no content review yet, so this is intentionally adult-gated only.
ApiResponse handleUploadCharacterAvatar(GameConfigCache& config_cache, const json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token)
{
    (void)config_cache; (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    if (!user_is_adult(username)) {
        response.error = "adult_required";
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!character_owned_by(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }

    std::string format = lower_ascii(body.value("format", ""));
    if (format == "jpeg") format = "jpg";
    if (!AvatarPool::allowed_extension("." + format)) {
        response.error = "Unsupported image format (png, jpg, webp, avif).";
        return response;
    }

    std::string encoded = body.value("image_base64", "");
    if (encoded.empty()) {
        response.error = "image_base64 required";
        return response;
    }
    const size_t comma = encoded.find(',');
    if (encoded.rfind("data:", 0) == 0 && comma != std::string::npos) {
        encoded = encoded.substr(comma + 1);
    }
    const std::string bytes = base64_decode(encoded);
    if (bytes.empty()) {
        response.error = "Image data is missing or invalid.";
        return response;
    }
    if (bytes.size() > kAvatarMaxUploadBytes) {
        response.error = "Image is too large (max 1 MB).";
        return response;
    }
    if (!bytes_match_format(bytes, format)) {
        response.error = "Image does not look like a valid " + format + " file.";
        return response;
    }

    auto& db = Database::getInstance().gameDB();
    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    int64_t remaining = 0;
    if (avatar_cooldown_active(db, character_id, now, remaining)) {
        response.error = avatar_cooldown_message(remaining);
        return response;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories("uploads/avatars", ec);
    const std::string filename = std::to_string(character_id) + "." + format;
    const std::string path = "uploads/avatars/" + filename;
    {
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            response.error = "Failed to store the image.";
            return response;
        }
    }

    // PNG/JPEG expose dimensions cheaply; reject oversized images when known.
    const ImageDimensions dim = get_image_dimensions(path);
    if (dim.width > 256 || dim.height > 256) {
        fs::remove(path, ec);
        response.error = "Image is too large (max 256x256).";
        return response;
    }

    // Admin identity lock: a locked avatar may only be changed by an admin.
    bool avatar_locked = false;
    db << "SELECT avatar_locked FROM characters WHERE id = ?;" << character_id
       >> [&](int v) { avatar_locked = v; };
    if (avatar_locked) {
        fs::remove(path, ec);
        response.error = "avatar_locked";
        return response;
    }

    // Drop a previous custom file under a different extension.
    std::unique_ptr<std::string> old_custom;
    db << "SELECT avatar_custom_id FROM characters WHERE id = ?;" << character_id
       >> [&](std::unique_ptr<std::string> c) { old_custom = std::move(c); };
    if (old_custom && !old_custom->empty() && *old_custom != filename) {
        fs::remove("uploads/avatars/" + *old_custom, ec);
    }

    db << "UPDATE characters SET avatar_custom_id = ?, avatar_changed_at = ? WHERE id = ?;"
       << filename << now << character_id;
    response.data["avatar_custom_id"] = filename;
    response.data["avatar_changed_at"] = now;
    response.data["avatar_url"] = resolve_avatar_url(character_id, true);
    if (new_token) response.data["token"] = *new_token;
    return response;
}

// Removes the character's custom PFP (18+ only), reverting the display to the
// safe pick. Rate-limited like every other avatar change.
ApiResponse handleRemoveCustomAvatar(GameConfigCache& config_cache, const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token)
{
    (void)config_cache; (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    if (!user_is_adult(username)) {
        response.error = "adult_required";
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!character_owned_by(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }

    auto& db = Database::getInstance().gameDB();
    bool avatar_locked = false;
    db << "SELECT avatar_locked FROM characters WHERE id = ?;" << character_id
       >> [&](int v) { avatar_locked = v; };
    if (avatar_locked) {
        response.error = "avatar_locked";
        return response;
    }
    std::unique_ptr<std::string> custom_id;
    db << "SELECT avatar_custom_id FROM characters WHERE id = ?;" << character_id
       >> [&](std::unique_ptr<std::string> c) { custom_id = std::move(c); };
    if (!custom_id || custom_id->empty()) {
        response.data["result"] = "no_custom";
        response.data["avatar_url"] = resolve_avatar_url(character_id, true);
        if (new_token) response.data["token"] = *new_token;
        return response;
    }

    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    int64_t remaining = 0;
    if (avatar_cooldown_active(db, character_id, now, remaining)) {
        response.error = avatar_cooldown_message(remaining);
        return response;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove("uploads/avatars/" + *custom_id, ec);
    db << "UPDATE characters SET avatar_custom_id = NULL, avatar_changed_at = ? WHERE id = ?;"
       << now << character_id;
    response.data["result"] = "removed";
    response.data["avatar_changed_at"] = now;
    response.data["avatar_url"] = resolve_avatar_url(character_id, true);
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleAcknowledgeLandPatent(GameConfigCache& config_cache, const json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    int character_id = body["character_id"].get<int>();

    try {
        auto& db = Database::getInstance().gameDB();
        player_state_db::acknowledge_land_patent(db, character_id);
    } catch (const std::exception& e) {
        log_error("handleAcknowledgeLandPatent", e.what());
        response.error = std::string("Failed to acknowledge land patent: ") + e.what();
    }

    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleGetBaronies(GameConfigCache& config_cache, const json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    try {
        auto& db = Database::getInstance().gameDB();
        json baronies_list = json::array();

        db << "SELECT d.id, d.name, d.description, d.owner_character_id, d.created_at, "
              "COUNT(dm.id) as member_count, c.display_name as owner_name, "
              "d.baron_character_id, bc.display_name as baron_name "
              "FROM baronies d "
              "LEFT JOIN barony_members dm ON dm.barony_id = d.id "
              "LEFT JOIN characters c ON c.id = d.owner_character_id "
              "LEFT JOIN characters bc ON bc.id = d.baron_character_id "
              "WHERE d.defunct_at = 0 "
              "GROUP BY d.id "
              "ORDER BY d.name;"
           >> [&](int id, std::string name, std::string description, int owner_id, int64_t created_at, int member_count, std::string owner_name, std::optional<int> baron_id, std::optional<std::string> baron_name) {
                json entry;
                entry["id"] = id;
                entry["name"] = name;
                entry["description"] = description;
                entry["owner_character_id"] = owner_id;
                entry["owner_name"] = owner_name;
                entry["member_count"] = member_count;
                entry["created_at"] = created_at;
                if (baron_id) {
                    entry["baron_character_id"] = *baron_id;
                }
                if (baron_name) {
                    entry["baron_name"] = *baron_name;
                }
                baronies_list.push_back(entry);
            };

        response.data["baronies"] = baronies_list;

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleGetBaronies", e.what());
        response.error = std::string("Failed to fetch baronies: ") + e.what();
    }

    return response;
}

/**
 * Lists a barony's members (for the founder's successor choice when leaving).
 * Only a member of that barony may call it.
 */
ApiResponse handleGetBaronyMembers(GameConfigCache& config_cache, const json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    if (!body.contains("barony_id") || !body["barony_id"].is_number_integer()) {
        response.error = "barony_id required";
        return response;
    }
    int barony_id = body["barony_id"].get<int>();

    try {
        auto& db = Database::getInstance().gameDB();
        int user_id = 0;
        db << "SELECT id FROM users WHERE username = ?;" << *username >> [&](int id) { user_id = id; };
        int is_member = 0;
        db << "SELECT COUNT(*) FROM barony_members bm JOIN characters c ON c.id = bm.character_id "
              "WHERE bm.barony_id = ? AND c.user_id = ?;"
           << barony_id << user_id >> [&](int c) { is_member = c; };
        if (!is_member) {
            response.error = "Not a member of this barony";
            return response;
        }

        json members_arr = json::array();
        db << "SELECT bm.character_id, c.display_name, bm.role FROM barony_members bm "
              "JOIN characters c ON c.id = bm.character_id WHERE bm.barony_id = ? "
              "ORDER BY bm.joined_at ASC;"
           << barony_id
           >> [&](int cid, std::string display_name, std::string role) {
                  members_arr.push_back({
                      {"character_id", cid},
                      {"display_name", display_name},
                      {"role", role}
                  });
              };
        response.data["members"] = members_arr;
        if (new_token) response.data["token"] = *new_token;
    } catch (const std::exception& e) {
        log_error("handleGetBaronyMembers", e.what());
        response.error = std::string("Failed to fetch barony members: ") + e.what();
    }
    return response;
}

/**
 * Checks if a barony has 21+ members with manor_level >= 3.
 * If so, promotes the mesne_lord to baron.
 */
void checkBaronPromotion(int barony_id) {
    try {
        auto& db = Database::getInstance().gameDB();

        // Count members with manor_level >= 3
        int qualified_members = 0;
        db << "SELECT COUNT(*) FROM barony_members dm "
              "JOIN fiefdoms f ON dm.fiefdom_id = f.id "
              "WHERE dm.barony_id = ? AND f.manor_level >= 3;"
           << barony_id
           >> [&](int count) { qualified_members = count; };

        if (qualified_members >= 21) {
            // Promote mesne_lord to baron
            db << "UPDATE barony_members SET role = 'baron' "
                  "WHERE barony_id = ? AND role = 'mesne_lord' "
                  "AND (SELECT COUNT(*) FROM barony_members WHERE barony_id = ? AND role = 'baron') = 0;"
               << barony_id << barony_id;

            // Track the current baron on the baronies row so leadership is
            // queryable and can be reassigned if a baron is replaced.
            db << "UPDATE baronies SET baron_character_id = "
                  "(SELECT character_id FROM barony_members "
                  " WHERE barony_id = ? AND role = 'baron' LIMIT 1) "
                  "WHERE id = ?;"
               << barony_id << barony_id;
        }
        // Keep the barony chat's owner/membership in step with the game state.
        chat::ChatService::instance().sync_barony_chat(barony_id);
    } catch (const std::exception& e) {
        log_error("checkBaronPromotion", e.what());
    }
}

ApiResponse handleJoinBarony(GameConfigCache& config_cache, const json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    if (!body.contains("barony_id") || !body["barony_id"].is_number_integer()) {
        response.error = "barony_id required";
        return response;
    }

    int character_id = body["character_id"].get<int>();
    int barony_id = body["barony_id"].get<int>();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    try {
        auto& db = Database::getInstance().gameDB();

        // Verify character ownership
        int owner_id = 0;
        int user_id = 0;
        db << "SELECT id FROM users WHERE username = ?;" << *username >> [&](int id) { user_id = id; };
        db << "SELECT user_id FROM characters WHERE id = ?;" << character_id >> [&](int uid) { owner_id = uid; };

        if (owner_id != user_id) {
            response.error = "Character does not belong to this user";
            return response;
        }

        // Verify barony exists and is not defunct
        int barony_exists = 0;
        db << "SELECT COUNT(*) FROM baronies WHERE id = ? AND defunct_at = 0;" << barony_id >> [&](int count) { barony_exists = count; };
        if (!barony_exists) {
            response.error = "Barony not found";
            return response;
        }

        // Check if already a member of any barony
        int existing_member = 0;
        db << "SELECT COUNT(*) FROM barony_members WHERE character_id = ?;"
           << character_id >> [&](int count) { existing_member = count; };
        if (existing_member > 0) {
            response.error = "Already a member of a barony";
            return response;
        }

        // A land-patent holder or a freeholder may join a barony.
        std::string current_phase;
        db << "SELECT game_phase FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](std::string phase) { current_phase = phase; };
        if (current_phase != "land_patent" && current_phase != "freehold" &&
            current_phase != "freehold_qualified") {
            response.error = "Must have a land patent to join a barony";
            return response;
        }

        // Join cooldown after leaving a barony.
        int64_t barony_left_at = 0;
        db << "SELECT barony_left_at FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](int64_t v) { barony_left_at = v; };
        const int cooldown_seconds = progression::join_cooldown_seconds(config_cache.getProgression());
        if (cooldown_seconds > 0 && barony_left_at > 0 &&
            now < barony_left_at + cooldown_seconds) {
            response.error = "You must wait before joining another barony";
            response.data["cooldown_until"] = barony_left_at + cooldown_seconds;
            return response;
        }

        // Ensure a fiefdom exists (a freeholder already has one; it persists).
        int fiefdom_id = ensure_fiefdom_for_character(db, config_cache, character_id, *username, now);

        // Add to barony members
        db << "INSERT INTO barony_members (barony_id, character_id, fiefdom_id, joined_at, role) "
              "VALUES (?, ?, ?, ?, 'member');"
           << barony_id << character_id << fiefdom_id << now;

        // Unlock base (sandbox phase)
        player_state_db::unlock_base(db, character_id, now);

        // Check for baron promotion: 21+ members with manor_level >= 3
        checkBaronPromotion(barony_id);

        response.data["barony_id"] = barony_id;
        response.data["fiefdom_id"] = fiefdom_id;
        response.data["game_phase"] = "sandbox";
        response.data["base_unlocked"] = true;

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleJoinBarony", e.what());
        response.error = std::string("Failed to join barony: ") + e.what();
    }

    return response;
}

ApiResponse handleCreateBarony(GameConfigCache& config_cache, const json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    std::string name = body.value("name", "");
    if (name.empty()) {
        response.error = "name required";
        return response;
    }

    std::string description = body.value("description", "");
    int character_id = body["character_id"].get<int>();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    try {
        auto& db = Database::getInstance().gameDB();

        // Verify character ownership
        int owner_id = 0;
        int user_id = 0;
        db << "SELECT id FROM users WHERE username = ?;" << *username >> [&](int id) { user_id = id; };
        db << "SELECT user_id FROM characters WHERE id = ?;" << character_id >> [&](int uid) { owner_id = uid; };
        if (owner_id != user_id) {
            response.error = "Character does not belong to this user";
            return response;
        }

        // Check for duplicate barony name (case-insensitive, matching the
        // capture-time uniqueness rule in handleStartBaronTrack)
        int name_taken = 0;
        db << "SELECT COUNT(*) FROM baronies WHERE LOWER(name) = LOWER(?);" << name >> [&](int count) { name_taken = count; };
        if (name_taken > 0) {
            response.error = "A barony with that name already exists";
            return response;
        }

        // Verify character is in baron_right phase (all 25 levels complete)
        std::string current_phase;
        db << "SELECT game_phase FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](std::string phase) { current_phase = phase; };
        if (current_phase != "baron_right") {
            response.error = "Must complete the baron track to start a barony";
            return response;
        }

        // Check not already a member
        int existing_member = 0;
        db << "SELECT COUNT(*) FROM barony_members WHERE character_id = ?;"
           << character_id >> [&](int count) { existing_member = count; };
        if (existing_member > 0) {
            response.error = "Already a member of a barony";
            return response;
        }

        // Ensure a fiefdom exists (a freehold-entered player already has one).
        int fiefdom_id = ensure_fiefdom_for_character(db, config_cache, character_id, *username, now);

        // Create barony
        db << "INSERT INTO baronies (name, owner_character_id, description, created_at) "
              "VALUES (?, ?, ?, ?);"
           << name << character_id << description << now;

        int barony_id = db.last_insert_rowid();

        // Add founder as member with mesne_lord role
        db << "INSERT INTO barony_members (barony_id, character_id, fiefdom_id, joined_at, role) "
              "VALUES (?, ?, ?, ?, 'mesne_lord');"
           << barony_id << character_id << fiefdom_id << now;

        // Materialize the barony chat now that the barony exists.
        chat::ChatService::instance().sync_barony_chat(barony_id);

        // Unlock base
        player_state_db::unlock_base(db, character_id, now);

        // Keep the captured honor name in sync if the player edited it at creation
        db << "UPDATE player_game_state SET honor_name = ? WHERE character_id = ?;"
           << name << character_id;

        response.data["barony_id"] = barony_id;
        response.data["fiefdom_id"] = fiefdom_id;
        response.data["game_phase"] = "sandbox";
        response.data["base_unlocked"] = true;

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleCreateBarony", e.what());
        response.error = std::string("Failed to create barony: ") + e.what();
    }

    return response;
}

ApiResponse handleStartBaronTrack(GameConfigCache& config_cache, const json& body,
                                  const std::optional<std::string>& username,
                                  const ClientInfo& client,
                                  const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }

    int character_id = body["character_id"].get<int>();

    // The aspiring barony's honor name is captured up front: required, trimmed,
    // length-capped, and must not collide (case-insensitively) with an existing barony.
    std::string honor_name = body.value("honor_name", "");
    honor_name.erase(0, honor_name.find_first_not_of(" \t\r\n"));
    honor_name.erase(honor_name.find_last_not_of(" \t\r\n") + 1);
    if (honor_name.empty()) {
        response.error = "honor_name required";
        return response;
    }
    if (honor_name.length() > 64) {
        response.error = "honor_name must be 64 characters or fewer";
        return response;
    }

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    try {
        auto& db = Database::getInstance().gameDB();

        // Verify character ownership
        int owner_id = 0;
        int user_id = 0;
        db << "SELECT id FROM users WHERE username = ?;" << *username >> [&](int id) { user_id = id; };
        db << "SELECT user_id FROM characters WHERE id = ?;" << character_id >> [&](int uid) { owner_id = uid; };
        if (owner_id != user_id) {
            response.error = "Character does not belong to this user";
            return response;
        }

        // Verify character is in land_patent / freehold phase
        std::string current_phase;
        db << "SELECT game_phase FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](std::string phase) { current_phase = phase; };
        if (current_phase != "land_patent" && current_phase != "freehold" &&
            current_phase != "freehold_qualified") {
            response.error = "Must have a land patent to start the baron track";
            return response;
        }

        // Reject an honor name that already belongs to a barony — the aspiring
        // name must be unique from the moment it is chosen (not just a warning).
        int name_taken = 0;
        db << "SELECT COUNT(*) FROM baronies WHERE LOWER(name) = LOWER(?);"
           << honor_name >> [&](int count) { name_taken = count; };
        if (name_taken > 0) {
            response.error = "A barony with that name already exists";
            return response;
        }

        // Switch to baron_track phase and persist the chosen honor name
        player_state_db::start_baron_track(db, character_id, now, honor_name);

        response.data["game_phase"] = "baron_track";
        response.data["honor_name"] = honor_name;

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleStartBaronTrack", e.what());
        response.error = std::string("Failed to start baron track: ") + e.what();
    }

    return response;
}

/**
 * Found an Outlying Freehold: a lone knight's manor unattached to any barony.
 * Only a land-patent holder may do this. The fiefdom persists across later
 * tenure changes (joining/founding a barony reuses it).
 */
ApiResponse handleFoundFreehold(GameConfigCache& config_cache, const json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }
    int character_id = body["character_id"].get<int>();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    try {
        auto& db = Database::getInstance().gameDB();
        std::string ownership_error;
        if (!character_owned_by(db, username, character_id, ownership_error)) {
            response.error = ownership_error;
            return response;
        }

        std::string current_phase;
        db << "SELECT game_phase FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](std::string phase) { current_phase = phase; };
        if (current_phase != "land_patent") {
            response.error = "Must have a land patent to found a freehold";
            return response;
        }

        int existing_member = 0;
        db << "SELECT COUNT(*) FROM barony_members WHERE character_id = ?;"
           << character_id >> [&](int count) { existing_member = count; };
        if (existing_member > 0) {
            response.error = "Already a member of a barony";
            return response;
        }

        int fiefdom_id = ensure_fiefdom_for_character(db, config_cache, character_id, *username, now);
        player_state_db::enter_freehold(db, character_id, now);

        response.data["fiefdom_id"] = fiefdom_id;
        response.data["game_phase"] = "freehold";
        response.data["base_unlocked"] = true;
        if (new_token) response.data["token"] = *new_token;
    } catch (const std::exception& e) {
        log_error("handleFoundFreehold", e.what());
        response.error = std::string("Failed to found freehold: ") + e.what();
    }
    return response;
}

/**
 * Leave a barony. A non-founder leaves freely; a founder must name another
 * member as the new baron, unless they are the last member — in which case the
 * barony is tombstoned (defunct_at set, name reserved, hidden from the list).
 * The leaver keeps their fiefdom and becomes a freeholder. A join cooldown is
 * stamped (progression.json "barony.join_cooldown_days").
 */
ApiResponse handleLeaveBarony(GameConfigCache& config_cache, const json& body,
                              const std::optional<std::string>& username,
                              const ClientInfo& client,
                              const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }
    int character_id = body["character_id"].get<int>();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    try {
        auto& db = Database::getInstance().gameDB();
        std::string ownership_error;
        if (!character_owned_by(db, username, character_id, ownership_error)) {
            response.error = ownership_error;
            return response;
        }

        int barony_id = 0;
        bool is_member = false;
        db << "SELECT barony_id FROM barony_members WHERE character_id = ?;"
           << character_id >> [&](int b) { barony_id = b; is_member = true; };
        if (!is_member) {
            response.error = "Not a member of a barony";
            return response;
        }

        int owner_character_id = 0;
        db << "SELECT owner_character_id FROM baronies WHERE id = ?;"
           << barony_id >> [&](int o) { owner_character_id = o; };
        int member_count = 0;
        db << "SELECT COUNT(*) FROM barony_members WHERE barony_id = ?;"
           << barony_id >> [&](int c) { member_count = c; };
        const bool is_founder = (owner_character_id == character_id);

        if (is_founder && member_count > 1) {
            if (!body.contains("successor_character_id") ||
                !body["successor_character_id"].is_number_integer()) {
                response.error = "successor_character_id required to leave as founder";
                return response;
            }
            int successor = body["successor_character_id"].get<int>();
            int successor_is_member = 0;
            db << "SELECT COUNT(*) FROM barony_members WHERE barony_id = ? AND character_id = ?;"
               << barony_id << successor >> [&](int c) { successor_is_member = c; };
            if (successor == character_id || successor_is_member == 0) {
                response.error = "successor must be another member of the barony";
                return response;
            }
            // Reassign ownership and leadership to the successor, who becomes baron.
            db << "UPDATE baronies SET owner_character_id = ?, baron_character_id = ? WHERE id = ?;"
               << successor << successor << barony_id;
            db << "UPDATE barony_members SET role = 'baron' WHERE barony_id = ? AND character_id = ?;"
               << barony_id << successor;
        } else if (is_founder && member_count <= 1) {
            // Last member left: the barony becomes defunct (tombstoned).
            db << "UPDATE baronies SET defunct_at = ? WHERE id = ?;" << now << barony_id;
        }

        db << "DELETE FROM barony_members WHERE character_id = ?;" << character_id;
        player_state_db::set_barony_left_at(db, character_id, now);

        // Keep the barony chat in step (archives it when tombstoned).
        chat::ChatService::instance().sync_barony_chat(barony_id);

        // New tenure: freehold (or freehold_qualified if already eligible).
        player_state_db::enter_freehold(db, character_id, now);
        const auto mesne_lord_reqs = progression::parse_mesne_lord_requirements(config_cache.getProgression());
        progression::check_freehold_qualification(db, character_id, now, mesne_lord_reqs);

        std::string new_phase;
        db << "SELECT game_phase FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](std::string p) { new_phase = p; };
        response.data["game_phase"] = new_phase;
        response.data["left_barony_id"] = barony_id;
        if (new_token) response.data["token"] = *new_token;
    } catch (const std::exception& e) {
        log_error("handleLeaveBarony", e.what());
        response.error = std::string("Failed to leave barony: ") + e.what();
    }
    return response;
}

/**
 * Apply to become a Mesne Lord: a qualified freeholder founds an Honor (a
 * barony under its pre-elevation name). Reuses the freeholder's existing manor
 * and enters the same `sandbox` state as the baron-track founders.
 */
ApiResponse handleApplyForMesneLord(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }
    int character_id = body["character_id"].get<int>();

    std::string honor_name = body.value("name", "");
    honor_name.erase(0, honor_name.find_first_not_of(" \t\r\n"));
    honor_name.erase(honor_name.find_last_not_of(" \t\r\n") + 1);
    if (honor_name.empty()) {
        response.error = "name required";
        return response;
    }
    if (honor_name.length() > 64) {
        response.error = "name must be 64 characters or fewer";
        return response;
    }

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    try {
        auto& db = Database::getInstance().gameDB();
        std::string ownership_error;
        if (!character_owned_by(db, username, character_id, ownership_error)) {
            response.error = ownership_error;
            return response;
        }

        std::string current_phase;
        db << "SELECT game_phase FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](std::string p) { current_phase = p; };
        if (current_phase != "freehold_qualified") {
            response.error = "You are not eligible to become a mesne lord";
            return response;
        }

        // Defense in depth: re-check the requirements.
        const auto reqs = progression::parse_mesne_lord_requirements(config_cache.getProgression());
        if (!progression::meets_mesne_lord_requirements(db, character_id, reqs)) {
            response.error = "You are not eligible to become a mesne lord";
            return response;
        }

        int existing_member = 0;
        db << "SELECT COUNT(*) FROM barony_members WHERE character_id = ?;"
           << character_id >> [&](int c) { existing_member = c; };
        if (existing_member > 0) {
            response.error = "Already a member of a barony";
            return response;
        }

        // Honor names stay reserved even after a barony is tombstoned.
        int name_taken = 0;
        db << "SELECT COUNT(*) FROM baronies WHERE LOWER(name) = LOWER(?);"
           << honor_name >> [&](int c) { name_taken = c; };
        if (name_taken > 0) {
            response.error = "A barony with that name already exists";
            return response;
        }

        int fiefdom_id = ensure_fiefdom_for_character(db, config_cache, character_id, *username, now);

        db << "INSERT INTO baronies (name, owner_character_id, description, created_at) "
              "VALUES (?, ?, '', ?);"
           << honor_name << character_id << now;
        int barony_id = static_cast<int>(db.last_insert_rowid());

        db << "INSERT INTO barony_members (barony_id, character_id, fiefdom_id, joined_at, role) "
              "VALUES (?, ?, ?, ?, 'mesne_lord');"
           << barony_id << character_id << fiefdom_id << now;

        chat::ChatService::instance().sync_barony_chat(barony_id);
        player_state_db::unlock_base(db, character_id, now);
        db << "UPDATE player_game_state SET honor_name = ? WHERE character_id = ?;"
           << honor_name << character_id;

        response.data["barony_id"] = barony_id;
        response.data["fiefdom_id"] = fiefdom_id;
        response.data["game_phase"] = "sandbox";
        response.data["base_unlocked"] = true;
        if (new_token) response.data["token"] = *new_token;
    } catch (const std::exception& e) {
        log_error("handleApplyForMesneLord", e.what());
        response.error = std::string("Failed to apply for mesne lord: ") + e.what();
    }
    return response;
}

ApiResponse handleGetLetters(GameConfigCache& config_cache, const json& body,
                             const std::optional<std::string>& username,
                             const ClientInfo& client,
                             const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }
    int character_id = body["character_id"].get<int>();

    try {
        auto& db = Database::getInstance().gameDB();
        std::string ownership_error;
        if (!character_owned_by(db, username, character_id, ownership_error)) {
            response.error = ownership_error;
            return response;
        }

        json letters_arr = json::array();
        for (const auto& letter : letters::get_letters(db, character_id)) {
            letters_arr.push_back({
                {"id", letter.id},
                {"letter_key", letter.letter_key},
                {"created_at", letter.created_at},
                {"read_at", letter.read_at}
            });
        }
        response.data["letters"] = letters_arr;
        if (new_token) response.data["token"] = *new_token;
    } catch (const std::exception& e) {
        log_error("handleGetLetters", e.what());
        response.error = std::string("Failed to get letters: ") + e.what();
    }
    return response;
}

ApiResponse handleMarkLetterRead(GameConfigCache& config_cache, const json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    if (!body.contains("character_id") || !body["character_id"].is_number_integer()) {
        response.error = "character_id required";
        return response;
    }
    if (!body.contains("letter_id") || !body["letter_id"].is_number_integer()) {
        response.error = "letter_id required";
        return response;
    }
    int character_id = body["character_id"].get<int>();
    int64_t letter_id = body["letter_id"].get<int64_t>();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    try {
        auto& db = Database::getInstance().gameDB();
        std::string ownership_error;
        if (!character_owned_by(db, username, character_id, ownership_error)) {
            response.error = ownership_error;
            return response;
        }
        if (!letters::mark_letter_read(db, character_id, letter_id, now)) {
            response.error = "Letter not found";
            return response;
        }
        response.data["result"] = "read";
        if (new_token) response.data["token"] = *new_token;
    } catch (const std::exception& e) {
        log_error("handleMarkLetterRead", e.what());
        response.error = std::string("Failed to mark letter read: ") + e.what();
    }
    return response;
}

ApiResponse handleGetBuildingConfigs(GameConfigCache& config_cache, const nlohmann::json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token)
{
    ApiResponse response;

    auto building_types = config_cache.getFiefdomBuildingTypes();
    json result = json::object();

    for (const auto& entry : building_types) {
        for (auto it = entry.begin(); it != entry.end(); ++it) {
            const std::string& type_id = it.key();
            json cfg = it.value();

            // Internal design notes (`descriptions`) never reach the client.
            cfg.erase("descriptions");

            // Normalize construction_image: if absent, copy image
            if (!cfg.contains("construction_image") && cfg.contains("image")) {
                cfg["construction_image"] = cfg["image"];
            } else if (!cfg.contains("construction_image")) {
                cfg["construction_image"] = "";
            }

            // Extract level-1 costs
            json costs = json::object();
            auto extract_cost = [&](const std::string& key, const std::string& res) {
                if (cfg.contains(key) && cfg[key].is_array() && !cfg[key].empty()) {
                    costs[res] = cfg[key][0];
                }
            };
            extract_cost("gold_cost", "gold");
            extract_cost("wood_cost", "wood");
            extract_cost("stone_cost", "stone");
            extract_cost("silver_pence_cost", "silver_pence");
            extract_cost("charcoal_cost", "charcoal");
            extract_cost("iron_cost", "iron");
            extract_cost("ironwork_cost", "ironwork");
            extract_cost("fancy_ironwork_cost", "fancy_ironwork");
            extract_cost("beams_cost", "beams");
            extract_cost("boards_cost", "boards");

            cfg["costs"] = costs;

            // Extract min_manor_level from prerequisites
            int min_level = 1;
            if (cfg.contains("prerequisites") && cfg["prerequisites"].is_array() && !cfg["prerequisites"].empty()) {
                auto prereq = cfg["prerequisites"][0];
                if (prereq.is_object() && prereq.contains("manor_level")) {
                    min_level = prereq["manor_level"].get<int>();
                }
            }
            cfg["min_manor_level"] = min_level;

            // Arable acres claimed by this building type (default 0).
            cfg["arable_acres"] = cfg.value("arable_acres", 0);

            // Forest acres claimed by this building type (wood producers only).
            cfg["forest_acres"] = cfg.value("forest_acres", 0);

            // Class grouping (definitive, independent of the built_from chain).
            cfg["class"] = cfg.value("class", "");

            // Exclusive group (at most one building from the group; "" = none).
            cfg["exclusive_group"] = cfg.value("exclusive_group", "");

            result[type_id] = cfg;
        }
    }

    response.data = result;

    // Build-palette ordering is config-driven via manor_ui.json (governs order
    // only; the client still applies its own display/level filters).
    json build_order = json::array();
    const json& manor_ui = config_cache.getManorUi();
    if (manor_ui.is_object() && manor_ui.contains("build_order") && manor_ui["build_order"].is_array()) {
        build_order = manor_ui["build_order"];
    }
    response.data["build_order"] = build_order;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

// ─────────────────────── Combat (realtime) REST ───────────────────────
// The combat game itself runs over the WebSocket route /ws/combat — see
// docs/combat_protocol.md. These REST endpoints handle the lobby phase only.

namespace {

// Verifies the request's character belongs to the authenticated user.
// Returns true on success, filling in user_id/display_name.
bool verify_character_ownership(const json& body,
                                const std::optional<std::string>& username,
                                int64_t& character_id, std::string& display_name) {
    if (!username) return false;
    character_id = body.value("character_id", 0);
    if (character_id == 0) return false;

    auto& db = Database::getInstance().gameDB();
    int owner_id = 0;
    int user_id = 0;
    db << "SELECT user_id FROM characters WHERE id = ?;" << character_id >> [&](int uid) { owner_id = uid; };
    db << "SELECT id FROM users WHERE username = ?;" << *username >> [&](int id) { user_id = id; };
    if (owner_id == 0 || owner_id != user_id) return false;

    db << "SELECT display_name FROM characters WHERE id = ?;"
       << character_id >> [&](std::string dn) { display_name = dn; };
    return true;
}

std::vector<combat::retinue_member_snapshot> load_retinue_snapshot(
    int64_t character_id,
    const retinue_db::recovery_settings& settings = retinue_db::recovery_settings{}) {
    auto& db = Database::getInstance().gameDB();
    auto members = retinue_db::load_active_retinue(db, character_id);
    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    retinue_db::apply_recovery(db, members, now, settings.multiplier, settings.max_recovery_hours);
    std::vector<combat::retinue_member_snapshot> snapshot;
    for (const auto& m : members) {
        if (m.health + 1e-9 < settings.min_deploy_hp) continue;  // locked in the infirmary
        combat::retinue_member_snapshot s;
        s.member_id = m.id;
        s.display_name = m.display_name;
        s.unit_class = m.unit_class;
        s.level = m.level;
        s.is_knight = m.is_knight;
        s.health = m.health;         // roster health % (HP bridge, §5.2)
        s.fieldable = true;          // above min_deploy_hp by construction
        snapshot.push_back(std::move(s));
    }
    return snapshot;
}

// Resolves one equipped cosmetic slot into an appearance-only JSON blob for the
// combat welcome snapshot (rts_design.md §15.3). Ownership is verified first
// (revoked items are never sent), and only the visual `appearance` object is
// copied — the simulation has no cosmetic read path (§15.5.1).
json resolve_equipped_appearance(GameConfigCache& config_cache, sqlite::database& db,
                                 int64_t character_id, const json& loadout,
                                 const std::string& slot) {
    if (!loadout.is_object()) return json();
    const std::string item_id = loadout.value(slot, "");
    if (item_id.empty()) return json();
    if (!cosmetics_db::owns_cosmetic(db, character_id, item_id)) return json();
    const json item = cosmetics_db::find_item(config_cache.getCosmetics(), item_id);
    if (item.is_null() || !item.contains("appearance") || !item["appearance"].is_object()) {
        return json();
    }
    json appearance = item["appearance"];
    appearance["item_id"] = item_id;
    appearance["slot"] = slot;
    return appearance;
}

// Recovery/fielding settings for a character's fiefdom: the retinue.json
// healing parameters plus additive `recovery_multiplier` bonuses from every
// completed `class: "infirmary"` building.
retinue_db::recovery_settings recovery_settings_for(GameConfigCache& config_cache,
                                                    int64_t character_id) {
    retinue_db::recovery_settings s;
    const auto& retinue_cfg = config_cache.getRetinueConfig();
    const json recovery_cfg = retinue_cfg.value("recovery", json::object());
    s.max_recovery_hours = recovery_cfg.value("max_recovery_hours", 16.0);
    s.min_deploy_hp = recovery_cfg.value("min_deploy_hp", 50.0);

    auto& db = Database::getInstance().gameDB();
    int fiefdom_id = 0;
    db << "SELECT id FROM fiefdoms WHERE owner_id = ? LIMIT 1;" << character_id
       >> [&](int id) { fiefdom_id = id; };
    if (fiefdom_id == 0) return s;

    std::map<std::string, double> type_bonus;
    const auto& types = config_cache.getFiefdomBuildingTypes();
    for (const auto& obj : types) {
        if (!obj.is_object()) continue;
        for (auto& [type_id, bcfg] : obj.items()) {
            if (!bcfg.is_object()) continue;
            if (config_cache.getBuildingClass(type_id) == "infirmary") {
                type_bonus[type_id] = bcfg.value("recovery_multiplier", 0.0);
            }
        }
    }
    db << "SELECT DISTINCT name FROM fiefdom_buildings WHERE fiefdom_id = ? AND level >= 1;"
       << fiefdom_id
       >> [&](std::string name) {
              auto it = type_bonus.find(name);
              if (it != type_bonus.end()) s.multiplier += it->second;
          };

    // Household morale also speeds healing: morale.recovery_morale_bonus of the
    // derived general-morale percent adds to the healing-rate multiplier.
    {
        const int64_t now_ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        int morale_funded = 0, morale_unfunded = 0;
        db << "SELECT COALESCE(SUM(maintained),0), "
              "COALESCE(SUM(CASE WHEN maintained = 0 THEN 1 ELSE 0 END),0) "
              "FROM retinue_members WHERE character_id = ? AND is_knight = 0 AND status = 'active';"
           << character_id
           >> [&](int f, int u) { morale_funded = f; morale_unfunded = u; };
        int64_t last_victory_ts = 0, last_defeat_ts = 0;
        db << "SELECT last_victory_ts, last_defeat_ts FROM fiefdoms WHERE id = ?;"
           << fiefdom_id
           >> [&](int64_t v, int64_t d) { last_victory_ts = v; last_defeat_ts = d; };
        auto morale_fiefdom = FiefdomFetcher::fetchFiefdomById(fiefdom_id);
        if (morale_fiefdom.has_value()) {
            const auto hm = Morale::computeHouseholdMorale(
                config_cache.getFiefdomBuildingTypes(), config_cache.getRetinueConfig(),
                morale_fiefdom->buildings, morale_funded, morale_unfunded,
                now_ts, last_victory_ts, last_defeat_ts);
            const json morale_cfg = config_cache.getRetinueConfig().value("morale", json::object());
            const double morale_recovery_bonus = morale_cfg.value("recovery_morale_bonus", 0.5);
            s.multiplier += (hm.percent / 100.0) * morale_recovery_bonus;
        }
    }

    return s;
}

} // namespace

ApiResponse handleCombatCreate(GameConfigCache& config_cache, const json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }

    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }

    const std::string mode = body.value("mode", "pve");
    if (mode != "pve") {
        response.error = "Only pve matches are available yet — pvp matchmaking is not implemented";
        return response;
    }

    // Mission opportunity (rts_design.md §3.1): the warband is fixed at create (D62).
    // An `opportunity_id` carries ruleset + map + warband; legacy create passes
    // ruleset_id/map_id directly and we derive the warband by matching.
    nlohmann::json opportunity;
    const auto& warbands_cfg = config_cache.getCombatWarbands();
    std::string ruleset_id = body.value("ruleset_id", "");
    std::string map_id = body.value("map_id", "");

    // Auto-generated offering (rts_design.md §3.1): the id encodes the seed, so
    // the offering is re-derived here and validated (authenticity + freshness).
    const std::string offering_id = body.value("offering_id", "");
    if (!offering_id.empty()) {
        mission_gen::offering_id_parts parts;
        if (!mission_gen::decode_offering_id(offering_id, parts)) {
            response.error = "invalid offering id";
            return response;
        }
        auto& game_db = Database::getInstance().gameDB();
        const int64_t now_ts =
            std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        if (mission_offerings::offering_is_completed(game_db, character_id, offering_id)) {
            response.error = "offering already completed";
            return response;
        }
        if (!mission_offerings::validate_offering_authentic(game_db, parts, character_id, now_ts,
                                                            config_cache)) {
            response.error = "offering is no longer valid";
            return response;
        }
        const mission_gen::mission_offering offering =
            mission_gen::generate_offering(parts, config_cache);
        opportunity = offering.to_json();
        ruleset_id = offering.ruleset;
        map_id = offering.map_id;
    }

    const std::string opportunity_id = body.value("opportunity_id", "");
    if (offering_id.empty() && !opportunity_id.empty()) {
        if (warbands_cfg.contains("opportunities") && warbands_cfg["opportunities"].is_array()) {
            for (const auto& opp : warbands_cfg["opportunities"]) {
                if (opp.value("id", "") == opportunity_id) {
                    opportunity = opp;
                    break;
                }
            }
        }
        if (opportunity.is_null()) {
            response.error = "unknown opportunity: " + opportunity_id;
            return response;
        }
        ruleset_id = opportunity.value("ruleset", "");
        map_id = opportunity.value("map", "");
    }

    if (ruleset_id.empty()) {
        response.error = "ruleset_id required";
        return response;
    }
    if (map_id.empty()) {
        response.error = "map_id required";
        return response;
    }

    // Look up the ruleset config (hot-reloadable).
    nlohmann::json ruleset;
    const auto& rulesets = config_cache.getCombatRulesets();
    if (rulesets.contains("rulesets") && rulesets["rulesets"].is_array()) {
        for (const auto& rs : rulesets["rulesets"]) {
            if (rs.value("id", "") == ruleset_id) {
                ruleset = rs;
                break;
            }
        }
    }
    if (ruleset.is_null()) {
        response.error = "unknown ruleset: " + ruleset_id;
        return response;
    }

    // Look up the map (hot-reloadable; format_version gate is in the cache).
    const std::string map_filename = map_id + ".json";
    const nlohmann::json map_metadata = CombatMapCache::get_instance().get_map(map_filename);
    if (map_metadata.is_null()) {
        response.error = "unknown map: " + map_id;
        return response;
    }

    // Build the match's config snapshot on the loop thread — the sim thread
    // only ever reads this copy (threading rule: never GameConfigCache).
    combat::combat_match_config config;
    config.ruleset = ruleset;
    config.map_metadata = map_metadata;
    config.player_combatants = config_cache.getPlayerCombatants();
    config.enemy_combatants = config_cache.getEnemyCombatants();
    config.structures = config_cache.getCombatStructures();
    config.formations = config_cache.getCombatFormations();
    config.profiles = config_cache.getCombatProfiles();
    config.rewards = config_cache.getCombatRewards();
    config.seed = g_combat_seed;   // 0 = random; --combat-seed makes it reproducible
    config.offering_id = offering_id;

    // Resolve the opportunity's warband (fixed at create) into spawn entries.
    const auto try_opportunity_warband = [&](const nlohmann::json& opp) -> bool {
        if (!opp.is_object() || !opp.contains("warband") || !opp["warband"].is_array()) return false;
        for (const auto& e : opp["warband"]) {
            if (!e.is_object()) continue;
            combat::warband_spawn_entry we;
            we.unit_class = e.value("unit_class", "");
            we.level = e.value("level", 1);
            we.count = e.value("count", 0);
            we.group = e.value("group", "");
            we.tactic = e.value("tactic", "");
            we.aggro_model = e.value("aggro_model", "");
            if (!we.unit_class.empty() && we.count > 0) {
                config.warband.push_back(std::move(we));
            }
        }
        return !config.warband.empty();
    };

    if (!opportunity.is_null()) {
        try_opportunity_warband(opportunity);
        config.opportunity = opportunity;
    } else if (warbands_cfg.contains("opportunities") && warbands_cfg["opportunities"].is_array()) {
        // Legacy create (ruleset+map): use the matching opportunity, else the
        // first authored one so PvE always has a real warband.
        for (const auto& opp : warbands_cfg["opportunities"]) {
            if (opp.value("ruleset", "") == ruleset_id && opp.value("map", "") == map_id) {
                opportunity = opp;
                break;
            }
        }
        if (opportunity.is_null()) {
            for (const auto& opp : warbands_cfg["opportunities"]) {
                if (try_opportunity_warband(opp)) {
                    opportunity = opp;
                    break;
                }
            }
        } else {
            try_opportunity_warband(opportunity);
        }
        config.opportunity = opportunity;
    }

    // Counter-based milestone conditions (progression.json "milestones"),
    // snapshotted so the match thread can evaluate them after counting deaths.
    const auto& progression_cfg = config_cache.getProgression();
    if (progression_cfg.is_object() && progression_cfg.contains("milestones") &&
        progression_cfg["milestones"].is_array()) {
        config.milestone_conditions = progression_cfg["milestones"];
    }

    // Host roster select (§4.2): an optional `unit_ids` subset filters the
    // created snapshot (the knight is always fielded, per the baked-in ruling).
    std::vector<combat::retinue_member_snapshot> host_retinue =
        load_retinue_snapshot(character_id, recovery_settings_for(config_cache, character_id));
    if (body.contains("unit_ids") && body["unit_ids"].is_array()) {
        std::set<int64_t> chosen;
        for (const auto& id : body["unit_ids"]) {
            if (id.is_number_integer()) chosen.insert(id.get<int64_t>());
        }
        if (!chosen.empty()) {
            std::vector<combat::retinue_member_snapshot> filtered;
            for (const auto& m : host_retinue) {
                if (m.is_knight || chosen.count(m.member_id)) filtered.push_back(m);
            }
            if (!filtered.empty()) host_retinue = std::move(filtered);
        }
    }

    std::string error;
    const std::string match_id = g_combat_manager.create_pve_match(
        std::move(config), character_id, display_name, host_retinue, error);
    if (match_id.empty()) {
        response.error = error.empty() ? "could not create match" : error;
        return response;
    }

    auto match = g_combat_manager.get_match(match_id);
    // Record the attempt now: the decision to start is trusted, so the result is
    // honoured regardless of later power changes.
    if (!offering_id.empty() && match) {
        const int64_t now_ts =
            std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        const int64_t attempt_id = mission_offerings::record_attempt_start(
            Database::getInstance().gameDB(), offering_id, character_id, match_id, now_ts);
        match->set_mission_attempt_id(attempt_id);
    }
    response.data["match_id"] = match_id;
    response.data["match_code"] = match->code();
    response.data["mode"] = "pve";
    response.data["ruleset_id"] = ruleset_id;
    response.data["map_id"] = map_id;
    if (!offering_id.empty()) {
        response.data["offering_id"] = offering_id;
        response.data["tier"] = opportunity.value("tier", std::string());
        response.data["danger_band"] = opportunity.value("danger_band", std::string());
    } else if (!opportunity.is_null()) {
        response.data["opportunity_id"] = opportunity.value("id", "");
        response.data["tier"] = opportunity.value("tier", 1);
    }

    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

ApiResponse handleCombatJoin(GameConfigCache& config_cache, const json& body,
                             const std::optional<std::string>& username,
                             const ClientInfo& client,
                             const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }

    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }

    const std::string match_code = body.value("match_code", "");
    if (match_code.empty()) {
        response.error = "match_code required";
        return response;
    }

    std::string error;
    auto match = g_combat_manager.join_match(match_code, character_id, display_name,
                                             load_retinue_snapshot(character_id, recovery_settings_for(config_cache, character_id)),
                                             error);
    if (!match) {
        response.error = error.empty() ? "could not join match" : error;
        return response;
    }

    response.data["match_id"] = match->id();
    response.data["match_code"] = match->code();
    response.data["mode"] = match->mode();

    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

ApiResponse handleCombatList(GameConfigCache& config_cache, const json& body,
                             const std::optional<std::string>& username,
                             const ClientInfo& client,
                             const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    response.data["matches"] = g_combat_manager.list_lobby_matches();
    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

ApiResponse handleCombatMatchmaking(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token)
{
    // PvP matchmaking (challenges/acceptances/handicaps) is a later milestone.
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    response.error = "PvP matchmaking is not yet implemented";
    return response;
}

namespace {

// The eight compass directions the directional sprite renderer indexes by
// (client `unit_sprites.ts`): 0=e, 1=se, 2=s, 3=sw, 4=w, 5=nw, 6=n, 7=ne.
const char* const k_combat_directions[] = {"e", "se", "s", "sw", "w", "nw", "n", "ne"};

// Counts the authored image frames in a combatant frame-set directory
// (images/combatants/<class>/<clip>/<set>/1.png …). The frame count is
// discovered from the directory contents — never declared in config — so an
// animation may have any number of frames. Returns 0 when the directory is
// missing or holds no images.
int count_combatant_set_frames(const std::string& class_id, const std::string& clip,
                               const std::string& set_name) {
    if (set_name.empty()) return 0;
    std::error_code ec;
    const std::filesystem::path set_dir =
        std::filesystem::path(g_images_dir) / "combatants" / class_id / clip / set_name;
    if (!std::filesystem::is_directory(set_dir, ec) || ec) return 0;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(set_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
            extension == ".webp" || extension == ".avif") {
            ++count;
        }
    }
    return count;
}

// Returns the subset of a class's animation clips that have at least one
// authored frame-set directory on disk (images/combatants/<class>/<clip>/<set>/).
// Each emitted clip carries the config `fps`, a discovered `frames` count (the
// largest authored set), and a `views` map narrowed to the directions whose
// resolved set directory exists. The client therefore only requests art that is
// actually present, and missing directions fall back to the nearest loaded set.
// Classes without art are omitted entirely and render procedurally.
nlohmann::json authored_combatant_clips(const std::string& class_id,
                                        const nlohmann::json& animations) {
    nlohmann::json result = nlohmann::json::object();
    if (!animations.is_object()) return result;
    for (auto clip = animations.begin(); clip != animations.end(); ++clip) {
        if (!clip.value().is_object()) continue;
        const nlohmann::json views =
            clip.value().contains("views") && clip.value()["views"].is_object()
                ? clip.value()["views"]
                : nlohmann::json::object();

        nlohmann::json effective_views = nlohmann::json::object();
        int max_frames = 0;
        for (const char* direction : k_combat_directions) {
            nlohmann::json view_value;
            std::string set_name;
            if (views.contains(direction)) {
                view_value = views[direction];
                if (view_value.is_string()) {
                    set_name = view_value.get<std::string>();
                } else if (view_value.is_object() && view_value.contains("set") &&
                           view_value["set"].is_string()) {
                    set_name = view_value["set"].get<std::string>();
                }
            } else {
                set_name = direction;
                view_value = direction;
            }
            const int frames = count_combatant_set_frames(class_id, clip.key(), set_name);
            if (frames > 0) {
                effective_views[direction] = view_value;
                if (frames > max_frames) max_frames = frames;
            }
        }
        if (effective_views.empty()) continue;
        nlohmann::json clip_out = clip.value();
        clip_out["views"] = std::move(effective_views);
        clip_out["frames"] = max_frames;
        result[clip.key()] = std::move(clip_out);
    }
    return result;
}

} // namespace

ApiResponse handleCombatGetConfigs(GameConfigCache& config_cache, const json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }

    nlohmann::json rulesets = nlohmann::json::array();
    const auto& ruleset_config = config_cache.getCombatRulesets();
    if (ruleset_config.contains("rulesets") && ruleset_config["rulesets"].is_array()) {
        for (const auto& rs : ruleset_config["rulesets"]) {
            nlohmann::json entry;
            entry["id"] = rs.value("id", "");
            entry["name"] = rs.value("name", "");
            entry["mode"] = rs.value("mode", "");
            rulesets.push_back(std::move(entry));
        }
    }

    nlohmann::json maps = nlohmann::json::array();
    for (const std::string& filename : CombatMapCache::get_instance().list_maps()) {
        const nlohmann::json map = CombatMapCache::get_instance().get_map(filename);
        nlohmann::json entry;
        entry["id"] = map.value("name", filename.substr(0, filename.size() - 5));
        entry["file"] = filename;
        maps.push_back(std::move(entry));
    }

    // Mission opportunities (rts_design.md §3.1): ruleset + map + warband +
    // tier + flavor; the warband is fixed at create.
    nlohmann::json opportunities = nlohmann::json::array();
    const auto& warbands = config_cache.getCombatWarbands();
    if (warbands.contains("opportunities") && warbands["opportunities"].is_array()) {
        for (const auto& opp : warbands["opportunities"]) {
            if (!opp.is_object()) continue;
            nlohmann::json entry;
            entry["id"] = opp.value("id", "");
            entry["name"] = opp.value("name", "");
            entry["tier"] = opp.value("tier", 1);
            entry["ruleset"] = opp.value("ruleset", "");
            entry["map"] = opp.value("map", "");
            entry["xp_base"] = opp.value("xp_base", 0);
            entry["flavor"] = opp.value("flavor", "");
            nlohmann::json composition = nlohmann::json::array();
            int total_units = 0;
            if (opp.contains("warband") && opp["warband"].is_array()) {
                for (const auto& w : opp["warband"]) {
                    if (!w.is_object()) continue;
                    const int count = w.value("count", 0);
                    const int level = w.value("level", 1);
                    total_units += count;
                    composition.push_back({{"unit_class", w.value("unit_class", "")},
                                           {"count", count},
                                           {"level", level}});
                }
            }
            entry["warband"] = composition;
            entry["total_units"] = total_units;
            opportunities.push_back(std::move(entry));
        }
    }

    // Temporary-structure catalog for the client's Construct palette (§7.1).
    nlohmann::json structures = nlohmann::json::object();
    const auto& structure_cfg = config_cache.getCombatStructures();
    if (structure_cfg.contains("structures") && structure_cfg["structures"].is_object()) {
        for (const auto& [structure_id, def] : structure_cfg["structures"].items()) {
            if (!def.is_object()) continue;
            nlohmann::json entry;
            entry["id"] = structure_id;
            entry["name"] = def.value("name", structure_id);
            entry["footprint"] = def.value("footprint", 1);
            entry["max_hp"] = def.value("max_hp", 0);
            entry["build_time_seconds"] = def.value("build_time_seconds", 0);
            entry["max_per_player"] = def.value("max_per_player", 0);
            entry["blocks_ranged"] = def.value("blocks_ranged", false);
            structures[structure_id] = std::move(entry);
        }
    }
    response.data["structures"] = structures;

    // Formation shapes for the client's Formation palette (§8.3).
    nlohmann::json formations = nlohmann::json::object();
    const auto& formation_cfg = config_cache.getCombatFormations();
    if (formation_cfg.contains("formations") && formation_cfg["formations"].is_object()) {
        for (const auto& [formation_id, def] : formation_cfg["formations"].items()) {
            nlohmann::json entry;
            entry["id"] = formation_id;
            entry["name"] = def.is_object() ? def.value("name", formation_id) : formation_id;
            formations[formation_id] = std::move(entry);
        }
    }
    response.data["formations"] = formations;

    // Combatant art (rts_design.md D24/D71): per-class animation clips for the
    // client's directional sprite renderer. Only classes/clips with authored
    // frame sets are emitted; everything else falls back to the procedural draw.
    nlohmann::json combatant_art = nlohmann::json::object();
    auto collect_combatant_art = [&](const nlohmann::json& combatants) {
        if (!combatants.is_object()) return;
        for (auto it = combatants.begin(); it != combatants.end(); ++it) {
            if (!it.value().is_object() || !it.value().contains("animations")) continue;
            nlohmann::json clips = authored_combatant_clips(it.key(), it.value()["animations"]);
            if (!clips.empty()) combatant_art[it.key()] = std::move(clips);
        }
    };
    collect_combatant_art(config_cache.getPlayerCombatants());
    collect_combatant_art(config_cache.getEnemyCombatants());

    // Per-class body size (§5.6): the client sizes deploy/roster tokens and the
    // in-battle drawn sprite from the class radius (`size`), so bigger units read
    // as bigger regardless of their source image dimensions.
    nlohmann::json combatant_radius = nlohmann::json::object();
    auto collect_combatant_radius = [&](const nlohmann::json& combatants) {
        if (!combatants.is_object()) return;
        for (auto it = combatants.begin(); it != combatants.end(); ++it) {
            if (!it.value().is_object()) continue;
            double size = 0.03;   // crash guard; `size` is required by the linter
            if (it.value().contains("size") && it.value()["size"].is_number()) {
                size = it.value()["size"].get<double>();
            }
            combatant_radius[it.key()] = size;
        }
    };
    collect_combatant_radius(config_cache.getPlayerCombatants());
    collect_combatant_radius(config_cache.getEnemyCombatants());
    response.data["combatant_radius"] = std::move(combatant_radius);

    response.data["combatant_art"] = std::move(combatant_art);
    response.data["rulesets"] = rulesets;
    response.data["maps"] = maps;
    response.data["opportunities"] = opportunities;

    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

// Auto-generated PvE mission offerings (rts_design.md §3.1): the current-hour
// board for the character (all applicable tiers), minus completed offerings.
// Offerings are never stored — the client receives them and passes the chosen
// `offering_id` back to combatCreate, which re-derives and validates it.
ApiResponse handleGetCombatOfferings(GameConfigCache& config_cache, const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token)
{
    ApiResponse response;
    (void)client;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }

    const int64_t now_ts =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto& db = Database::getInstance().gameDB();

    nlohmann::json offerings = nlohmann::json::array();
    for (const auto& off : mission_offerings::load_board(db, character_id, now_ts, config_cache)) {
        offerings.push_back(off.to_json());
    }

    nlohmann::json tiers = nlohmann::json::array();
    for (const auto& t : mission_gen::parse_tiers(config_cache.getCombatOfferings())) {
        tiers.push_back({{"id", t.id},
                         {"min_players", t.min_players},
                         {"max_players", t.max_players}});
    }
    response.data["tiers"] = std::move(tiers);
    response.data["offerings"] = std::move(offerings);

    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

// ── Premium variety: Platinum + cosmetics store (rts_design.md §15) ──────────
// Platinum is a separate ledger, never fungible with gold/silver_pence. These
// endpoints are server-authoritative: ownership and balances are derived from
// the DB, purchases are idempotent, and the catalog is whitelist-validated so a
// cosmetic can carry no gameplay key.
namespace {
bool is_valid_cosmetic_slot(const std::string& slot) {
    return slot == "banner" || slot == "knight" || slot == "squad_uniform" ||
           slot == "unit" || slot == "structure" || slot == "flourish";
}
} // namespace

ApiResponse handleGetStore(GameConfigCache& config_cache, const json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token)
{
    ApiResponse response;
    (void)client;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const auto& catalog = config_cache.getCosmetics();
    response.data["items"] = catalog.value("items", json::array());
    response.data["bundles"] = catalog.value("bundles", json::array());
    response.data["products"] = catalog.value("products", json::array());
    auto& db = Database::getInstance().gameDB();
    response.data["platinum"] = cosmetics_db::get_platinum(db, character_id);
    response.data["owned"] = cosmetics_db::owned_cosmetics(db, character_id);
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleGetCosmetics(GameConfigCache& config_cache, const json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token)
{
    ApiResponse response;
    (void)config_cache;
    (void)client;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    response.data["owned"] = cosmetics_db::owned_cosmetics(db, character_id);
    response.data["loadout"] = cosmetics_db::get_loadout(db, character_id);
    response.data["platinum"] = cosmetics_db::get_platinum(db, character_id);
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handlePurchasePremiumItem(GameConfigCache& config_cache, const json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token)
{
    ApiResponse response;
    (void)client;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const std::string idempotency_key = body.value("idempotency_key", "");
    if (idempotency_key.empty()) {
        response.error = "idempotency_key required";
        return response;
    }
    const auto& catalog = config_cache.getCosmetics();
    auto& db = Database::getInstance().gameDB();
    const int64_t now_ts =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    const std::string item_id = body.value("item_id", "");
    const std::string bundle_id = body.value("bundle_id", "");
    std::string outcome;
    if (!item_id.empty()) {
        const json item = cosmetics_db::find_item(catalog, item_id);
        if (item.is_null()) {
            response.error = "unknown item: " + item_id;
            return response;
        }
        const int64_t price = item.value("price_platinum", 0);
        outcome = cosmetics_db::purchase_cosmetic(db, character_id, item_id, price,
                                                  idempotency_key, now_ts);
    } else if (!bundle_id.empty()) {
        const json bundle = cosmetics_db::find_bundle(catalog, bundle_id);
        if (bundle.is_null()) {
            response.error = "unknown bundle: " + bundle_id;
            return response;
        }
        std::vector<std::string> item_ids;
        if (bundle.contains("items") && bundle["items"].is_array()) {
            for (const auto& ref : bundle["items"]) {
                if (ref.is_string()) item_ids.push_back(ref.get<std::string>());
            }
        }
        const int64_t price = bundle.value("price_platinum", 0);
        outcome = cosmetics_db::purchase_bundle(db, character_id, bundle_id, item_ids, price,
                                                idempotency_key, now_ts);
    } else {
        response.error = "item_id or bundle_id required";
        return response;
    }

    if (outcome == "insufficient") {
        response.error = "insufficient platinum";
        return response;
    }
    response.data["result"] = outcome;   // ok | already_owned | duplicate
    response.data["platinum"] = cosmetics_db::get_platinum(db, character_id);
    response.data["owned"] = cosmetics_db::owned_cosmetics(db, character_id);
    return response;
}

ApiResponse handlePurchasePlatinum(GameConfigCache& config_cache, const json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token)
{
    ApiResponse response;
    (void)client;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const std::string product_id = body.value("product_id", "");
    const std::string idempotency_key = body.value("idempotency_key", "");
    if (product_id.empty() || idempotency_key.empty()) {
        response.error = "product_id and idempotency_key required";
        return response;
    }
    const json product = cosmetics_db::find_product(config_cache.getCosmetics(), product_id);
    if (product.is_null()) {
        response.error = "unknown product: " + product_id;
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    const int64_t now_ts =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    if (cosmetics_db::transaction_exists(db, idempotency_key)) {
        response.data["result"] = "duplicate";
    } else {
        const int64_t grant = product.value("platinum", 0);
        const int64_t tx = cosmetics_db::record_transaction(db, character_id, product_id, grant,
                                                            "pending", idempotency_key, now_ts);
        response.data["result"] = "pending";
        response.data["transaction_id"] = tx;
    }
    // Gateway is a stub (rts_design.md §15.1): the real provider settles later.
    response.data["product_id"] = product_id;
    response.data["payment_intent"] = "mock://platinum/" + idempotency_key;
    response.data["price_real"] = product.value("price_real", 0.0);
    response.data["platinum"] = cosmetics_db::get_platinum(db, character_id);
    return response;
}

ApiResponse handleSettlePlatinum(GameConfigCache& config_cache, const json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token)
{
    // Mock gateway webhook stand-in — the only place Platinum is minted.
    ApiResponse response;
    (void)config_cache;
    (void)client;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const std::string idempotency_key = body.value("idempotency_key", "");
    if (idempotency_key.empty()) {
        response.error = "idempotency_key required";
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    const int64_t now_ts =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto [tx_character, granted] =
        cosmetics_db::settle_transaction(db, idempotency_key, character_id, now_ts);
    if (tx_character == 0) {
        response.data["result"] = cosmetics_db::transaction_exists(db, idempotency_key)
                                      ? "already_settled"
                                      : "not_found";
        response.data["platinum"] = cosmetics_db::get_platinum(db, character_id);
        return response;
    }
    response.data["result"] = "settled";
    response.data["granted"] = granted;
    response.data["platinum"] = cosmetics_db::get_platinum(db, character_id);
    return response;
}

ApiResponse handleEquipCosmetic(GameConfigCache& config_cache, const json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token)
{
    ApiResponse response;
    (void)client;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const std::string slot = body.value("slot", "");
    const std::string cosmetic_id = body.value("cosmetic_id", "");
    if (!is_valid_cosmetic_slot(slot)) {
        response.error = "unknown slot: " + slot;
        return response;
    }
    if (cosmetic_id.empty()) {
        response.error = "cosmetic_id required";
        return response;
    }
    const json item = cosmetics_db::find_item(config_cache.getCosmetics(), cosmetic_id);
    if (item.is_null()) {
        response.error = "unknown cosmetic: " + cosmetic_id;
        return response;
    }
    if (item.value("slot", "") != slot) {
        response.error = "cosmetic does not fit slot " + slot;
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    if (!cosmetics_db::owns_cosmetic(db, character_id, cosmetic_id)) {
        response.error = "cosmetic not owned";
        return response;
    }
    json loadout = cosmetics_db::get_loadout(db, character_id);
    loadout[slot] = cosmetic_id;
    cosmetics_db::set_loadout(db, character_id, loadout);
    response.data["loadout"] = loadout;
    return response;
}

ApiResponse handleDeassignCosmetic(GameConfigCache& config_cache, const json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token)
{
    ApiResponse response;
    (void)config_cache;
    (void)client;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const std::string slot = body.value("slot", "");
    if (!is_valid_cosmetic_slot(slot)) {
        response.error = "unknown slot: " + slot;
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    json loadout = cosmetics_db::get_loadout(db, character_id);
    loadout.erase(slot);
    cosmetics_db::set_loadout(db, character_id, loadout);
    response.data["loadout"] = loadout;
    return response;
}

ApiResponse handleRefundPremiumItem(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token)
{
    // Refunds revoke (rts_design.md §15.4/§15.8): the item is marked revoked and
    // cleared from any loadout slot; no Platinum is credited. Server-authoritative
    // and idempotent (a second call reports not_owned).
    ApiResponse response;
    (void)client;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const std::string item_id = body.value("item_id", "");
    if (item_id.empty()) {
        response.error = "item_id required";
        return response;
    }
    const json item = cosmetics_db::find_item(config_cache.getCosmetics(), item_id);
    if (item.is_null()) {
        response.error = "unknown item: " + item_id;
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    if (!cosmetics_db::refund_cosmetic(db, character_id, item_id)) {
        response.data["result"] = "not_owned";
        response.data["owned"] = cosmetics_db::owned_cosmetics(db, character_id);
        response.data["loadout"] = cosmetics_db::get_loadout(db, character_id);
        return response;
    }
    response.data["result"] = "revoked";
    response.data["owned"] = cosmetics_db::owned_cosmetics(db, character_id);
    response.data["loadout"] = cosmetics_db::get_loadout(db, character_id);
    return response;
}

ApiResponse handleGetRetinue(GameConfigCache& config_cache, const json& body,
                             const std::optional<std::string>& username,
                             const ClientInfo& client,
                             const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }

    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }

    auto& db = Database::getInstance().gameDB();
    auto roster = retinue_db::load_retinue(db, character_id);
    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const retinue_db::recovery_settings rec = recovery_settings_for(config_cache, character_id);
    retinue_db::apply_recovery(db, roster, now, rec.multiplier, rec.max_recovery_hours);
    // The knight is the character itself, so its portrait resolves from the
    // owner's PFP (age-gated for the viewer); soldiers use their headshot.
    const bool viewer_adult = user_is_adult(username);
    nlohmann::json members = nlohmann::json::array();
    for (const auto& m : roster) {
        nlohmann::json j = m.to_json();
        j["fieldable"] = (m.health + 1e-9 >= rec.min_deploy_hp);
        if (m.is_knight) {
            j["portrait_url"] = resolve_avatar_url(character_id, viewer_adult);
        } else if (!m.headshot_id.empty()) {
            j["portrait_url"] = AvatarPool::headshot_url(m.headshot_id);
        } else {
            j["portrait_url"] = "";
        }
        members.push_back(std::move(j));
    }
    response.data["members"] = members;
    response.data["recovery"] = {
        {"max_recovery_hours", rec.max_recovery_hours},
        {"min_deploy_hp", rec.min_deploy_hp},
        {"multiplier", rec.multiplier}
    };

    // Retinue capacity is keyed to the manor level; the knight is free.
    int fiefdom_id = 0;
    int manor_level = 0;
    db << "SELECT id, manor_level FROM fiefdoms WHERE owner_id = ? LIMIT 1;"
       << character_id >> [&](int id, int ml) { fiefdom_id = id; manor_level = ml; };
    const int capacity = (fiefdom_id > 0) ? config_cache.getRetinueCapacity(manor_level) : 0;
    const int recruited = (fiefdom_id > 0)
        ? static_cast<int>(retinue_db::recruited_member_count(db, character_id)) : 0;
    response.data["capacity"] = {
        {"manor_level", manor_level},
        {"total", capacity},
        {"recruited", recruited},
        {"available", (capacity - recruited > 0) ? (capacity - recruited) : 0}
    };

    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

// Lists the current recruit market offers for a character's manor: the current
// hour cohort plus the previous hour's (still-hirable, one-hour grace). Offers
// are deterministic from (character_id, hour bucket), never stored.
ApiResponse handleListRecruitCandidates(GameConfigCache& config_cache, const json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }

    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }

    auto& db = Database::getInstance().gameDB();
    int fiefdom_id = 0;
    int manor_level = 0;
    db << "SELECT id, manor_level FROM fiefdoms WHERE owner_id = ? LIMIT 1;"
       << character_id >> [&](int id, int ml) { fiefdom_id = id; manor_level = ml; };
    if (fiefdom_id == 0) {
        response.error = "You need a manor before you can recruit a household.";
        return response;
    }

    // Identities already in the roster (knight included) seed the uniqueness
    // walk, so the displayed offers never duplicate a serving member.
    std::set<std::string> used_names;
    std::set<std::string> used_headshots;
    for (const auto& member : retinue_db::load_retinue(db, character_id)) {
        if (!member.display_name.empty()) used_names.insert(lower_ascii(member.display_name));
        if (!member.headshot_id.empty()) used_headshots.insert(member.headshot_id);
    }

    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    int current_bucket = 0;
    int64_t next_refresh_at = 0;
    auto candidates = retinue_market::list_candidates(
        config_cache.getPlayerCombatants(), config_cache.getRetinueConfig(),
        character_id, now, manor_level, g_text_dir, used_names, used_headshots,
        current_bucket, next_refresh_at);

    json arr = json::array();
    for (auto& c : candidates) {
        arr.push_back(c.to_json());
    }
    const int capacity = config_cache.getRetinueCapacity(manor_level);
    const int recruited = static_cast<int>(retinue_db::recruited_member_count(db, character_id));
    response.data["candidates"] = arr;
    response.data["current_bucket"] = current_bucket;
    response.data["next_refresh_at"] = next_refresh_at;
    response.data["manor_level"] = manor_level;
    response.data["capacity"] = {
        {"total", capacity}, {"recruited", recruited},
        {"available", (capacity - recruited > 0) ? (capacity - recruited) : 0}
    };
    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

// Hires a candidate from the market. The offer is validated structurally (known
// + unlocked class, in-range level, current/previous hour bucket, name from the
// class/sex pool, existing headshot for the sex), must be affordable (gold +
// materials from fiefdom stores), and the manor must have headroom. Name and
// headshot uniqueness are a generation-time property, not a server gate.
ApiResponse handleHireRecruit(GameConfigCache& config_cache, const json& body,
                              const std::optional<std::string>& username,
                              const ClientInfo& client,
                              const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }

    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }

    auto& db = Database::getInstance().gameDB();
    int fiefdom_id = 0;
    int manor_level = 0;
    db << "SELECT id, manor_level FROM fiefdoms WHERE owner_id = ? LIMIT 1;"
       << character_id >> [&](int id, int ml) { fiefdom_id = id; manor_level = ml; };
    if (fiefdom_id == 0) {
        response.error = "You need a manor before you can recruit a household.";
        return response;
    }

    recruit_candidate offer;
    offer.unit_class = body.value("unit_class", "");
    offer.display_name = body.value("display_name", "");
    offer.gender = body.value("gender", "");
    offer.headshot_id = body.value("headshot_id", "");
    offer.level = body.value("level", 0);
    offer.hour_bucket = body.value("hour_bucket", -1);
    if (offer.unit_class.empty() || offer.display_name.empty() ||
        offer.gender.empty() || offer.level < 1 || offer.hour_bucket < 0) {
        response.error = "Missing or invalid recruit offer fields.";
        return response;
    }

    if (!retinue_market::is_valid_candidate(
            config_cache.getPlayerCombatants(), config_cache.getRetinueConfig(),
            manor_level, g_text_dir, offer)) {
        response.error = "That offer has expired or was never made.";
        return response;
    }

    // Capacity check (the knight is free).
    const int capacity = config_cache.getRetinueCapacity(manor_level);
    const int recruited = static_cast<int>(retinue_db::recruited_member_count(db, character_id));
    if (recruited >= capacity) {
        response.error = "Your manor cannot house anyone else in your retinue.";
        return response;
    }

    // The hire fee is the class cost at the offered level.
    const auto& player_combatants = config_cache.getPlayerCombatants();
    if (!player_combatants.is_object() || !player_combatants.contains(offer.unit_class)) {
        response.error = "Unknown unit class.";
        return response;
    }
    const json fee = retinue_market::compute_recruit_fee_for(
        player_combatants[offer.unit_class], offer.level);

    // Pay the fee from the fungible fiefdom wallet, auto-importing any material
    // shortfalls with money (respecting per-resource import toggles). Same path
    // buildings use — gold and silver_pence are one wallet at pence_per_gold.
    if (!GameLogic::Validation::hasEnoughResources(config_cache, fiefdom_id, fee)) {
        response.error = "You cannot afford this candidate's fee (gold + materials).";
        return response;
    }
    GameLogic::ActionResult deduct_result;
    if (GameLogic::Validation::deductResources(config_cache, fiefdom_id, fee, deduct_result).status != GameLogic::ActionStatus::OK) {
        response.error = "You cannot afford this candidate's fee (gold + materials).";
        return response;
    }

    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto member = retinue_db::hire_member(db, character_id, offer.display_name,
                                          offer.unit_class, offer.gender,
                                          offer.headshot_id, offer.level, now);
    response.data["member"] = member.to_json();
    response.data["fee"] = fee;
    response.data["capacity"] = {
        {"total", capacity}, {"recruited", recruited + 1},
        {"available", (capacity - recruited - 1 > 0) ? (capacity - recruited - 1) : 0}
    };
    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

// Reorders the roster by strict priority (funding + infirmary-bed order). The
// knight is always first (priority 0); `member_ids` lists the non-knight
// members in their new order (a full permutation of the current roster).
ApiResponse handleSetRetinuePriority(GameConfigCache& config_cache, const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }

    if (!body.contains("member_ids") || !body["member_ids"].is_array()) {
        response.error = "member_ids (an ordered array of member ids) is required.";
        return response;
    }
    std::vector<int64_t> ordered;
    for (const auto& v : body["member_ids"]) {
        if (v.is_number_integer()) ordered.push_back(v.get<int64_t>());
    }

    auto& db = Database::getInstance().gameDB();
    if (!retinue_db::reorder_priority(db, character_id, ordered)) {
        response.error = "member_ids must be exactly your current non-knight members, in your preferred order.";
        return response;
    }

    // Reflect the new ranking back so the client can re-render without refetch.
    auto roster = retinue_db::load_retinue(db, character_id);
    json members = json::array();
    for (const auto& m : roster) {
        members.push_back({{"id", m.id}, {"priority", m.priority}});
    }
    response.data["members"] = members;
    if (new_token) {
        response.data["token"] = *new_token;
    }
    return response;
}

// ── Phase 5a: technology trees, forge orders, armory/storage, gear ─────────

// The character's fiefdom id (0 if none).
static int fiefdom_id_for_character(int64_t character_id) {
    auto& db = Database::getInstance().gameDB();
    int id = 0;
    db << "SELECT id FROM fiefdoms WHERE owner_id = ? LIMIT 1;" << character_id
       >> [&](int i) { id = i; };
    return id;
}

// True if a building row belongs to the given fiefdom.
static bool building_belongs_to_fiefdom(int64_t building_id, int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    int n = 0;
    db << "SELECT COUNT(*) FROM fiefdom_buildings WHERE id = ? AND fiefdom_id = ?;"
       << building_id << fiefdom_id >> [&](int c) { n = c; };
    return n > 0;
}

// Reads a building's tech/order state (xp, learned nodes, active forge/training).
static void load_building_tech(int64_t building_id, double& xp, std::vector<std::string>& nodes,
                               std::string& forge_order, std::string& training) {
    auto& db = Database::getInstance().gameDB();
    db << "SELECT tech_xp, tech_nodes, forge_order, training FROM fiefdom_buildings WHERE id = ?;"
       << building_id
       >> [&](double xp_val, std::string node_json, std::string fo, std::string tr) {
           xp = xp_val;
           forge_order = std::move(fo);
           training = std::move(tr);
           try {
               auto arr = json::parse(node_json);
               if (arr.is_array()) {
                   for (auto& n : arr) if (n.is_string()) nodes.push_back(n.get<std::string>());
               }
           } catch (...) {}
       };
}

// The building type config object for a placed building (by its current name).
static json building_type_config(GameConfigCache& config_cache, const std::string& type_id) {
    for (const auto& obj : config_cache.getFiefdomBuildingTypes()) {
        if (obj.is_object() && obj.contains(type_id)) return obj[type_id];
    }
    return json::object();
}

ApiResponse handleGetTechTrees(GameConfigCache& config_cache, const json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) { response.needs_auth = true; return response; }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const int fiefdom_id = fiefdom_id_for_character(character_id);
    auto& db = Database::getInstance().gameDB();

    response.data["trees"] = config_cache.getTechTreesConfig().value("trees", json::object());

    json buildings = json::array();
    if (fiefdom_id > 0) {
        // Buildings that carry a tech tree, with their xp / learned nodes /
        // active forge + training state.
        db << "SELECT id, name, level, tech_xp, tech_nodes, forge_order, training "
              "FROM fiefdom_buildings WHERE fiefdom_id = ? AND level >= 1;"
           << fiefdom_id
           >> [&](int64_t bid, std::string name, int level, double xp, std::string nodes,
                  std::string forge_order, std::string training) {
               const json cfg = building_type_config(config_cache, name);
               if (!cfg.is_object() || !cfg.contains("tech_trees")) return;
               json b = json::object();
               b["id"] = bid;
               b["name"] = name;
               b["level"] = level;
               b["tech_trees"] = cfg.value("tech_trees", json::array());
               b["tech_xp"] = xp;
               try {
                   b["tech_nodes"] = json::parse(nodes);
               } catch (...) { b["tech_nodes"] = json::array(); }
               b["forge_order"] = forge_order.empty() ? json() : json::parse(forge_order);
               b["training"] = training.empty() ? json() : json::parse(training);
               buildings.push_back(std::move(b));
           };
    }
    response.data["buildings"] = buildings;
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleLearnTechNode(GameConfigCache& config_cache, const json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) { response.needs_auth = true; return response; }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const int fiefdom_id = fiefdom_id_for_character(character_id);
    if (fiefdom_id == 0) { response.error = "You need a manor."; return response; }

    const int64_t building_id = body.value("building_id", 0LL);
    const std::string node_id = body.value("node_id", "");
    if (building_id == 0 || node_id.empty()) {
        response.error = "building_id and node_id are required.";
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    if (!building_belongs_to_fiefdom(building_id, fiefdom_id)) {
        response.error = "Building does not belong to your fiefdom.";
        return response;
    }

    std::string bname;
    db << "SELECT name FROM fiefdom_buildings WHERE id = ?;" << building_id
       >> [&](std::string n) { bname = std::move(n); };
    const json cfg = building_type_config(config_cache, bname);
    if (!cfg.contains("tech_trees")) {
        response.error = "This building has no technology tree.";
        return response;
    }
    const auto& trees_root = config_cache.getTechTreesConfig().value("trees", json::object());
    for (const auto& tree_id : cfg["tech_trees"]) {
        const std::string tid = tree_id.get<std::string>();
        if (!trees_root.contains(tid)) continue;
        const auto& tree = trees_root[tid];
        if (!tree.contains("nodes")) continue;
        for (const auto& node : tree["nodes"]) {
            if (!node.is_object() || node.value("id", "") != node_id) continue;

            double xp = 0;
            std::vector<std::string> nodes;
            std::string forge_order, training;
            load_building_tech(building_id, xp, nodes, forge_order, training);

            const int cost = node.value("xp_cost", 0);
            if (xp + 1e-9 < cost) {
                response.error = "Not enough technology XP for this node.";
                return response;
            }
            for (const auto& prereq : node.value("prerequisites", json::array())) {
                if (std::find(nodes.begin(), nodes.end(), prereq.get<std::string>()) == nodes.end()) {
                    response.error = "Prerequisite nodes not learned.";
                    return response;
                }
            }
            if (std::find(nodes.begin(), nodes.end(), node_id) != nodes.end()) {
                response.error = "Node already learned.";
                return response;
            }

            nodes.push_back(node_id);
            json node_json = json::array();
            for (const auto& n : nodes) node_json.push_back(n);
            db << "UPDATE fiefdom_buildings SET tech_xp = ?, tech_nodes = ? WHERE id = ?;"
               << (xp - cost) << node_json.dump() << building_id;
            response.data["node_id"] = node_id;
            response.data["tech_xp"] = xp - cost;
            response.data["tech_nodes"] = node_json;
            if (new_token) response.data["token"] = *new_token;
            return response;
        }
    }
    response.error = "Unknown technology node for this building.";
    return response;
}

ApiResponse handleStartForgeOrder(GameConfigCache& config_cache, const json& body,
                                  const std::optional<std::string>& username,
                                  const ClientInfo& client,
                                  const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) { response.needs_auth = true; return response; }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const int fiefdom_id = fiefdom_id_for_character(character_id);
    if (fiefdom_id == 0) { response.error = "You need a manor."; return response; }

    const int64_t building_id = body.value("building_id", 0LL);
    const std::string item_id = body.value("item_id", "");
    if (building_id == 0 || item_id.empty()) {
        response.error = "building_id and item_id are required.";
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    if (!building_belongs_to_fiefdom(building_id, fiefdom_id)) {
        response.error = "Building does not belong to your fiefdom.";
        return response;
    }

    const auto& items_root = config_cache.getEquipmentConfig().value("items", json::object());
    if (!items_root.contains(item_id)) {
        response.error = "Unknown equipment item.";
        return response;
    }
    const json& item_cfg = items_root[item_id];
    if (!item_cfg.contains("craft")) {
        response.error = "This item cannot be forged at the manor.";
        return response;
    }
    const json craft = item_cfg["craft"];
    const std::string required_node = craft.value("tech_node", "");

    std::string bname;
    db << "SELECT name FROM fiefdom_buildings WHERE id = ?;" << building_id
       >> [&](std::string n) { bname = std::move(n); };
    const json cfg = building_type_config(config_cache, bname);
    const int max_orders = cfg.value("max_concurrent_orders", 1);

    double xp = 0;
    std::vector<std::string> nodes;
    std::string forge_order, training;
    load_building_tech(building_id, xp, nodes, forge_order, training);

    // One active forge order per building by default (config max_concurrent_orders).
    int active_orders = forge_order.empty() ? 0 : 1;
    if (active_orders >= max_orders) {
        response.error = "This building already has an active forge order.";
        return response;
    }
    if (!required_node.empty() &&
        std::find(nodes.begin(), nodes.end(), required_node) == nodes.end()) {
        response.error = "The recipe for this item has not been learned.";
        return response;
    }

    // Armory headroom for the finished item (player-initiated add is capped).
    const int armory_capacity = config_cache.getRetinueConfig().value("armory_capacity", 100);
    const int item_slots = item_cfg.value("armory_slots", 1);
    int used_slots = 0;
    {
        std::vector<std::string> armory_items;
        db << "SELECT item_id FROM fiefdom_armory WHERE fiefdom_id = ?;" << fiefdom_id
           >> [&](std::string it) { armory_items.push_back(it); };
        for (const auto& it : armory_items) {
            if (items_root.contains(it)) used_slots += items_root[it].value("armory_slots", 1);
        }
    }
    if (used_slots + item_slots > armory_capacity) {
        response.error = "Your armory cannot store another forged item.";
        return response;
    }

    // Pay forge materials (fungible money, auto-import honoring import toggles).
    const json materials = craft.value("materials", json::object());
    if (!materials.empty()) {
        if (!GameLogic::Validation::hasEnoughResources(config_cache, fiefdom_id, materials)) {
            response.error = "You cannot afford the forge materials.";
            return response;
        }
        GameLogic::ActionResult deduct_result;
        GameLogic::Validation::deductResources(config_cache, fiefdom_id, materials, deduct_result);
    }

    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const double duration_hours = craft.value("duration_hours", 1.0);
    json order = {{"item_id", item_id}, {"start_ts", now}, {"duration_hours", duration_hours}};
    db << "UPDATE fiefdom_buildings SET forge_order = ? WHERE id = ?;"
       << order.dump() << building_id;
    response.data["forge_order"] = order;
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleGetRetinueGear(GameConfigCache& config_cache, const json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) { response.needs_auth = true; return response; }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const int fiefdom_id = fiefdom_id_for_character(character_id);
    auto& db = Database::getInstance().gameDB();
    const auto& items_root = config_cache.getEquipmentConfig().value("items", json::object());
    const double sell_discount = config_cache.getRetinueConfig().value("sell_discount", 0.25);

    json armory = json::array();
    if (fiefdom_id > 0) {
        db << "SELECT id, item_id, member_id, created_at FROM fiefdom_armory "
              "WHERE fiefdom_id = ? ORDER BY id ASC;"
           << fiefdom_id
           >> [&](int64_t aid, std::string item_id, int64_t member_id, int64_t created_at) {
               json a = json::object();
               a["id"] = aid;
               a["item_id"] = item_id;
               a["member_id"] = member_id;
               a["created_at"] = created_at;
               if (items_root.contains(item_id)) {
                   a["item"] = items_root[item_id];
                   a["sell_value"] = items_root[item_id].value("base_value", 0.0) * sell_discount;
               }
               armory.push_back(std::move(a));
           };
    }
    json storage = json::array();
    if (fiefdom_id > 0) {
        db << "SELECT item_id, count FROM fiefdom_storage WHERE fiefdom_id = ? ORDER BY item_id ASC;"
           << fiefdom_id
           >> [&](std::string item_id, int count) {
               json s = json::object();
               s["item_id"] = item_id;
               s["count"] = count;
               if (config_cache.getItemsConfig().value("items", json::object()).contains(item_id)) {
                   s["item"] = config_cache.getItemsConfig()["items"][item_id];
               }
               storage.push_back(std::move(s));
           };
    }
    response.data["armory"] = armory;
    response.data["storage"] = storage;
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleEquipGear(GameConfigCache& config_cache, const json& body,
                            const std::optional<std::string>& username,
                            const ClientInfo& client,
                            const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) { response.needs_auth = true; return response; }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const int fiefdom_id = fiefdom_id_for_character(character_id);
    if (fiefdom_id == 0) { response.error = "You need a manor."; return response; }
    const int64_t armory_id = body.value("armory_id", 0LL);
    const int64_t member_id = body.value("member_id", 0LL);
    if (armory_id == 0 || member_id == 0) {
        response.error = "armory_id and member_id are required.";
        return response;
    }
    auto& db = Database::getInstance().gameDB();

    // The armory item must be yours and un-equipped.
    std::string item_id;
    int64_t current_member = 0;
    db << "SELECT item_id, member_id FROM fiefdom_armory WHERE id = ? AND fiefdom_id = ?;"
       << armory_id << fiefdom_id
       >> [&](std::string it, int64_t m) { item_id = it; current_member = m; };
    if (item_id.empty()) { response.error = "Armory item not found."; return response; }
    if (current_member != 0) { response.error = "That item is already equipped."; return response; }

    // The member must be yours.
    int owned = 0;
    db << "SELECT COUNT(*) FROM retinue_members WHERE id = ? AND character_id = ?;"
       << member_id << character_id >> [&](int c) { owned = c; };
    if (owned == 0) { response.error = "Member does not belong to you."; return response; }

    int mlvl = 0;
    std::string mclass;
    db << "SELECT level, unit_class FROM retinue_members WHERE id = ?;" << member_id
       >> [&](int l, std::string cls) { mlvl = l; mclass = cls; };

    const auto& items_root = config_cache.getEquipmentConfig().value("items", json::object());
    if (!items_root.contains(item_id)) { response.error = "Unknown item."; return response; }
    const json& item_cfg = items_root[item_id];
    if (item_cfg.value("requires_level", 1) > mlvl) {
        response.error = "This item requires a higher member level.";
        return response;
    }
    const auto allowed = item_cfg.value("allowed_classes", json::array());
    if (!allowed.empty()) {
        bool ok = false;
        for (const auto& cls : allowed) if (cls.get<std::string>() == mclass) ok = true;
        if (!ok) { response.error = "This item cannot be used by that unit class."; return response; }
    }
    const std::string slot = item_cfg.value("slot", "weapon");

    // Read the member's equipment map FIRST so a displaced item (already in this
    // slot) can be returned to the armory before the new one takes its place.
    std::string equip_json;
    db << "SELECT equipment FROM retinue_members WHERE id = ?;" << member_id
       >> [&](std::string e) { equip_json = std::move(e); };
    json equip;
    try { equip = json::parse(equip_json); } catch (...) { equip = json::object(); }
    if (!equip.is_object()) equip = json::object();

    // If the slot is occupied, the old item stops being equipped: clear its
    // armory member_id so it can be re-equipped or sold again.
    if (equip.contains(slot) && equip[slot].is_string()) {
        const std::string displaced = equip[slot].get<std::string>();
        db << "UPDATE fiefdom_armory SET member_id = NULL "
              "WHERE fiefdom_id = ? AND item_id = ? AND member_id = ?;"
           << fiefdom_id << displaced << member_id;
    }

    db << "UPDATE fiefdom_armory SET member_id = ? WHERE id = ?;" << member_id << armory_id;
    equip[slot] = item_id;
    db << "UPDATE retinue_members SET equipment = ? WHERE id = ?;" << equip.dump() << member_id;

    response.data["member_id"] = member_id;
    response.data["slot"] = slot;
    response.data["item_id"] = item_id;
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleDeassignGear(GameConfigCache& config_cache, const json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) { response.needs_auth = true; return response; }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const int fiefdom_id = fiefdom_id_for_character(character_id);
    if (fiefdom_id == 0) { response.error = "You need a manor."; return response; }
    const int64_t member_id = body.value("member_id", 0LL);
    const std::string slot = body.value("slot", "");
    if (member_id == 0 || slot.empty()) {
        response.error = "member_id and slot are required.";
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    int owned = 0;
    db << "SELECT COUNT(*) FROM retinue_members WHERE id = ? AND character_id = ?;"
       << member_id << character_id >> [&](int c) { owned = c; };
    if (owned == 0) { response.error = "Member does not belong to you."; return response; }

    std::string equip_json;
    db << "SELECT equipment FROM retinue_members WHERE id = ?;" << member_id
       >> [&](std::string e) { equip_json = std::move(e); };
    json equip;
    try { equip = json::parse(equip_json); } catch (...) { equip = json::object(); }
    if (!equip.is_object() || !equip.contains(slot)) {
        response.error = "That slot is empty.";
        return response;
    }
    const std::string item_id = equip[slot].get<std::string>();
    equip.erase(slot);

    // Hand the item back to the armory.
    db << "UPDATE fiefdom_armory SET member_id = NULL WHERE fiefdom_id = ? AND item_id = ? AND member_id = ?;"
       << fiefdom_id << item_id << member_id;
    db << "UPDATE retinue_members SET equipment = ? WHERE id = ?;" << equip.dump() << member_id;

    response.data["member_id"] = member_id;
    response.data["slot"] = slot;
    response.data["item_id"] = item_id;
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleSellItem(GameConfigCache& config_cache, const json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) { response.needs_auth = true; return response; }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const int fiefdom_id = fiefdom_id_for_character(character_id);
    if (fiefdom_id == 0) { response.error = "You need a manor."; return response; }
    const std::string kind = body.value("kind", "");   // "armory" | "storage"
    const int64_t armory_id = body.value("armory_id", 0LL);
    const std::string item_id = body.value("item_id", "");
    if (kind != "armory" && kind != "storage") {
        response.error = "kind must be 'armory' or 'storage'.";
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    const double sell_discount = config_cache.getRetinueConfig().value("sell_discount", 0.25);
    const auto& eq_items = config_cache.getEquipmentConfig().value("items", json::object());
    const auto& gen_items = config_cache.getItemsConfig().value("items", json::object());

    double value_gold = 0.0;
    if (kind == "armory") {
        if (armory_id == 0) { response.error = "armory_id required."; return response; }
        std::string it;
        int64_t holder = 0;
        db << "SELECT item_id, member_id FROM fiefdom_armory WHERE id = ? AND fiefdom_id = ?;"
           << armory_id << fiefdom_id
           >> [&](std::string i, int64_t m) { it = i; holder = m; };
        if (it.empty()) { response.error = "Armory item not found."; return response; }
        if (holder != 0) { response.error = "Sell items must be unequipped first."; return response; }
        if (eq_items.contains(it)) value_gold = eq_items[it].value("base_value", 0.0) * sell_discount;
        db << "DELETE FROM fiefdom_armory WHERE id = ?;" << armory_id;
    } else {
        if (item_id.empty()) { response.error = "item_id required."; return response; }
        if (gen_items.contains(item_id)) value_gold = gen_items[item_id].value("base_value", 0.0) * sell_discount;
        db << "UPDATE fiefdom_storage SET count = count - 1 WHERE fiefdom_id = ? AND item_id = ?;"
           << fiefdom_id << item_id;
        db << "DELETE FROM fiefdom_storage WHERE fiefdom_id = ? AND item_id = ? AND count <= 0;"
           << fiefdom_id << item_id;
    }
    if (value_gold > 0.0) {
        db << "UPDATE fiefdoms SET gold = gold + ? WHERE id = ?;" << value_gold << fiefdom_id;
    }
    response.data["gold"] = value_gold;
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleBuyGearCity(GameConfigCache& config_cache, const json& body,
                              const std::optional<std::string>& username,
                              const ClientInfo& client,
                              const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) { response.needs_auth = true; return response; }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const int fiefdom_id = fiefdom_id_for_character(character_id);
    if (fiefdom_id == 0) { response.error = "You need a manor."; return response; }
    const std::string item_id = body.value("item_id", "");
    if (item_id.empty()) { response.error = "item_id is required."; return response; }

    const auto& items_root = config_cache.getEquipmentConfig().value("items", json::object());
    if (!items_root.contains(item_id)) { response.error = "Unknown equipment item."; return response; }
    const json& item_cfg = items_root[item_id];
    const double city_price = item_cfg.value("city_price", 0.0);
    if (city_price <= 0.0) { response.error = "This item is not sold in the city."; return response; }

    // Armory headroom (a city purchase is a player-initiated add: capped).
    const int armory_capacity = config_cache.getRetinueConfig().value("armory_capacity", 100);
    const int item_slots = item_cfg.value("armory_slots", 1);
    int used_slots = 0;
    auto& db = Database::getInstance().gameDB();
    {
        std::vector<std::string> armory_items;
        db << "SELECT item_id FROM fiefdom_armory WHERE fiefdom_id = ?;" << fiefdom_id
           >> [&](std::string it) { armory_items.push_back(it); };
        for (const auto& it : armory_items) {
            if (items_root.contains(it)) used_slots += items_root[it].value("armory_slots", 1);
        }
    }
    if (used_slots + item_slots > armory_capacity) {
        response.error = "Your armory cannot store another purchased item.";
        return response;
    }

    // Pay the city markup (fungible money path). Extremely expensive by design —
    // a diagetic bypass for lords who skip their own blacksmith.
    const json cost = {{"gold", city_price}};
    if (!GameLogic::Validation::hasEnoughResources(config_cache, fiefdom_id, cost)) {
        response.error = "You cannot afford this item in the city.";
        return response;
    }
    GameLogic::ActionResult deduct_result;
    GameLogic::Validation::deductResources(config_cache, fiefdom_id, cost, deduct_result);

    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    db << "INSERT INTO fiefdom_armory (fiefdom_id, item_id, member_id, created_at) "
          "VALUES (?, ?, NULL, ?);"
       << fiefdom_id << item_id << now;
    response.data["item_id"] = item_id;
    response.data["armory_id"] = db.last_insert_rowid();
    response.data["gold"] = city_price;
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleHireTeacher(GameConfigCache& config_cache, const json& body,
                              const std::optional<std::string>& username,
                              const ClientInfo& client,
                              const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) { response.needs_auth = true; return response; }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const int fiefdom_id = fiefdom_id_for_character(character_id);
    if (fiefdom_id == 0) { response.error = "You need a manor."; return response; }
    const int64_t building_id = body.value("building_id", 0LL);
    if (building_id == 0) { response.error = "building_id is required."; return response; }
    auto& db = Database::getInstance().gameDB();
    if (!building_belongs_to_fiefdom(building_id, fiefdom_id)) {
        response.error = "Building does not belong to your fiefdom.";
        return response;
    }
    std::string bname;
    db << "SELECT name FROM fiefdom_buildings WHERE id = ?;" << building_id
       >> [&](std::string n) { bname = std::move(n); };
    if (!building_type_config(config_cache, bname).contains("tech_trees")) {
        response.error = "This building has no technology tree.";
        return response;
    }
    double xp = 0;
    std::vector<std::string> nodes;
    std::string forge_order, training;
    load_building_tech(building_id, xp, nodes, forge_order, training);
    if (!training.empty()) {
        response.error = "This building is already training.";
        return response;
    }

    const auto train = config_cache.getRetinueConfig().value("training", json::object());
    const double cost_gold = train.value("teacher_gold_cost", 20.0);
    const double grant_xp = train.value("teacher_xp_grant", 50.0);
    const double hours = train.value("teacher_duration_hours", 4.0);

    const json cost = {{"gold", cost_gold}};
    if (cost_gold > 0.0 &&
        !GameLogic::Validation::hasEnoughResources(config_cache, fiefdom_id, cost)) {
        response.error = "You cannot afford a teacher.";
        return response;
    }
    if (cost_gold > 0.0) {
        GameLogic::ActionResult deduct_result;
        GameLogic::Validation::deductResources(config_cache, fiefdom_id, cost, deduct_result);
    }

    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    json tr = {{"grant_xp", grant_xp}, {"start_ts", now}, {"duration_hours", hours}};
    db << "UPDATE fiefdom_buildings SET training = ? WHERE id = ?;" << tr.dump() << building_id;
    response.data["training"] = tr;
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse handleApplyBookItem(GameConfigCache& config_cache, const json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token)
{
    ApiResponse response;
    if (!username) { response.needs_auth = true; return response; }
    int64_t character_id = 0;
    std::string display_name;
    if (!verify_character_ownership(body, username, character_id, display_name)) {
        response.error = "Character does not belong to this user";
        return response;
    }
    const int fiefdom_id = fiefdom_id_for_character(character_id);
    if (fiefdom_id == 0) { response.error = "You need a manor."; return response; }
    const int64_t building_id = body.value("building_id", 0LL);
    const std::string item_id = body.value("item_id", "");
    if (building_id == 0 || item_id.empty()) {
        response.error = "building_id and item_id are required.";
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    if (!building_belongs_to_fiefdom(building_id, fiefdom_id)) {
        response.error = "Building does not belong to your fiefdom.";
        return response;
    }
    std::string bname;
    db << "SELECT name FROM fiefdom_buildings WHERE id = ?;" << building_id
       >> [&](std::string n) { bname = std::move(n); };
    if (!building_type_config(config_cache, bname).contains("tech_trees")) {
        response.error = "This building has no technology tree.";
        return response;
    }

    const auto& gen_items = config_cache.getItemsConfig().value("items", json::object());
    if (!gen_items.contains(item_id)) { response.error = "Unknown item."; return response; }
    const json& icfg = gen_items[item_id];
    const double grant_xp = icfg.value("xp_grant", 0.0);
    const double hours = icfg.value("training_duration_hours", 0.0);
    if (grant_xp <= 0.0 || hours <= 0.0) {
        response.error = "That item is not a training item.";
        return response;
    }

    int have = 0;
    db << "SELECT count FROM fiefdom_storage WHERE fiefdom_id = ? AND item_id = ?;"
       << fiefdom_id << item_id >> [&](int c) { have = c; };
    if (have < 1) { response.error = "You do not have that item."; return response; }

    double xp = 0;
    std::vector<std::string> nodes;
    std::string forge_order, training;
    load_building_tech(building_id, xp, nodes, forge_order, training);
    if (!training.empty()) {
        response.error = "This building is already training.";
        return response;
    }

    db << "UPDATE fiefdom_storage SET count = count - 1 WHERE fiefdom_id = ? AND item_id = ?;"
       << fiefdom_id << item_id;
    db << "DELETE FROM fiefdom_storage WHERE fiefdom_id = ? AND item_id = ? AND count <= 0;"
       << fiefdom_id << item_id;

    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    json tr = {{"grant_xp", grant_xp}, {"start_ts", now}, {"duration_hours", hours}};
    db << "UPDATE fiefdom_buildings SET training = ? WHERE id = ?;" << tr.dump() << building_id;
    response.data["training"] = tr;
    response.data["item_id"] = item_id;
    if (new_token) response.data["token"] = *new_token;
    return response;
}

void handleApiRequest(GameConfigCache& config_cache, auto* res, auto* req) {
    // Extract req data NOW while req is still valid (uWS only guarantees req
    // lifetime during the synchronous handler call — the onData callback fires
    // later when the POST body arrives, at which point req may be freed).
    std::string endpoint = extractEndpointName(req->getUrl());
    ClientInfo client = parseClientHeaders(req);
    std::string ip_address = std::string(client.real_ip);

    // uWS requires an abort handler when using onData; without it, the
    // process aborts with "Returning from a request handler without
    // responding or attaching an abort handler is forbidden!"
    res->onAborted([]() {
        // No cleanup needed — the res object is already invalid.
    });

    std::string buffer;
    res->onData([res, endpoint = std::move(endpoint), client = std::move(client),
                 ip_address = std::move(ip_address), buffer = std::move(buffer),
                 &config_cache]
                (std::string_view data, bool isLast) mutable {
        buffer += data;
        if (!isLast) return;

        try {
            json body = json::parse(buffer, nullptr, true, true, true);

            json auth_object;
            if (body.contains("auth") && body["auth"].is_object()) {
                auth_object = body["auth"];
            }

            // Handle public endpoints before authentication check
            if (PUBLIC_ENDPOINTS.count(endpoint)) {
                ApiResponse response;
                if (endpoint == "createAccount") {
                    response = handleCreateAccount(config_cache, body, std::nullopt, client, std::nullopt);
                } else if (endpoint == "getTexts") {
                    response = handleGetTexts(config_cache, body, std::nullopt, client, std::nullopt);
                } else if (endpoint == "verifyAgeOverride") {
                    response = handleVerifyAgeOverride(config_cache, body, std::nullopt, client, std::nullopt);
                } else if (endpoint == "getUITextures") {
                    response = handleGetUITextures(config_cache, body, std::nullopt, client, std::nullopt);
                }
                sendJsonResponse(res, response);
                return;
            }

            // Authenticate other endpoints
            AuthResult auth_result = handleAuth(endpoint, auth_object, ip_address);

            if (!auth_result.isOk()) {
                std::cout << "[DEBUG] Auth check failed: endpoint=" << endpoint
                          << " needs_auth=" << auth_result.needs_auth
                          << " auth_failed=" << auth_result.auth_failed << std::endl;
                ApiResponse response;
                response.needs_auth = auth_result.needs_auth;
                response.auth_failed = auth_result.auth_failed;
                if (auth_result.error) response.error = *auth_result.error;
                sendJsonResponse(res, response);
                return;
            }

            // Authenticated endpoints
            auto handlers = getEndpointHandlers(config_cache);
            if (handlers.count(endpoint)) {
                ApiResponse response = handlers[endpoint](config_cache, body, auth_result.username, client, auth_result.new_token);
                sendJsonResponse(res, response);
            } else {
                ApiResponse error_response;
                error_response.error = "Unknown endpoint: " + endpoint;
                sendJsonResponse(res, error_response);
            }

            increment_request_count();

        } catch (const json::exception& e) {
            log_error(endpoint, std::string("JSON parse error: type=") + typeid(e).name() + " what=" + e.what());
            ApiResponse error_response;
            error_response.error = std::string("Invalid JSON: ") + e.what();
            sendJsonResponse(res, error_response);
        } catch (const std::exception& e) {
            log_error(endpoint, std::string("type=") + typeid(e).name() + " what=" + e.what());
            ApiResponse error_response;
            error_response.error = e.what();
            sendJsonResponse(res, error_response);
        } catch (...) {
            log_error(endpoint, "Unknown exception (not a std::exception)");
            // Catch-all: uWS aborts if ANY exception escapes a handler.
            // This prevents std::terminate() from unexpected errors.
            ApiResponse error_response;
            error_response.error = "Internal server error";
            sendJsonResponse(res, error_response);
        }
    });
}

// Runs a tick-exact combat replay recorded to `combat_replays/` (M10/D27):
// constructs the match from the recorded config, injects the command/ready log
// at its recorded ticks, runs the battle synchronously, and compares the final
// state hash. Returns 0 on match (or if the record carries no expected hash),
// 2 on mismatch, 1 on load error.
int run_combat_replay(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::cerr << "combat replay: cannot open " << file << std::endl;
        return 1;
    }
    json rec;
    try {
        in >> rec;
    } catch (const std::exception& e) {
        std::cerr << "combat replay: bad JSON (" << e.what() << ")" << std::endl;
        return 1;
    }

    combat::combat_match_config config;
    config.seed = rec.value("seed", 0u);
    config.ruleset = rec.value("ruleset", json::object());
    config.map_metadata = rec.value("map", json::object());
    config.player_combatants = rec.value("player_combatants", json::object());
    config.enemy_combatants = rec.value("enemy_combatants", json::object());
    config.structures = rec.value("structures", json::object());
    config.formations = rec.value("formations", json::object());
    config.profiles = rec.value("profiles", json::object());
    config.rewards = rec.value("rewards", json::object());
    config.opportunity = rec.value("opportunity", json::object());
    for (const auto& w : rec.value("warband", json::array())) {
        combat::warband_spawn_entry e;
        e.unit_class = w.value("unit_class", "");
        e.level = w.value("level", 1);
        e.count = w.value("count", 0);
        e.group = w.value("group", "");
        e.tactic = w.value("tactic", "");
        e.aggro_model = w.value("aggro_model", "");
        config.warband.push_back(std::move(e));
    }

    auto match = std::make_shared<combat::combat_match>(
        rec.value("match_id", std::string("replay")), "REPLAY",
        rec.value("mode", std::string("pve")), std::move(config));

    for (const auto& p : rec.value("players", json::array())) {
        std::vector<combat::retinue_member_snapshot> retinue;
        for (const auto& m : p.value("retinue", json::array())) {
            combat::retinue_member_snapshot s;
            s.member_id = m.value("member_id", static_cast<int64_t>(0));
            s.display_name = m.value("display_name", "");
            s.unit_class = m.value("unit_class", "");
            s.level = m.value("level", 1);
            s.is_knight = m.value("is_knight", false);
            s.health = m.value("health", 100.0);
            retinue.push_back(std::move(s));
        }
        if (!match->add_player(p.value("player_id", static_cast<int64_t>(0)),
                               p.value("display_name", ""), retinue)) {
            std::cerr << "combat replay: could not add recorded player" << std::endl;
            return 1;
        }
    }

    std::vector<json> commands;
    for (const auto& c : rec.value("commands", json::array())) commands.push_back(c);
    std::vector<json> ready;
    for (const auto& r : rec.value("ready", json::array())) ready.push_back(r);
    match->set_scripted_input(std::move(commands), std::move(ready));

    match->run_battle([](const combat::combat_message&) {},
                      [](const combat::combat_match&) {});
    const uint64_t actual = match->state_hash();
    const uint64_t expected = rec.value("state_hash", static_cast<uint64_t>(0));
    std::cout << "REPLAY winner=" << match->winner() << " state_hash=" << actual
              << " expected=" << expected << std::endl;
    if (expected != 0 && actual != expected) {
        std::cerr << "combat replay: state hash mismatch" << std::endl;
        return 2;
    }
    return 0;
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --content PATH             Asset root holding config/, images/, text/ (default: .)" << std::endl;
    std::cout << "  --db-dir PATH              Database directory (default: current)" << std::endl;
    std::cout << "  --text-dir PATH            Text directory (default: <content>/text)" << std::endl;
    std::cout << "  --port PORT                Port to bind (default: 2290)" << std::endl;
    std::cout << "  --combat-sim-threads N     Realtime combat simulation worker threads (default: CPU count, capped 64)" << std::endl;
    std::cout << "  --combat-seed N            Fixed combat RNG seed (reproducible matches; default: random)" << std::endl;
    std::cout << "  --combat-replay-dir DIR     Write tick-exact match replays to DIR (default: off)" << std::endl;
    std::cout << "  --combat-replay FILE        Re-run a recorded match, print its state hash, exit" << std::endl;
    std::cout << "  --shutdown-after-ms N       Gracefully stop after N ms (0 = wait for a signal)" << std::endl;
    std::cout << "  --init-db                  Initialize all database tables and indexes, then exit" << std::endl;
    std::cout << "  --create-tables            Create all database tables, then exit" << std::endl;
    std::cout << "  --ensure-indexes           Ensure all indexes exist, then exit" << std::endl;
    std::cout << "  --test-num-requests N      Exit after N requests (agent test mode)" << std::endl;
    std::cout << "  --test-timeout-seconds M    Exit after M seconds (agent test mode)" << std::endl;
    std::cout << "  --verbose                  Enable verbose logging" << std::endl;
    std::cout << "  --quiet                    Minimal logging" << std::endl;
    std::cout << "  -h, --help                 Show this help message" << std::endl;
}

// Serves a file from `root` (relative to the server working directory) for a
// URL starting with `prefix`. Guards against path traversal and maps the image
// extensions we accept (png/jpg/jpeg/webp/avif) to their MIME types.
template <typename Res>
static void serve_image_subtree(Res* res, const std::string& url,
                                const std::string& prefix, const std::string& root) {
    std::string relative = url.substr(prefix.length());
    if (relative.empty() || relative.find("..") != std::string::npos) {
        res->writeStatus("404 Not Found")->end("Not found");
        return;
    }
    std::ifstream file(root + relative, std::ios::binary);
    if (!file) {
        res->writeStatus("404 Not Found")->end("Not found");
        return;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    const std::string lower = lower_ascii(relative);
    auto ends_with = [&](const char* suffix) {
        const size_t n = std::strlen(suffix);
        return lower.size() >= n && lower.compare(lower.size() - n, n, suffix) == 0;
    };
    if (ends_with(".png")) res->writeHeader("Content-Type", "image/png");
    else if (ends_with(".jpg") || ends_with(".jpeg")) res->writeHeader("Content-Type", "image/jpeg");
    else if (ends_with(".webp")) res->writeHeader("Content-Type", "image/webp");
    else if (ends_with(".avif")) res->writeHeader("Content-Type", "image/avif");
    res->end(content);
}

int main(int argc, char* argv[]) {
    int port = 2290;
    bool verbose = false;
    bool quiet = false;
    bool init_db_mode = false;
    bool create_tables_mode = false;
    bool ensure_indexes_mode = false;
    int combat_sim_threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 64);
    std::string combat_replay_dir;
    std::string combat_replay_file;
    int shutdown_after_ms = 0;   // 0 = only a signal triggers shutdown

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strncmp(argv[i], "--content=", 10) == 0) {
            g_content_dir = argv[i] + 10;
        } else if (strcmp(argv[i], "--content") == 0) {
            if (i + 1 < argc) {
                g_content_dir = argv[++i];
            }
        } else if (strcmp(argv[i], "--db-dir") == 0) {
            if (i + 1 < argc) {
                g_db_dir = argv[++i];
            }
        } else if (strcmp(argv[i], "--text-dir") == 0) {
            if (i + 1 < argc) {
                g_text_dir = argv[++i];
            }
        } else if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                port = std::atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--combat-sim-threads") == 0) {
            if (i + 1 < argc) {
                combat_sim_threads = std::max(1, std::atoi(argv[++i]));
            }
        } else if (strcmp(argv[i], "--combat-seed") == 0) {
            if (i + 1 < argc) {
                g_combat_seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            }
        } else if (strcmp(argv[i], "--combat-replay-dir") == 0) {
            if (i + 1 < argc) {
                combat_replay_dir = argv[++i];
            }
        } else if (strcmp(argv[i], "--combat-replay") == 0) {
            if (i + 1 < argc) {
                combat_replay_file = argv[++i];
            }
        } else if (strcmp(argv[i], "--shutdown-after-ms") == 0) {
            if (i + 1 < argc) {
                shutdown_after_ms = std::max(0, std::atoi(argv[++i]));
            }
        } else if (strcmp(argv[i], "--init-db") == 0) {
            init_db_mode = true;
        } else if (strcmp(argv[i], "--create-tables") == 0) {
            create_tables_mode = true;
        } else if (strcmp(argv[i], "--ensure-indexes") == 0) {
            ensure_indexes_mode = true;
        } else if (strcmp(argv[i], "--test-num-requests") == 0) {
            if (i + 1 < argc) {
                g_test_num_requests = std::atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--test-timeout-seconds") == 0) {
            if (i + 1 < argc) {
                g_test_timeout_seconds = std::atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        }
    }

    // Derive the asset subdirectories from the content root. `--text-dir`
    // overrides the text location; `--content` defaults to the working
    // directory, preserving the historical layout (server run from game/).
    if (g_content_dir.empty()) {
        g_content_dir = ".";
    }
    if (g_content_dir.size() > 1 && g_content_dir.back() == '/') {
        g_content_dir.pop_back();
    }
    g_config_dir = g_content_dir + "/config";
    g_images_dir = g_content_dir + "/images";
    if (g_text_dir.empty()) {
        g_text_dir = g_content_dir + "/text";
    }

    if (!quiet) {
        std::cout << "Ravenest Server initializing..." << std::endl;
        std::cout << "Content directory: " << g_content_dir << std::endl;
        std::cout << "Config directory: " << g_config_dir << std::endl;
        std::cout << "Images directory: " << g_images_dir << std::endl;
        std::cout << "Database directory: " << g_db_dir << std::endl;
        std::cout << "Port: " << port << std::endl;
        if (g_test_num_requests > 0 || g_test_timeout_seconds > 0) {
            std::cout << "Test mode: enabled" << std::endl;
        }
    }

    if (!SafeNameGenerator::getInstance().initialize(
            g_config_dir + "/safe_words_1.txt", g_config_dir + "/safe_words_2.txt")) {
        std::cerr << "Warning: Failed to load safe word lists" << std::endl;
    }

    MiniGames mini_games;
    g_mini_games = &mini_games;
    if (!mini_games.initialize(g_config_dir)) {
        std::cerr << "Warning: Failed to load game configuration files" << std::endl;
    }

    TowerDefenseMapCache::get_instance().initialize(g_config_dir + "/tower_defense/maps");
    CombatMapCache::get_instance().initialize(g_config_dir + "/combat/maps");

    if (!ImageCache::getInstance().initialize(g_images_dir)) {
        std::cerr << "Warning: Failed to initialize image cache" << std::endl;
    }

    if (!AvatarPool::getInstance().initialize(g_images_dir)) {
        std::cerr << "Warning: Failed to initialize avatar pool" << std::endl;
    }

    static TextManager text_manager(g_text_dir);
    g_text_manager = &text_manager;
    if (!quiet) {
        std::cout << "Text directory: " << g_text_dir << std::endl;
    }



    std::string game_db_path = g_db_dir + "/game.db";
    std::string chat_db_path = g_db_dir + "/chat.db";

    if (!quiet) {
        std::cout << "Opening databases..." << std::endl;
    }

    try {
        Database::getInstance().init(game_db_path, chat_db_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize databases: " << e.what() << std::endl;
        return 1;
    }

    if (init_db_mode || create_tables_mode || ensure_indexes_mode) {
        auto& game_db = Database::getInstance().gameDB();
        auto& chat_db = Database::getInstance().chatDB();

        if (init_db_mode) {
            if (!quiet) {
                std::cout << "Initializing all database tables and indexes..." << std::endl;
            }
            try {
                initializeAllDatabases(game_db, chat_db);
                if (!quiet) {
                    std::cout << "Database initialization complete." << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Database initialization failed: " << e.what() << std::endl;
                return 1;
            }
        } else if (create_tables_mode) {
            if (!quiet) {
                std::cout << "Creating all database tables..." << std::endl;
            }
            try {
                initializeGameDB(game_db);
                initializeChatDB(chat_db);
                if (!quiet) {
                    std::cout << "Tables created." << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Table creation failed: " << e.what() << std::endl;
                return 1;
            }
        } else if (ensure_indexes_mode) {
            if (!quiet) {
                std::cout << "Ensuring all indexes exist..." << std::endl;
            }
            try {
                ensureGameDBIndexes(game_db);
                ensureChatDBIndexes(chat_db);
                if (!quiet) {
                    std::cout << "Indexes ensured." << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Index creation failed: " << e.what() << std::endl;
                return 1;
            }
        }
        return 0;
    }

    if (!quiet) {
        std::cout << "Initializing database schemas..." << std::endl;
    }
    try {
        initializeAllDatabases(Database::getInstance().gameDB(), Database::getInstance().chatDB());
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize database schemas: " << e.what() << std::endl;
        return 1;
    }

    // Chat: bind the config cache and materialize barony/dukedom membership.
    chat::ChatService::instance().initialize(&mini_games.config_cache());
    try {
        chat::ChatService::instance().reconcile_alliances();
    } catch (const std::exception& e) {
        std::cerr << "Warning: chat alliance reconcile failed: " << e.what() << std::endl;
    }

    // Register all manor action handlers (build, demolish, move, upgrade, walls,
    // train, research) into the registry used by /api/Build and friends.
    GameLogic::registerAllActionHandlers(GameLogic::ActionRegistry::getInstance());

    uWS::App app;

    check_test_limits();

    // ── Realtime combat: in-RAM match manager + WebSocket route ────────────
    // uWS is single-threaded; the ONLY thread-safe entry is Loop::defer. Match
    // worker threads therefore never call app.publish directly — they post to
    // the loop, which publishes on this thread. The App's address lives in a
    // shared atomic slot so a late post observes nullptr instead of a freed App.
    // SQLite writes are likewise deferred to this loop thread so the Database
    // singleton stays single-owner. See docs/combat_protocol.md.
    uWS::Loop* main_loop = uWS::Loop::get();
    auto app_slot = std::make_shared<std::atomic<uWS::App*>>(&app);
    g_combat_manager.initialize(
        std::make_shared<combat::json_codec>(),
        combat_sim_threads,
        [main_loop, app_slot](const std::string& topic, const std::string& message) {
            main_loop->defer([app_slot, topic, message]() {
                if (uWS::App* a = app_slot->load(std::memory_order_acquire)) {
                    a->publish(topic, message, uWS::OpCode::TEXT);
                }
            });
        },
        [main_loop](std::function<void()> fn) {
            main_loop->defer(std::move(fn));
        }
    );

    // Determinism replay (M10): recordings live here; `--combat-replay` re-runs
    // one synchronously and exits before the server starts listening.
    g_combat_manager.set_replay_dir(combat_replay_dir);
    if (!combat_replay_file.empty()) {
        return run_combat_replay(combat_replay_file);
    }

    auto send_ws_error = [](auto* ws, const std::string& match_id, const std::string& message) {
        combat::combat_message msg;
        msg.type = "error";
        msg.match_id = match_id;
        msg.json_payload = {{"error", message}};
        ws->send(g_combat_manager.codec()->serialize_message(msg), uWS::OpCode::TEXT);
    };

    // First message must be auth; on success the socket is subscribed to the
    // match topics and receives welcome + a full state snapshot.
    auto handle_ws_auth = [&send_ws_error, &mini_games](auto* ws, ws_combat_user_data* ud, std::string_view message) {
        json j;
        try {
            j = json::parse(message, nullptr, true, true);
        } catch (...) {
            send_ws_error(ws, "", "malformed auth message");
            ws->close();
            return;
        }
        if (j.value("type", "") != "auth") {
            send_ws_error(ws, "", "first message must be auth");
            ws->close();
            return;
        }
        const json payload = j.value("payload", json::object());
        const std::string username = payload.value("username", "");
        const std::string token = payload.value("token", "");
        const int64_t character_id = payload.value("character_id", static_cast<int64_t>(0));
        const std::string match_id = payload.value("match_id", "");
        if (username.empty() || token.empty() || character_id == 0 || match_id.empty()) {
            send_ws_error(ws, match_id, "auth requires username, token, character_id, match_id");
            ws->close();
            return;
        }
        if (!AuthManager::getInstance().authenticateWithToken(username, token)) {
            send_ws_error(ws, match_id, "authentication failed");
            ws->close();
            return;
        }
        auto& db = Database::getInstance().gameDB();
        int owner_id = 0;
        int user_id = 0;
        db << "SELECT user_id FROM characters WHERE id = ?;" << character_id >> [&](int uid) { owner_id = uid; };
        db << "SELECT id FROM users WHERE username = ?;" << username >> [&](int id) { user_id = id; };
        if (owner_id == 0 || owner_id != user_id) {
            send_ws_error(ws, match_id, "character does not belong to this user");
            ws->close();
            return;
        }
        auto match = g_combat_manager.get_match(match_id);
        if (!match || !match->contains_player(character_id)) {
            send_ws_error(ws, match_id, "not a member of this match");
            ws->close();
            return;
        }
        if (!g_combat_manager.connect_player(match_id, character_id)) {
            send_ws_error(ws, match_id, "player already connected");
            ws->close();
            return;
        }
        ud->authed = true;
        ud->username = username;
        ud->character_id = character_id;
        ud->match_id = match_id;
        const int team = match->player_team(character_id);
        ws->subscribe("match:" + match_id);
        ws->subscribe("match:" + match_id + ":team:" + std::to_string(team));
        ws->subscribe("match:" + match_id + ":player:" + std::to_string(character_id));

        combat::combat_message welcome;
        welcome.type = "welcome";
        welcome.match_id = match_id;
        welcome.json_payload["player_id"] = character_id;
        welcome.json_payload["team"] = team;
        welcome.json_payload["mode"] = match->mode();
        welcome.json_payload["match_code"] = match->code();
        welcome.json_payload["ruleset_id"] = match->ruleset_id();
        welcome.json_payload["ruleset"] = match->ruleset();
        welcome.json_payload["map"] = match->map_metadata();
        // Mission identity (the offering is fixed at create): shown in the
        // lobby/deploy/results so the player always knows what they are fighting.
        if (match->opportunity().is_object()) {
            welcome.json_payload["offering"] = match->opportunity();
        }
        json players = json::array();
        for (const auto& p : match->players_snapshot()) {
            players.push_back({{"player_id", p.player_id}, {"team", p.team},
                               {"display_name", p.display_name}, {"ready", p.ready},
                               {"connected", p.connected}});
        }
        welcome.json_payload["players"] = players;
        // Cosmetics ride the join-time snapshot only, never tick deltas (§15.3).
        // The server authenticates ownership; clients receive appearance only.
        GameConfigCache& cosmetics_cfg = mini_games.config_cache();
        const json loadout = cosmetics_db::get_loadout(db, character_id);
        const json unit_appearance = resolve_equipped_appearance(cosmetics_cfg, db, character_id,
                                                                 loadout, "unit");
        // The fielded (roster-filtered) members for this player, not the full
        // book — so the client's deploy view matches what will spawn.
        std::vector<combat::retinue_member_snapshot> fielded;
        for (const auto& p : match->players_snapshot()) {
            if (p.player_id == character_id) {
                fielded = p.retinue;
                break;
            }
        }
        json retinue = json::array();
        for (const auto& m : fielded) {
            retinue.push_back({{"member_id", m.member_id}, {"display_name", m.display_name},
                               {"unit_class", m.unit_class}, {"level", m.level},
                               {"is_knight", m.is_knight}, {"health", m.health},
                               {"fieldable", m.fieldable}, {"cosmetic", unit_appearance}});
        }
        welcome.json_payload["retinue"] = retinue;
        // Per-player cosmetic slots (one banner + the squad/knight/structure/
        // flourish appearances). The `unit` override above is per-member on the
        // wire so a future per-member map can vary it.
        welcome.json_payload["banner"] =
            resolve_equipped_appearance(cosmetics_cfg, db, character_id, loadout, "banner");
        json player_cosmetics = json::object();
        for (const char* slot : {"knight", "squad_uniform", "structure", "flourish"}) {
            json appearance = resolve_equipped_appearance(cosmetics_cfg, db, character_id, loadout, slot);
            if (!appearance.is_null()) player_cosmetics[slot] = appearance;
        }
        welcome.json_payload["cosmetics"] = player_cosmetics;
        ws->send(g_combat_manager.codec()->serialize_message(welcome), uWS::OpCode::TEXT);
        // Full state immediately so the client starts from a consistent snapshot.
        ws->send(g_combat_manager.codec()->serialize_message(match->build_full_state()),
                 uWS::OpCode::TEXT);
    };

    // Post-auth messages: chat/voice are relayed on the loop thread; game
    // commands go through the match's command queue.
    auto handle_ws_message = [&app, &send_ws_error](auto* ws, ws_combat_user_data* ud,
                                                    std::string_view message) {
        combat::combat_message msg;
        try {
            msg = g_combat_manager.codec()->deserialize_message(message);
        } catch (...) {
            send_ws_error(ws, ud->match_id, "malformed message");
            return;
        }
        auto match = g_combat_manager.get_match(ud->match_id);
        if (!match) {
            send_ws_error(ws, ud->match_id, "match not found");
            ws->close();
            return;
        }

        if (msg.type == "chat") {
            std::string text = msg.json_payload.value("text", "");
            if (text.empty() || text.size() > 500) {
                send_ws_error(ws, ud->match_id, "invalid chat message");
                return;
            }
            combat::combat_message out;
            out.type = "chat";
            out.match_id = ud->match_id;
            out.json_payload["from"] = ud->character_id;
            out.json_payload["from_name"] = match->player_display_name(ud->character_id);
            out.json_payload["team"] = match->player_team(ud->character_id);
            out.json_payload["text"] = text;
            out.json_payload["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            app.publish("match:" + ud->match_id + ":team:" + std::to_string(out.json_payload["team"].get<int>()),
                        g_combat_manager.codec()->serialize_message(out), uWS::OpCode::TEXT);
            return;
        }
        if (msg.type == "voice") {
            const int64_t to_player = msg.json_payload.value("to_player", static_cast<int64_t>(0));
            const std::string signal_type = msg.json_payload.value("signal_type", "");
            if (to_player <= 0 || to_player == ud->character_id || signal_type.empty()) {
                send_ws_error(ws, ud->match_id, "invalid voice message");
                return;
            }
            if (match->player_team(to_player) != match->player_team(ud->character_id)) {
                send_ws_error(ws, ud->match_id, "voice is only allowed within a team");
                return;
            }
            combat::combat_message out;
            out.type = "voice";
            out.match_id = ud->match_id;
            out.json_payload["from"] = ud->character_id;
            out.json_payload["from_name"] = match->player_display_name(ud->character_id);
            out.json_payload["signal_type"] = signal_type;
            out.json_payload["data"] = msg.json_payload.value("data", json::object());
            app.publish("match:" + ud->match_id + ":player:" + std::to_string(to_player),
                        g_combat_manager.codec()->serialize_message(out), uWS::OpCode::TEXT);
            return;
        }
        if (msg.type == "command") {
            const json& payload = msg.json_payload;
            const std::string cmd = payload.value("cmd", "");
            // Command vocabulary §9: move/attack (target or ground Lob)/stop/
            // hold/patrol/stance (+ reserved ability), plus the deploy-phase
            // placement command (§4.4).
            const bool is_deploy = (cmd == "deploy");
            if (!is_deploy && cmd != "construct" && cmd != "formation" &&
                cmd != "move" && cmd != "attack" && cmd != "stop" && cmd != "hold" &&
                cmd != "patrol" && cmd != "stance" && cmd != "ability") {
                send_ws_error(ws, ud->match_id, "unknown command: " + cmd);
                return;
            }
            if (is_deploy) {
                if (match->phase() != combat::match_phase::deploy) {
                    send_ws_error(ws, ud->match_id, "deploy is only valid during the deploy phase");
                    return;
                }
            } else if (match->phase() != combat::match_phase::countdown &&
                       match->phase() != combat::match_phase::battle) {
                send_ws_error(ws, ud->match_id, "match is not in battle");
                return;
            }
            combat::combat_command c;
            c.player_id = ud->character_id;
            c.type = cmd;
            c.payload = payload;
            match->enqueue_command(c);
            return;
        }
        if (msg.type == "roster") {
            // Roster select (§4.2): which fieldable members to bring (knight
            // always fielded). Allowed until the player's deploy placement.
            std::vector<int64_t> unit_ids;
            if (msg.json_payload.contains("unit_ids") && msg.json_payload["unit_ids"].is_array()) {
                for (const auto& id : msg.json_payload["unit_ids"]) {
                    if (id.is_number_integer()) unit_ids.push_back(id.get<int64_t>());
                }
            }
            match->set_player_roster(ud->character_id, unit_ids);
            return;
        }
        if (msg.type == "ready") {
            g_combat_manager.mark_ready(ud->match_id, ud->character_id, true);
            return;
        }
        if (msg.type == "request_state") {
            ws->send(g_combat_manager.codec()->serialize_message(match->build_full_state()),
                     uWS::OpCode::TEXT);
            return;
        }
        if (msg.type == "leave") {
            g_combat_manager.handle_disconnect(ud->match_id, ud->character_id);
            ud->authed = false;
            ws->close();
            return;
        }
        send_ws_error(ws, ud->match_id, "unknown message type: " + msg.type);
    };

    app.ws<ws_combat_user_data>("/ws/combat", {
        .maxPayloadLength = 64 * 1024,
        .idleTimeout = 600,
        .sendPingsAutomatically = true,
        .open = [](auto* ws) { g_open_combat_sockets.insert(ws); },
        .message = [&handle_ws_auth, &handle_ws_message](auto* ws, std::string_view message,
                                                         uWS::OpCode /*opCode*/) {
            auto* ud = ws->getUserData();
            if (!ud->authed) {
                handle_ws_auth(ws, ud, message);
            } else {
                handle_ws_message(ws, ud, message);
            }
        },
        .close = [](auto* ws, int /*code*/, std::string_view /*message*/) {
            g_open_combat_sockets.erase(ws);
            auto* ud = ws->getUserData();
            if (ud->authed) {
                g_combat_manager.handle_disconnect(ud->match_id, ud->character_id);
                ud->authed = false;
            }
        }
    });

    app.get("/images/ui/*", [](auto *res, auto *req) {
        serve_image_subtree(res, std::string(req->getUrl()), "/images/ui/", g_images_dir + "/ui/");
    });

    app.get("/images/tower_defense/*", [](auto *res, auto *req) {
        serve_image_subtree(res, std::string(req->getUrl()),
                            "/images/tower_defense/", g_images_dir + "/tower_defense/");
    });

    app.get("/images/weeding/*", [](auto *res, auto *req) {
        serve_image_subtree(res, std::string(req->getUrl()),
                            "/images/weeding/", g_images_dir + "/weeding/");
    });

    app.get("/images/manor/*", [](auto *res, auto *req) {
        serve_image_subtree(res, std::string(req->getUrl()),
                            "/images/manor/", g_images_dir + "/manor/");
    });

    // Optional combat art (rts_design.md D24/D71): /images/combatants/<class>/
    // {idle,attack,defend,die}/<n>.png. Nothing requires these files; the
    // client falls back to procedural drawing when a sprite 404s.
    app.get("/images/combatants/*", [](auto *res, auto *req) {
        serve_image_subtree(res, std::string(req->getUrl()),
                            "/images/combatants/", g_images_dir + "/combatants/");
    });

    // Retinue portraits (headshots + frames) and the safe avatar pool. Both are
    // static, public, and safe for all ages.
    app.get("/images/retinue/*", [](auto *res, auto *req) {
        serve_image_subtree(res, std::string(req->getUrl()),
                            "/images/retinue/", g_images_dir + "/retinue/");
    });

    app.get("/images/avatars/safe/*", [](auto *res, auto *req) {
        serve_image_subtree(res, std::string(req->getUrl()),
                            "/images/avatars/safe/", g_images_dir + "/avatars/safe/");
    });

    // Age-gated custom PFP stream. Never served statically: a custom avatar is
    // returned only to an authenticated viewer who is the owner or an adult.
    // Auth rides the query string (?username=&token=) because the browser
    // requests this via <img src>. TODO: moderation/review before release.
    app.get("/api/avatar/*", [](auto *res, auto *req) {
        const std::string url(req->getUrl());
        const std::string prefix = "/api/avatar/";
        std::string rest = url.substr(prefix.length());
        const size_t query = rest.find('?');
        std::string query_string;
        if (query != std::string::npos) {
            query_string = rest.substr(query + 1);
            rest = rest.substr(0, query);
        }
        // Parse the id and the auth query params.
        int64_t character_id = 0;
        std::string username;
        std::string token;
        try {
            character_id = std::stoll(rest);
        } catch (const std::exception&) {
            character_id = 0;
        }
        size_t pos = 0;
        while (pos < query_string.size()) {
            const size_t amp = query_string.find('&', pos);
            const std::string pair = query_string.substr(
                pos, (amp == std::string::npos) ? std::string::npos : amp - pos);
            const size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                const std::string key = pair.substr(0, eq);
                const std::string value = pair.substr(eq + 1);
                if (key == "username") username = value;
                else if (key == "token") token = value;
            }
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }

        if (character_id <= 0 || username.empty() || token.empty() ||
            !AuthManager::getInstance().authenticateWithToken(username, token)) {
            res->writeStatus("404 Not Found")->end("Not found");
            return;
        }

        auto& db = Database::getInstance().gameDB();
        std::unique_ptr<std::string> custom_id;
        int owner_user_id = 0;
        db << "SELECT avatar_custom_id, user_id FROM characters WHERE id = ?;" << character_id
           >> [&](std::unique_ptr<std::string> c, int uid) {
                  custom_id = std::move(c);
                  owner_user_id = uid;
              };
        if (!custom_id || custom_id->empty() || owner_user_id == 0) {
            res->writeStatus("404 Not Found")->end("Not found");
            return;
        }

        bool owner_adult = false;
        db << "SELECT adult FROM users WHERE id = ?;" << owner_user_id
           >> [&](bool a) { owner_adult = a; };
        int viewer_user_id = 0;
        bool viewer_adult = false;
        db << "SELECT id, adult FROM users WHERE username = ?;" << username
           >> [&](int id, bool a) { viewer_user_id = id; viewer_adult = a; };
        if (!owner_adult || viewer_user_id == 0 ||
            (viewer_user_id != owner_user_id && !viewer_adult)) {
            res->writeStatus("404 Not Found")->end("Not found");
            return;
        }

        serve_image_subtree(res, "/" + *custom_id, "/", "uploads/avatars/");
    });

    if (!quiet) {
        // `quiet` is a main-scope bool; the App is constructed after it and
        // destroyed before it on the fall-through path, so the reference holds.
        app.get("/*", [&quiet](auto *res, auto *req) {
            res->end("Ravenest Build and Battle Server v1.0");
        });
    }

    // `mini_games` outlives the App (declared earlier in main); see W9 note.
    app.post("/api/*", [&mini_games](auto *res, auto *req) {
        handleApiRequest(mini_games.config_cache(), res, req);
    });

    app.listen(port, [port, quiet](auto *listenSocket) {
        g_listen_socket = listenSocket;
        if (listenSocket) {
            if (!quiet) {
                std::cout << "Ravenest Server listening on port " << port << std::endl;
            }
        } else {
            std::cerr << "Failed to bind to port " << port << std::endl;
        }
    });

    // Graceful shutdown: a signal (or --shutdown-after-ms for tests) sets the
    // flag; the monitor thread posts the close-all to the loop. uWS is not
    // thread-safe, so the monitor only uses Loop::defer.
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    std::thread shutdown_monitor([main_loop, shutdown_after_ms]() {
        const auto start = std::chrono::steady_clock::now();
        for (;;) {
            if (g_shutdown_requested.load(std::memory_order_relaxed)) break;
            if (shutdown_after_ms > 0) {
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count();
                if (elapsed >= shutdown_after_ms) {
                    g_shutdown_requested.store(true, std::memory_order_relaxed);
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        // Close every combat socket + the listener so the loop falls through.
        // HTTP keep-alive sockets time out on their own (HTTP_TIMEOUT_S = 10s).
        main_loop->defer([]() {
            std::vector<uWS::WebSocket<false, true, ws_combat_user_data>*> snapshot(
                g_open_combat_sockets.begin(), g_open_combat_sockets.end());
            for (auto* ws : snapshot) {
                ws->close();
            }
            if (g_listen_socket) {
                us_listen_socket_close(0, g_listen_socket);
                g_listen_socket = nullptr;
            }
        });

        // Backstop: never let a signal hang the process if some socket keeps the
        // loop alive. The normal path is a clean fall-through (run() returns and
        // main sets g_run_returned before joining this thread).
        for (int i = 0; i < 150; ++i) {
            if (g_run_returned.load(std::memory_order_relaxed)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::quick_exit(0);
    });

    app.run();

    // Fall-through teardown: stop producers before the App/loop is destroyed so
    // no worker can post into a dead loop.
    g_run_returned.store(true, std::memory_order_relaxed);
    g_shutdown_requested.store(true, std::memory_order_relaxed);
    if (shutdown_monitor.joinable()) shutdown_monitor.join();
    app_slot->store(nullptr, std::memory_order_release);
    g_combat_manager.shutdown();

    return 0;
}