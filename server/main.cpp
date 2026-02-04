#include <iostream>
#include <nlohmann/json.hpp>
#include <uWebSockets/App.hpp>
#include "Database.hpp"

using json = nlohmann::json;

/**
 * @brief Client information parsed from nginx proxy headers.
 *
 * These values are populated from headers set by nginx reverse proxy.
 * Uses string_view since the lifetime is bounded by the request handler.
 * See README.md "Header Reference" section for full documentation.
 */
struct ClientInfo {
    std::string_view real_ip;           ///< X-Real-IP: True client IP address
    std::string_view forwarded_for;     ///< X-Forwarded-For: Forwarded IP chain
    std::string_view forwarded_proto;   ///< X-Forwarded-Proto: Original protocol
    std::string_view forwarded_host;    ///< X-Forwarded-Host: Original hostname
    std::string_view forwarded_port;    ///< X-Forwarded-Port: Original port
    std::string_view user_agent;        ///< User-Agent: Client browser info
    std::string_view host;              ///< Host: Original host header
    std::string_view request_id;        ///< X-Request-ID: Request tracking ID
};

/**
 * @brief Parses client-identifying headers from nginx reverse proxy.
 *
 * Extracts headers set by nginx proxy_set_header directives.
 * All string_view values are valid only during request handler lifetime.
 *
 * @param req Pointer to uWS HttpRequest containing headers
 * @return ClientInfo structure populated with header values
 */
ClientInfo parseClientHeaders(auto *req) {
    ClientInfo info;

    info.real_ip = req->getHeader("x-real-ip");
    info.forwarded_for = req->getHeader("x-forwarded-for");
    info.forwarded_proto = req->getHeader("x-forwarded-proto");
    info.forwarded_host = req->getHeader("x-forwarded-host");
    info.forwarded_port = req->getHeader("x-forwarded-port");
    info.user_agent = req->getHeader("user-agent");
    info.host = req->getHeader("host");
    info.request_id = req->getHeader("x-request-id");

    return info;
}

void sendError(auto *res, const std::string& message) {
    json error;
    error["error"] = message;
    res->end(error.dump());
}

void sendSuccess(auto *res, const json& data) {
    json response;
    response["status"] = "ok";
    response["data"] = data;
    res->end(response.dump());
}

/**
 * @brief Handles /api/login POST requests.
 *
 * Authenticates user credentials against game.db.
 *
 * @param res Pointer to uWS HttpResponse
 * @param req Pointer to uWS HttpRequest containing client headers
 * @param data JSON request body string
 */
void handleLogin(auto *res, auto *req, const std::string& data) {
    try {
        ClientInfo client = parseClientHeaders(req);

        // Optional: Log client info for debugging
        std::cout << "Login request from " << client.real_ip
                  << " (" << client.user_agent << ")" << std::endl;

        json body = json::parse(data);
        std::string username = body.value("username", "");
        std::string password = body.value("password", "");
        
        auto& db = Database::getInstance().gameDB();
        std::string stored_hash;
        
        db << "SELECT password_hash FROM users WHERE username = ?;" 
           << username 
           >> [&](std::string hash) { stored_hash = hash; };
        
        if (stored_hash.empty()) {
            sendError(res, "Invalid username or password");
            return;
        }
        
        json player_data;
        player_data["id"] = 1;
        player_data["username"] = username;
        
        sendSuccess(res, player_data);
        
    } catch (const json::exception& e) {
        sendError(res, std::string("Invalid JSON: ") + e.what());
    } catch (const std::exception& e) {
        sendError(res, std::string("Database error: ") + e.what());
    }
}

/**
 * @brief Handles /api/getPlayer POST requests.
 *
 * Retrieves player information from game.db.
 *
 * @param res Pointer to uWS HttpResponse
 * @param req Pointer to uWS HttpRequest containing client headers
 * @param data JSON request body string
 */
