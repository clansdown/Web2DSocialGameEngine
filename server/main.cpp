#include <iostream>
#include <optional>
#include <chrono>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <nlohmann/json.hpp>
#include <uWebSockets/App.h>
#include "Database.hpp"
#include "ApiResponse.hpp"
#include "AuthManager.hpp"
#include "ApiHandlers.hpp"
#include "PasswordHash.hpp"
#include "SafeNameGenerator.hpp"

using json = nlohmann::json;

std::atomic<int> g_request_count(0);
std::atomic<bool> g_test_complete(false);
int g_test_num_requests = 0;
int g_test_timeout_seconds = 0;
bool g_verbose = false;
std::string g_db_dir = ".";

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
    ApiResponse response;

    auto& db = Database::getInstance().gameDB();

    int player_id = 0;
    std::string name;
    int level = 1;

    db << "SELECT id, name, level FROM players WHERE name = ?;"
       << *username
       >> [&](int id, std::string n, int l) {
            player_id = id;
            name = n;
            level = l;
        };

    if (player_id == 0) {
        response.error = "Player not found";
        return response;
    }

    response.data["id"] = player_id;
    response.data["name"] = name;
    response.data["level"] = level;
    response.data["username"] = *username;

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

ApiResponse handleGetPlayer(const json& body,
                            const std::optional<std::string>& username,
                            const ClientInfo& client,
                            const std::optional<std::string>& new_token)
{
    ApiResponse response;

    int player_id = body.value("player_id", 0);
    if (player_id == 0) {
        response.error = "player_id required";
        return response;
    }

    auto& db = Database::getInstance().gameDB();

    std::string name;
    int level;

    db << "SELECT name, level FROM players WHERE id = ?;"
       << player_id
       >> [&](std::string n, int l) {
            name = n;
            level = l;
        };

    response.data["id"] = player_id;
    response.data["name"] = name;
    response.data["level"] = level;

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
    response.data["message"] = "Build endpoint received";
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
    response.data["message"] = "getFiefdom endpoint received";
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
    bool adult = body.value("adult", false);
    std::string word1 = body.value("word1", "");
    std::string word2 = body.value("word2", "");
    std::string displayName = body.value("displayName", "");

    if (new_username.empty() || password.empty()) {
        response.error = "username and password required";
        return response;
    }

    if (word1.empty() || word2.empty()) {
        response.error = "word1 and word2 required for safe display name";
        return response;
    }

    if (!adult && !displayName.empty()) {
        response.error = "displayName can only be set if adult is true";
        return response;
    }

    auto& db = Database::getInstance().gameDB();

    std::string existing;
    db << "SELECT username FROM users WHERE username = ?;"
       << new_username
       >> [&](std::string u) { existing = u; };

    if (!existing.empty()) {
        response.error = "Username already exists";
        return response;
    }

    std::optional<std::string> safeDisplayNameOpt = SafeNameGenerator::getInstance().generateSafeDisplayName(
        word1, word2, new_username);

    if (!safeDisplayNameOpt) {
        response.error = "Invalid word1 or word2 - words must exist in safe word lists";
        return response;
    }

    std::string safeDisplayName = *safeDisplayNameOpt;

    std::string password_hash;
    try {
        password_hash = hashPassword(password);
    } catch (const std::exception& e) {
        response.error = std::string("Password hashing failed: ") + e.what();
        return response;
    }

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    db << "INSERT INTO users (username, password_hash, created_at, adult, displayName, safeDisplayName) "
       << "VALUES (?, ?, ?, ?, ?, ?);"
       << new_username << password_hash << now << (adult ? 1 : 0)
       << (adult ? displayName : "") << safeDisplayName;

    int user_id = db.last_insert_rowid();

    db << "INSERT INTO players (user_id, name, level) VALUES (?, ?, 1);"
       << user_id << new_username;

    int player_id = db.last_insert_rowid();

    std::string ip = std::string(client.real_ip);
    std::string token = AuthManager::getInstance().authenticateWithPassword(
        new_username, password, ip);

    response.data["id"] = user_id;
    response.data["username"] = new_username;
    response.data["player_id"] = player_id;
    response.data["adult"] = adult;
    response.data["displayName"] = adult ? displayName : "";
    response.data["safeDisplayName"] = safeDisplayName;
    response.data["token"] = token;

    return response;
}

