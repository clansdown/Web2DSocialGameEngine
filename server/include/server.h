#ifndef SERVER_H
#define SERVER_H

/**
 * @file server.h
 * @brief Main server header file for the Web2D Social Game Engine
 */

namespace Web2DEngine {
namespace Server {

/**
 * @brief Main server class that manages the game server
 */
class Server {
public:
    Server();
    ~Server();
    
    /**
     * @brief Initialize the server
     * @param port Port to listen on
     * @return true if successful, false otherwise
     */
    bool initialize(int port);
    
    /**
     * @brief Start the server main loop
     */
    void run();
    
    /**
     * @brief Shutdown the server
     */
    void shutdown();
    
private:
    bool running;
    int port;
};

} // namespace Server
} // namespace Web2DEngine

#endif // SERVER_H
