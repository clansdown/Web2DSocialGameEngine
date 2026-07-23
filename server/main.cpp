#include <iostream>
#include <fstream>
#include <optional>
#include <chrono>
#include <chrono>
#include <iomanip>
#include <typeinfo>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <random>
#include <map>
#include <set>
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
#include "DigitalCredentialsVerifier.hpp"
#include "game_logic.hpp"
#include "PlayerStateDB.hpp"
#include "mini_games/mini_game_registry.hpp"
#include "TowerDefenseMapCache.hpp"
#include "TextManager.hpp"
#include "ImageReader.hpp"
#include "UnitUnlockCalculator.hpp"
#include <sqlite_modern_cpp/errors.h>

using json = nlohmann::json;

static const bool login_debug = false;
static MiniGames* g_mini_games = nullptr;

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

std::atomic<int> g_request_count(0);
std::atomic<bool> g_test_complete(false);
int g_test_num_requests = 0;
int g_test_timeout_seconds = 0;
bool g_verbose = false;
std::string g_db_dir = ".";
std::string g_text_dir = "text";

// Public endpoints that don't require authentication
const std::unordered_set<std::string> PUBLIC_ENDPOINTS = {
    "createAccount",
    "getTexts",
    "verifyAgeOverride",
    "getUITextures"
};

