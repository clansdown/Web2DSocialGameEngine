#include <iostream>
#include <nlohmann/json.hpp>
#include <uWebSockets/App.hpp>
#include "Database.hpp"

using json = nlohmann::json;

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

void handleLogin(auto *res, auto *req, const std::string& data) {
    try {
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

void handleGetPlayer(auto *res, auto *req, const std::string& data) {
    try {
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

void handleStub(auto *res, auto *req, const std::string& endpoint_name) {
    try {
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