#ifndef CLIENT_H
#define CLIENT_H

/**
 * @file client.h
 * @brief Main client header file for the Web2D Social Game Engine
 */

namespace Web2DEngine {
namespace Client {

/**
 * @brief Main client class that manages the game client
 */
class Client {
public:
    Client();
    ~Client();
    
    /**
     * @brief Initialize the client
     * @return true if successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Start the client main loop
     */
    void run();
    
    /**
     * @brief Shutdown the client
     */
    void shutdown();
    
private:
    bool running;
};

} // namespace Client
} // namespace Web2DEngine

#endif // CLIENT_H
