#include "api_handler.h"
#include <sstream>

namespace game {

ApiHandler::ApiHandler(std::shared_ptr<GameDatabase> game_db, 
                       std::shared_ptr<MessageDatabase> msg_db)
    : game_db_(game_db), msg_db_(msg_db) {
}

HttpResponse ApiHandler::handleGameState(const HttpRequest& req) {
    HttpResponse response;
    
    if (req.method == "GET") {
        // Return game state
        response.body = R"({
            "status": "ok",
            "game_state": {
                "players": [],
                "world": {}
            }
        })";
    } else if (req.method == "POST") {
        // Update game state
        response.body = R"({
            "status": "ok",
            "message": "Game state updated"
        })";
    } else {
        response.status_code = 405;
        response.body = R"({"status": "error", "message": "Method not allowed"})";
    }
    
    return response;
}

HttpResponse ApiHandler::handleMessages(const HttpRequest& req) {
    HttpResponse response;
    
    if (req.method == "GET") {
        // Get messages
        response.body = R"({
            "status": "ok",
            "messages": []
        })";
    } else if (req.method == "POST") {
        // Send message
        response.body = R"({
            "status": "ok",
            "message": "Message sent"
        })";
    } else {
        response.status_code = 405;
        response.body = R"({"status": "error", "message": "Method not allowed"})";
    }
    
    return response;
}

HttpResponse ApiHandler::handlePlayerAction(const HttpRequest& req) {
    HttpResponse response;
    
    if (req.method == "POST") {
        // Process player action
        response.body = R"({
            "status": "ok",
            "message": "Action processed"
        })";
    } else {
        response.status_code = 405;
        response.body = R"({"status": "error", "message": "Method not allowed"})";
    }
    
    return response;
}

} // namespace game
