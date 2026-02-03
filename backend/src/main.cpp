#include "web_server.h"
#include "database.h"
#include "api_handler.h"
#include <iostream>
#include <memory>
#include <csignal>

using namespace game;

static WebServer* g_server = nullptr;

void signalHandler(int signum) {
    std::cout << "\nShutting down server..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    exit(signum);
}

int main(int argc, char* argv[]) {
    std::cout << "Web2D Social Game Engine - Backend Server" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    // Initialize databases
    auto game_db = std::make_shared<GameDatabase>("db/game_data.db");
    auto msg_db = std::make_shared<MessageDatabase>("db/messages.db");
    
    if (!game_db->init()) {
        std::cerr << "Failed to initialize game database" << std::endl;
        return 1;
    }
    
    if (!msg_db->init()) {
        std::cerr << "Failed to initialize message database" << std::endl;
        return 1;
    }
    
    std::cout << "Databases initialized successfully" << std::endl;
    
    // Create API handler
    auto api_handler = std::make_shared<ApiHandler>(game_db, msg_db);
    
    // Create and configure web server
    int port = 8080;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    
    WebServer server(port);
    g_server = &server;
    
    // Register signal handler
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Register API endpoints
    server.registerHandler("/api/game_state", 
        [api_handler](const HttpRequest& req) { 
            return api_handler->handleGameState(req); 
        });
    
    server.registerHandler("/api/messages", 
        [api_handler](const HttpRequest& req) { 
            return api_handler->handleMessages(req); 
        });
    
    server.registerHandler("/api/player_action", 
        [api_handler](const HttpRequest& req) { 
            return api_handler->handlePlayerAction(req); 
        });
    
    // Start server
    std::cout << "Starting server on port " << port << "..." << std::endl;
    server.start();
    
    return 0;
}