ApiResponse handleUpdateProfile(const json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token)
{
    ApiResponse response;

    if (!username || username->empty()) {
        response.error = "authentication required";
        return response;
    }

    bool adult = body.value("adult", false);
    std::string displayName = body.value("displayName", "");
    std::string word1 = body.value("word1", "");
    std::string word2 = body.value("word2", "");

    if (!adult && !displayName.empty()) {
        response.error = "displayName can only be set if adult is true";
        return response;
    }

    auto& db = Database::getInstance().gameDB();

    int user_id = 0;
    db << "SELECT id FROM users WHERE username = ?;"
       << *username
       >> [&](int id) { user_id = id; };

    if (user_id == 0) {
        response.error = "user not found";
        return response;
    }

    bool regenerateSafeName = !word1.empty() && !word2.empty();
    std::string safeDisplayName;

    if (regenerateSafeName) {
        std::optional<std::string> safeNameOpt = SafeNameGenerator::getInstance().generateSafeDisplayName(
            word1, word2, *username);

        if (!safeNameOpt) {
            response.error = "Invalid word1 or word2 - words must exist in safe word lists";
            return response;
        }
        safeDisplayName = *safeNameOpt;

        db << "UPDATE users SET safeDisplayName = ? WHERE id = ?;"
           << safeDisplayName << user_id;
    }

    if (body.contains("adult")) {
        if (!adult) {
            db << "UPDATE users SET adult = 0, displayName = '' WHERE id = ?;"
               << user_id;
        } else {
            db << "UPDATE users SET adult = 1, displayName = ? WHERE id = ?;"
               << displayName << user_id;
        }
    } else if (body.contains("displayName")) {
        db << "UPDATE users SET displayName = ? WHERE id = ?;"
           << displayName << user_id;
    }

    response.data["adult"] = adult;
    response.data["displayName"] = adult ? displayName : "";
    if (regenerateSafeName) {
        response.data["safeDisplayName"] = safeDisplayName;
    }

    if (new_token) {
        response.data["token"] = *new_token;
    }

    return response;
}

void handleApiRequest(auto* res, auto* req) {
    std::string buffer;
    res->onData([res, req, buffer = std::move(buffer)](std::string_view data, bool isLast) mutable {
        buffer += data;
        if (!isLast) return;

        try {
            json body = json::parse(buffer);

            std::string_view url = req->getUrl();
            std::string endpoint = extractEndpointName(url);

            ClientInfo client = parseClientHeaders(req);
            std::string ip_address = std::string(client.real_ip);

            json auth_object;
            if (body.contains("auth") && body["auth"].is_object()) {
                auth_object = body["auth"];
            }

            AuthResult auth_result = handleAuth(endpoint, auth_object, ip_address);

            if (!auth_result.isOk()) {
                ApiResponse response;
                response.needs_auth = auth_result.needs_auth;
                response.auth_failed = auth_result.auth_failed;
                if (auth_result.error) response.error = *auth_result.error;
                sendJsonResponse(res, response);
                return;
            }

            if (endpoint == "createAccount") {
                ApiResponse response = handleCreateAccount(body, auth_result.username, client, auth_result.new_token);
                sendJsonResponse(res, response);
                return;
            }

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
            ApiResponse error_response;
            error_response.error = std::string("Invalid JSON: ") + e.what();
            sendJsonResponse(res, error_response);
        } catch (const std::exception& e) {
            ApiResponse error_response;
            error_response.error = e.what();
            sendJsonResponse(res, error_response);
        }
    });
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --db-dir PATH              Database directory (default: current)" << std::endl;
    std::cout << "  --port PORT                Port to bind (default: 2290)" << std::endl;
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

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--db-dir") == 0) {
            if (i + 1 < argc) {
                g_db_dir = argv[++i];
            }
        } else if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                port = std::atoi(argv[++i]);
            }
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

    check_test_limits(uWS::App());

    uWS::App app;

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