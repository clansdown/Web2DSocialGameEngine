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
#include "GameConfigCache.hpp"
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
#include <sqlite_modern_cpp/errors.h>

using json = nlohmann::json;

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

ApiResponse handleLogin(const json& body,
                       const std::optional<std::string>& username,
                       const ClientInfo& client,
                       const std::optional<std::string>& new_token)
{
    std::cout << "[DEBUG] handleLogin: username=" << (username ? *username : "(nullopt)")
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

ApiResponse handleGetCharacter(const json& body,
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

ApiResponse handleBuild(const json& body,
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

ApiResponse handleGetWorld(const json& body,
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

ApiResponse handleGetFiefdom(const json& body,
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

    GameLogic::updateStateSince(last_update_time, std::to_string(fiefdom_id));

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

ApiResponse handleSally(const json& body,
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

ApiResponse handleCampaign(const json& body,
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

ApiResponse handleHunt(const json& body,
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

ApiResponse handleCreateAccount(const json& body,
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

ApiResponse handleUpdateUserProfile(const json& body,
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

ApiResponse handleUpdateCharacterProfile(const json& body,
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

ApiResponse handleGetGameInfo(const json& body,
                              const std::optional<std::string>& username,
                              const ClientInfo& client,
                              const std::optional<std::string>& new_token)
{
    ApiResponse response;

    auto& cache = GameConfigCache::getInstance();
    auto& img_cache = ImageCache::getInstance();

    if (!cache.isLoaded()) {
        response.error = "Game configuration not loaded";
        return response;
    }

    if (!img_cache.isLoaded()) {
        response.error = "Images not loaded";
        return response;
    }

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
                    filtered_configs["damage_types"] = cache.getDamageTypes();
                } else if (asset_type == "fiefdom_building_types") {
                    filtered_configs["fiefdom_building_types"] = cache.getFiefdomBuildingTypes();
                } else if (asset_type == "player_combatants") {
                    filtered_configs["player_combatants"] = cache.getPlayerCombatants();
                } else if (asset_type == "enemy_combatants") {
                    filtered_configs["enemy_combatants"] = cache.getEnemyCombatants();
                } else if (asset_type == "heroes") {
                    filtered_configs["heroes"] = cache.getHeroes();
                } else if (asset_type == "fiefdom_officials") {
                    filtered_configs["fiefdom_officials"] = cache.getFiefdomOfficials();
                } else if (asset_type == "wall_config") {
                    filtered_configs["wall_config"] = cache.getWallConfig();
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
        configs = cache.getAllConfigs();
        images = img_cache.getImages();
    }

    response.data["configs"] = configs;
    response.data["images"] = images;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleGetPlayerState(const json& body,
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
        response.error = std::string("Failed to get player state: ") + e.what();
    }

    return response;
}

ApiResponse handleStartMiniGame(const json& body,
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

    auto& registry = MiniGameRegistry::getInstance();
    MiniGameHandler* handler = registry.get_handler(mini_game);

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
        response.error = std::string("Failed to start mini-game: ") + e.what();
    }

    return response;
}

ApiResponse handleEndMiniGame(const json& body,
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

        auto& registry = MiniGameRegistry::getInstance();
        MiniGameHandler* handler = registry.get_handler(mini_game);

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
                auto& config_cache = GameConfigCache::getInstance();
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
            auto& config_cache = GameConfigCache::getInstance();
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
        response.error = std::string("Failed to end mini-game: ") + e.what();
    }

    return response;
}

ApiResponse handleGetMiniGameConfig(const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username) {
        response.needs_auth = true;
        return response;
    }

    auto& config_cache = GameConfigCache::getInstance();

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

ApiResponse handleGetUITextures(const json& body,
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

    try {
        for (const auto& entry : std::filesystem::directory_iterator("images/ui")) {
            if (!entry.is_regular_file()) continue;
            std::string filename = entry.path().filename().string();
            if (filename.rfind("text_silk_", 0) != 0) continue;

            auto dims = get_image_dimensions(entry.path().string());
            if (dims.width <= 0 || dims.height <= 0) continue;

            json tex;
            tex["url"] = "/images/ui/" + filename;
            tex["width"] = dims.width;
            tex["height"] = dims.height;
            textures.push_back(tex);
        }
    } catch (const std::exception&) {
    }

    std::sort(textures.begin(), textures.end(), [](const json& a, const json& b) {
        return a["height"].get<int>() < b["height"].get<int>();
    });

    response.data["textures"] = textures;

    int padding_vertical_px = 60;
    int padding_horizontal_px = 60;
    try {
        std::ifstream f("config/ui_textures.json");
        if (f.is_open()) {
            json cfg = json::parse(f);
            if (cfg.contains(component_id) && cfg[component_id].is_object()) {
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
    } catch (const std::exception&) {
    }

    response.data["padding_vertical_px"] = padding_vertical_px;
    response.data["padding_horizontal_px"] = padding_horizontal_px;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleGetTexts(const json& body,
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

ApiResponse handleVerifyAgeOverride(const json& body,
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

ApiResponse handleSetCharacterArchetype(const json& body,
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
    db << "SELECT display_name, safe_display_name, level FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::string dn, std::string sdn, int l) {
             display_name = dn;
             safe_display_name = sdn;
             level = l;
         };

    response.data["id"] = character_id;
    response.data["display_name"] = display_name;
    response.data["safe_display_name"] = safe_display_name;
    response.data["level"] = level;
    response.data["archetype"] = archetype;
    response.data["sex"] = nullptr;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleSetCharacterSex(const json& body,
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

ApiResponse handleGetDukedoms(const json& body,
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
        response.error = std::string("Failed to fetch dukedoms: ") + e.what();
    }

    return response;
}

ApiResponse handleJoinDukedom(const json& body,
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
        response.error = std::string("Failed to join dukedom: ") + e.what();
    }

    return response;
}

ApiResponse handleCreateDukedom(const json& body,
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
        response.error = std::string("Failed to create dukedom: ") + e.what();
    }

    return response;
}

ApiResponse handleStartDukeTrack(const json& body,
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
        response.error = std::string("Failed to start duke track: ") + e.what();
    }

    return response;
}

void handleApiRequest(auto* res, auto* req) {
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
                 ip_address = std::move(ip_address), buffer = std::move(buffer)]
                (std::string_view data, bool isLast) mutable {
        buffer += data;
        if (!isLast) return;

        try {
            json body = json::parse(buffer);

            json auth_object;
            if (body.contains("auth") && body["auth"].is_object()) {
                auth_object = body["auth"];
            }

            // Handle public endpoints before authentication check
            if (PUBLIC_ENDPOINTS.count(endpoint)) {
                ApiResponse response;
                if (endpoint == "createAccount") {
                    response = handleCreateAccount(body, std::nullopt, client, std::nullopt);
                } else if (endpoint == "getTexts") {
                    response = handleGetTexts(body, std::nullopt, client, std::nullopt);
                } else if (endpoint == "verifyAgeOverride") {
                    response = handleVerifyAgeOverride(body, std::nullopt, client, std::nullopt);
                } else if (endpoint == "getUITextures") {
                    response = handleGetUITextures(body, std::nullopt, client, std::nullopt);
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
            auto& handlers = getEndpointHandlers();
            if (handlers.count(endpoint)) {
                ApiResponse response = handlers[endpoint](body, auth_result.username, client, auth_result.new_token);
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

    if (!GameConfigCache::getInstance().initialize("config")) {
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

    register_all_mini_game_handlers();

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

    app.post("/api/*", [](auto *res, auto *req) {
        handleApiRequest(res, req);
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