void handleGetPlayer(auto *res, auto *req, const std::string& data) {
    try {
        ClientInfo client = parseClientHeaders(req);

        json body = json::parse(data);
        int player_id = body.value("player_id", 0);
        
        if (player_id == 0) {
            sendError(res, "player_id required");
            return;
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
        
        json player;
        player["id"] = player_id;
        player["name"] = name;
        player["level"] = level;
        
        sendSuccess(res, player);
        
    } catch (const json::exception& e) {
        sendError(res, std::string("Invalid JSON: ") + e.what());
    } catch (const std::exception& e) {
        sendError(res, e.what());
    }
}

/**
 * @brief Handles stub endpoints awaiting implementation.
 *
 * Generic handler for endpoints that are not yet fully implemented.
 * Parses client headers and returns a placeholder response.
 *
 * @param res Pointer to uWS HttpResponse
 * @param req Pointer to uWS HttpRequest containing client headers
 * @param endpoint_name Name of the endpoint being handled
 */
void handleStub(auto *res, auto *req, const std::string& endpoint_name) {
    try {
        ClientInfo client = parseClientHeaders(req);

        json body = json::parse(req->getData());
        
        json response;
        response["message"] = endpoint_name + " endpoint received";
        sendSuccess(res, response);
        
    } catch (const json::exception& e) {
        sendError(res, std::string("Invalid JSON: ") + e.what());
    }
}

int main() {
    const int port = 2290;
    
    uWS::App().get("/*", [](auto *res, auto *req) {
        res->end("Ravenest Build and Battle Server v1.0");
    })
    
    .post("/api/login", [](auto *res, auto *req) {
        std::string buffer;
        res->onData([res, buffer = std::move(buffer)](std::string_view data, bool isLast) mutable {
            buffer += data;
            if (isLast) {
                handleLogin(res, req, buffer);
            }
        });
    })
    
    .post("/api/getPlayer", [](auto *res, auto *req) {
        std::string buffer;
        res->onData([res, buffer = std::move(buffer)](std::string_view data, bool isLast) mutable {
            buffer += data;
            if (isLast) {
                handleGetPlayer(res, req, buffer);
            }
        });
    })
    
    .post("/api/Build", [](auto *res, auto *req) {
        std::string buffer;
        res->onData([res, buffer = std::move(buffer)](std::string_view data, bool isLast) mutable {
            buffer += data;
            if (isLast) {
                handleStub(res, req, "Build");
            }
        });
    })
    
    .post("/api/getWorld", [](auto *res, auto *req) {
        std::string buffer;
        res->onData([res, buffer = std::move(buffer)](std::string_view data, bool isLast) mutable {
            buffer += data;
            if (isLast) {
                handleStub(res, req, "getWorld");
            }
        });
    })
    
    .post("/api/getFiefdom", [](auto *res, auto *req) {
        std::string buffer;
        res->onData([res, buffer = std::move(buffer)](std::string_view data, bool isLast) mutable {
            buffer += data;
            if (isLast) {
                handleStub(res, req, "getFiefdom");
            }
        });
    })
    
    .post("/api/sally", [](auto *res, auto *req) {
        std::string buffer;
        res->onData([res, buffer = std::move(buffer)](std::string_view data, bool isLast) mutable {
            buffer += data;
            if (isLast) {
                handleStub(res, req, "sally");
            }
        });
    })
    
    .post("/api/campaign", [](auto *res, auto *req) {
        std::string buffer;
        res->onData([res, buffer = std::move(buffer)](std::string_view data, bool isLast) mutable {
            buffer += data;
            if (isLast) {
                handleStub(res, req, "campaign");
            }
        });
    })
    
    .post("/api/hunt", [](auto *res, auto *req) {
        std::string buffer;
        res->onData([res, buffer = std::move(buffer)](std::string_view data, bool isLast) mutable {
            buffer += data;
            if (isLast) {
                handleStub(res, req, "hunt");
            }
        });
    })
    
    .listen(port, [port](auto *listenSocket) {
        if (listenSocket) {
            std::cout << "Ravenest Server listening on port " << port << std::endl;
            std::cout << "HTTPS termination should be handled by reverse proxy" << std::endl;
        } else {
            std::cerr << "Failed to bind to port " << port << std::endl;
        }
    })
    .run();
    
    return 0;
}