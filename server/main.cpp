#include <iostream>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>
#include <uWebSockets/App.h>
#include "Database.hpp"
#include "ApiResponse.hpp"
#include "AuthManager.hpp"
#include "ApiHandlers.hpp"
#include "PasswordHash.hpp"
#include "SafeNameGenerator.hpp"

using json = nlohmann::json;

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

int main() {
    const int port = 2290;

    if (!SafeNameGenerator::getInstance().initialize("config/safe_words_1.txt", "config/safe_words_2.txt")) {
        std::cerr << "Warning: Failed to load safe word lists" << std::endl;
    }

    uWS::App().get("/*", [](auto *res, auto *req) {
        res->end("Ravenest Build and Battle Server v1.0");
    })
    .post("/api/*", [](auto *res, auto *req) {
        handleApiRequest(res, req);
    })
    .listen(port, [port](auto *listenSocket) {
        if (listenSocket) {
            std::cout << "Ravenest Server listening on port " << port << std::endl;
        } else {
            std::cerr << "Failed to bind to port " << port << std::endl;
        }
    })
    .run();

    return 0;
}