void check_test_limits(uWS::App& app) {
    if (g_test_num_requests > 0 || g_test_timeout_seconds > 0) {
        std::thread([&app]() {
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
    db << "SELECT id, adult FROM users WHERE username = ?;"
       << *username
       >> [&](int id, bool a) { user_id = id; adult = a; };

    if (user_id == 0) {
        response.error = "User not found";
        return response;
    }

    std::vector<json> characters_list;
    db << "SELECT id, display_name, safe_display_name, level, archetype, sex FROM characters WHERE user_id = ?;"
       << user_id
       >> [&](int id, std::string display_name, std::string safe_display_name, int level, std::unique_ptr<std::string> archetype, std::unique_ptr<std::string> sex) {
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
             characters_list.push_back(character);
         };

    response.data["user_id"] = user_id;
    response.data["username"] = *username;
    response.data["adult"] = adult;
    response.data["characters"] = characters_list;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
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

    db << "SELECT display_name, safe_display_name, level, archetype, sex FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::string dn, std::string sdn, int l, std::unique_ptr<std::string> a, std::unique_ptr<std::string> s) {
             display_name = dn;
             safe_display_name = sdn;
             level = l;
             archetype = std::move(a);
             sex = std::move(s);
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
        response.error = "Invalid action: must be 'create', 'demolish', or 'move'";
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
    if (fiefdom_id == 0) {
        response.error = "fiefdom_id required";
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

    GameLogic::updateStateSince(config_cache, last_update_time, std::to_string(fiefdom_id));

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
            std::string sql = "INSERT INTO users (username, password_hash, created_at, adult) VALUES (?, ?, ?, ?)";
            std::cout << "[DEBUG] handleCreateAccount: Step 1 - INSERT user" << std::endl;
            log_sql("handleCreateAccount", sql);
            try {
                db << sql << new_username << password_hash << now << (adult ? 1 : 0);
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

        response.data["user_id"] = user_id;
        response.data["username"] = new_username;
        response.data["adult"] = adult;
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
                    filtered_configs["fiefdom_building_types"] = config_cache.getFiefdomBuildingTypes();
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

        response.data = state.toJson();

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
    if (mg_config.contains("duke_levels")) {
        for (const auto& lvl : mg_config["duke_levels"]) {
            if (lvl["id"] == level_id) {
                return mg_config.value("duke_schedule_file", std::string());
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
    if (map_file.empty()) map_file = find_map(mg_config.value("duke_levels", json::array()));

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

            std::cerr << "[tdRound] Completion body: lives_lost=" << lives_lost
                      << " gold_earned=" << gold_earned
                      << std::endl;

            int new_lives = session->lives - lives_lost;
            int new_gold = session->gold + gold_earned;

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
                player_state_db::update_game_session(db, session_id, new_lives, new_gold, "active", now);

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
                player_state_db::update_game_session(db, session_id, new_lives, new_gold, new_state, now);

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
                        if (level_rewards.empty() && mg_config.contains("duke_levels")) {
                            check_rewards(mg_config["duke_levels"]);
                        }
                    }
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
                    if (end_result.base_unlocked) {
                        response.data["base_unlocked"] = true;
                    }
                    auto state = player_state_db::get_player_game_state(db, character_id);
                    response.data["game_phase"] = state.game_phase;
                    response.data["land_patent_earned"] = (state.game_phase == "land_patent");
                    response.data["duke_right_earned"] = (state.game_phase == "duke_right");
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
        } else if (state.game_phase == "duke_track") {
            auto next = player_state_db::get_next_incomplete_level(db, character_id, mini_game, 25);
            if (!next) {
                response.error = "All duke levels completed";
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
            bool is_duke = false;
            int d = find_field(mg_config.value("levels", json::array()), "difficulty", 1);
            if (d < 0) {
                d = find_field(mg_config.value("duke_levels", json::array()), "difficulty", 1);
                is_duke = true;
            }
            if (d >= 0) difficulty = d;
            int r = find_field(mg_config.value("levels", json::array()), "rounds", 1);
            if (r < 0) r = find_field(mg_config.value("duke_levels", json::array()), "rounds", 1);
            if (r >= 0) total_rounds = r;

            // Resolve schedule file for this level
            std::string schedule_file;
            if (!is_duke && mg_config.contains("schedule_file")) {
                schedule_file = mg_config["schedule_file"].get<std::string>();
            } else if (is_duke && mg_config.contains("duke_schedule_file")) {
                schedule_file = mg_config["duke_schedule_file"].get<std::string>();
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
        } else if (state.game_phase == "duke_track") {
            auto next_level = player_state_db::get_next_incomplete_level(db, character_id, mini_game, 25);

            if (!next_level) {
                response.error = "All duke levels completed";
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
        int expected_total_levels = (state.game_phase == "duke_track") ? 25 : 9;

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
        response.data["duke_right_earned"] = false;

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
            } else if (state.game_phase == "duke_track") {
                // Transition to duke_right phase
                player_state_db::earn_duke_right(db, character_id, now);
                response.data["duke_right_earned"] = true;
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

                // Check duke levels if not found in main levels
                if (level_rewards.empty() && mg_config.contains("duke_levels")) {
                    for (const auto& lvl : mg_config["duke_levels"]) {
                        if (lvl["id"] == level_id && lvl.contains("reward")) {
                            level_rewards = lvl["reward"];
                            break;
                        }
                    }
                }
            }

            response.data["rewards"] = level_rewards;
        }

        player_state_db::clear_current_mini_game(db, character_id, now);

        // Determine game phase for response
        if (end_result.all_levels_done) {
            if (state.game_phase == "initial_mission") {
                response.data["game_phase"] = "land_patent";
            } else if (state.game_phase == "duke_track") {
                response.data["game_phase"] = "duke_right";
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
        } else {
            response.error = "Unknown mini_game: " + mini_game;
            return response;
        }
    } else {
        response.data = config_cache.getMiniGames();
    }

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
        std::ifstream f("config/ui_textures.json");
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
        for (const auto& entry : std::filesystem::directory_iterator("images/ui", ec)) {
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

    std::optional<std::string> sex;
    if (body.contains("sex") && body["sex"].is_string()) {
        sex = body["sex"].get<std::string>();
    }

    json texts = json::object();

    for (const auto& id_val : body["text_ids"]) {
        if (!id_val.is_string()) continue;
        std::string text_id = id_val.get<std::string>();
        std::string raw = g_text_manager->get_text(language, text_id);
        texts[text_id] = TextManager::substitute_gender(raw, sex);
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

ApiResponse handleGetDukedoms(GameConfigCache& config_cache, const json& body,
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
        json dukedoms_list = json::array();

        db << "SELECT d.id, d.name, d.description, d.owner_character_id, d.created_at, "
              "COUNT(dm.id) as member_count, c.display_name as owner_name "
              "FROM dukedoms d "
              "LEFT JOIN dukedom_members dm ON dm.dukedom_id = d.id "
              "LEFT JOIN characters c ON c.id = d.owner_character_id "
              "GROUP BY d.id "
              "ORDER BY d.name;"
           >> [&](int id, std::string name, std::string description, int owner_id, int64_t created_at, int member_count, std::string owner_name) {
                json entry;
                entry["id"] = id;
                entry["name"] = name;
                entry["description"] = description;
                entry["owner_character_id"] = owner_id;
                entry["owner_name"] = owner_name;
                entry["member_count"] = member_count;
                entry["created_at"] = created_at;
                dukedoms_list.push_back(entry);
            };

        response.data["dukedoms"] = dukedoms_list;

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleGetDukedoms", e.what());
        response.error = std::string("Failed to fetch dukedoms: ") + e.what();
    }

    return response;
}

ApiResponse handleJoinDukedom(GameConfigCache& config_cache, const json& body,
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

    if (!body.contains("dukedom_id") || !body["dukedom_id"].is_number_integer()) {
        response.error = "dukedom_id required";
        return response;
    }

    int character_id = body["character_id"].get<int>();
    int dukedom_id = body["dukedom_id"].get<int>();
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

        // Verify dukedom exists
        int dukedom_exists = 0;
        db << "SELECT COUNT(*) FROM dukedoms WHERE id = ?;" << dukedom_id >> [&](int count) { dukedom_exists = count; };
        if (!dukedom_exists) {
            response.error = "Dukedom not found";
            return response;
        }

        // Check if already a member of any dukedom
        int existing_member = 0;
        db << "SELECT COUNT(*) FROM dukedom_members WHERE character_id = ?;"
           << character_id >> [&](int count) { existing_member = count; };
        if (existing_member > 0) {
            response.error = "Already a member of a dukedom";
            return response;
        }

        // Verify character is in land_patent phase
        std::string current_phase;
        db << "SELECT game_phase FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](std::string phase) { current_phase = phase; };
        if (current_phase != "land_patent") {
            response.error = "Must have a land patent to join a dukedom";
            return response;
        }

        // Create a fiefdom for this character
        static std::atomic<int> fiefdom_counter(0);
        int counter = fiefdom_counter++;
        std::string fiefdom_name = "Manor of " + *username;
        int fx = (counter % 100) * 10;
        int fy = (counter / 100) * 10;

        db << "INSERT INTO fiefdoms (owner_id, name, x, y, peasants, gold, grain, wood, steel, bronze, stone, leather, mana, wall_count, morale, last_update_time, manor_level) "
              "VALUES (?, ?, ?, ?, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0, ?, 1);"
           << character_id << fiefdom_name << fx << fy << now;

        int fiefdom_id = db.last_insert_rowid();

        // Add to dukedom members
        db << "INSERT INTO dukedom_members (dukedom_id, character_id, fiefdom_id, joined_at, role) "
              "VALUES (?, ?, ?, ?, 'member');"
           << dukedom_id << character_id << fiefdom_id << now;

        // Unlock base (sandbox phase)
        player_state_db::unlock_base(db, character_id, now);

        response.data["dukedom_id"] = dukedom_id;
        response.data["fiefdom_id"] = fiefdom_id;
        response.data["game_phase"] = "sandbox";
        response.data["base_unlocked"] = true;

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleJoinDukedom", e.what());
        response.error = std::string("Failed to join dukedom: ") + e.what();
    }

    return response;
}

ApiResponse handleCreateDukedom(GameConfigCache& config_cache, const json& body,
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

        // Check for duplicate dukedom name
        int name_taken = 0;
        db << "SELECT COUNT(*) FROM dukedoms WHERE name = ?;" << name >> [&](int count) { name_taken = count; };
        if (name_taken > 0) {
            response.error = "A dukedom with that name already exists";
            return response;
        }

        // Verify character is in duke_right phase (all 25 levels complete)
        std::string current_phase;
        db << "SELECT game_phase FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](std::string phase) { current_phase = phase; };
        if (current_phase != "duke_right") {
            response.error = "Must complete the duke track to start a dukedom";
            return response;
        }

        // Check not already a member
        int existing_member = 0;
        db << "SELECT COUNT(*) FROM dukedom_members WHERE character_id = ?;"
           << character_id >> [&](int count) { existing_member = count; };
        if (existing_member > 0) {
            response.error = "Already a member of a dukedom";
            return response;
        }

        // Create fiefdom
        static std::atomic<int> duke_fiefdom_counter(0);
        int counter = duke_fiefdom_counter++;
        std::string fiefdom_name = "Manor of " + *username;
        int fx = (counter % 100) * 10;
        int fy = (counter / 100) * 10;

        db << "INSERT INTO fiefdoms (owner_id, name, x, y, peasants, gold, grain, wood, steel, bronze, stone, leather, mana, wall_count, morale, last_update_time, manor_level) "
              "VALUES (?, ?, ?, ?, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0, ?, 1);"
           << character_id << fiefdom_name << fx << fy << now;

        int fiefdom_id = db.last_insert_rowid();

        // Create dukedom
        db << "INSERT INTO dukedoms (name, owner_character_id, description, created_at) "
              "VALUES (?, ?, ?, ?);"
           << name << character_id << description << now;

        int dukedom_id = db.last_insert_rowid();

        // Add founder as member with mesne_lord role
        db << "INSERT INTO dukedom_members (dukedom_id, character_id, fiefdom_id, joined_at, role) "
              "VALUES (?, ?, ?, ?, 'mesne_lord');"
           << dukedom_id << character_id << fiefdom_id << now;

        // Unlock base
        player_state_db::unlock_base(db, character_id, now);

        response.data["dukedom_id"] = dukedom_id;
        response.data["fiefdom_id"] = fiefdom_id;
        response.data["game_phase"] = "sandbox";
        response.data["base_unlocked"] = true;

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleCreateDukedom", e.what());
        response.error = std::string("Failed to create dukedom: ") + e.what();
    }

    return response;
}

ApiResponse handleStartDukeTrack(GameConfigCache& config_cache, const json& body,
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

        // Verify character ownership
        int owner_id = 0;
        int user_id = 0;
        db << "SELECT id FROM users WHERE username = ?;" << *username >> [&](int id) { user_id = id; };
        db << "SELECT user_id FROM characters WHERE id = ?;" << character_id >> [&](int uid) { owner_id = uid; };
        if (owner_id != user_id) {
            response.error = "Character does not belong to this user";
            return response;
        }

        // Verify character is in land_patent phase
        std::string current_phase;
        db << "SELECT game_phase FROM player_game_state WHERE character_id = ?;"
           << character_id >> [&](std::string phase) { current_phase = phase; };
        if (current_phase != "land_patent") {
            response.error = "Must have a land patent to start the duke track";
            return response;
        }

        // Switch to duke_track phase
        player_state_db::start_duke_track(db, character_id, now);

        response.data["game_phase"] = "duke_track";

        if (new_token) {
            response.data["token"] = *new_token;
        }
    } catch (const std::exception& e) {
        log_error("handleStartDukeTrack", e.what());
        response.error = std::string("Failed to start duke track: ") + e.what();
    }

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

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --db-dir PATH              Database directory (default: current)" << std::endl;
    std::cout << "  --text-dir PATH            Text directory (default: ./text)" << std::endl;
    std::cout << "  --port PORT                Port to bind (default: 2290)" << std::endl;
    std::cout << "  --init-db                  Initialize all database tables and indexes, then exit" << std::endl;
    std::cout << "  --create-tables            Create all database tables, then exit" << std::endl;
    std::cout << "  --ensure-indexes           Ensure all indexes exist, then exit" << std::endl;
    std::cout << "  --test-num-requests N      Exit after N requests (agent test mode)" << std::endl;
    std::cout << "  --test-timeout-seconds M    Exit after M seconds (agent test mode)" << std::endl;
    std::cout << "  --verbose                  Enable verbose logging" << std::endl;
    std::cout << "  --quiet                    Minimal logging" << std::endl;
    std::cout << "  -h, --help                 Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    int port = 2290;
    bool verbose = false;
    bool quiet = false;
    bool init_db_mode = false;
    bool create_tables_mode = false;
    bool ensure_indexes_mode = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
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

    if (!quiet) {
        std::cout << "Ravenest Server initializing..." << std::endl;
        std::cout << "Database directory: " << g_db_dir << std::endl;
        std::cout << "Port: " << port << std::endl;
        if (g_test_num_requests > 0 || g_test_timeout_seconds > 0) {
            std::cout << "Test mode: enabled" << std::endl;
        }
    }

    if (!SafeNameGenerator::getInstance().initialize("config/safe_words_1.txt", "config/safe_words_2.txt")) {
        std::cerr << "Warning: Failed to load safe word lists" << std::endl;
    }

    MiniGames mini_games;
    g_mini_games = &mini_games;
    if (!mini_games.initialize("config")) {
        std::cerr << "Warning: Failed to load game configuration files" << std::endl;
    }

    TowerDefenseMapCache::get_instance().initialize("config/tower_defense/maps");

    if (!ImageCache::getInstance().initialize("images")) {
        std::cerr << "Warning: Failed to initialize image cache" << std::endl;
    }

    static TextManager text_manager(g_text_dir);
    g_text_manager = &text_manager;
    if (!quiet) {
        std::cout << "Text directory: " << g_text_dir << std::endl;
    }



    std::string game_db_path = g_db_dir + "/game.db";
    std::string messages_db_path = g_db_dir + "/messages.db";

    if (!quiet) {
        std::cout << "Opening databases..." << std::endl;
    }

    try {
        Database::getInstance().init(game_db_path, messages_db_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize databases: " << e.what() << std::endl;
        return 1;
    }

    if (init_db_mode || create_tables_mode || ensure_indexes_mode) {
        auto& game_db = Database::getInstance().gameDB();
        auto& messages_db = Database::getInstance().messagesDB();

        if (init_db_mode) {
            if (!quiet) {
                std::cout << "Initializing all database tables and indexes..." << std::endl;
            }
            try {
                initializeAllDatabases(game_db, messages_db);
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
                initializeMessagesDB(messages_db);
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
                ensureMessagesDBIndexes(messages_db);
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
        initializeAllDatabases(Database::getInstance().gameDB(), Database::getInstance().messagesDB());
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize database schemas: " << e.what() << std::endl;
        return 1;
    }

    uWS::App app;

    check_test_limits(app);

    app.get("/images/ui/*", [](auto *res, auto *req) {
        std::string url(req->getUrl());
        std::string relative = url.substr(std::string("/images/ui/").length());

        if (relative.empty() || relative.find("..") != std::string::npos) {
            res->writeStatus("404 Not Found")->end("Not found");
            return;
        }

        std::string filepath = "images/ui/" + relative;
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            res->writeStatus("404 Not Found")->end("Not found");
            return;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        if (relative.size() >= 4) {
            std::string ext = relative.substr(relative.size() - 4);
            if (ext == ".png") res->writeHeader("Content-Type", "image/png");
            else if (ext == ".jpg") res->writeHeader("Content-Type", "image/jpeg");
        }

        res->end(content);
    });

    app.get("/images/tower_defense/*", [](auto *res, auto *req) {
        std::string url(req->getUrl());
        std::string relative = url.substr(std::string("/images/tower_defense/").length());

        if (relative.empty() || relative.find("..") != std::string::npos) {
            res->writeStatus("404 Not Found")->end("Not found");
            return;
        }

        std::string filepath = "images/tower_defense/" + relative;
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            res->writeStatus("404 Not Found")->end("Not found");
            return;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        if (relative.size() >= 4) {
            std::string ext = relative.substr(relative.size() - 4);
            if (ext == ".png") res->writeHeader("Content-Type", "image/png");
            else if (ext == ".jpg") res->writeHeader("Content-Type", "image/jpeg");
        }

        res->end(content);
    });

    if (!quiet) {
        app.get("/*", [&quiet](auto *res, auto *req) {
            res->end("Ravenest Build and Battle Server v1.0");
        });
    }

    app.post("/api/*", [&mini_games](auto *res, auto *req) {
        handleApiRequest(mini_games.config_cache(), res, req);
    });

    app.listen(port, [port, quiet](auto *listenSocket) {
        if (listenSocket) {
            if (!quiet) {
                std::cout << "Ravenest Server listening on port " << port << std::endl;
            }
        } else {
            std::cerr << "Failed to bind to port " << port << std::endl;
        }
    });

    app.run();

    return 0;
}