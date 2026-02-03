#ifndef NETWORK_SERVER_H
#define NETWORK_SERVER_H

#include <cstddef>

/**
 * @file network_server.h
 * @brief Server-side networking
 */

namespace Web2DEngine {
namespace Server {

/**
 * @brief Network server for handling client connections
 */
class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();
    
    /**
     * @brief Start listening for connections
     * @param port Port to listen on
     * @return true if started successfully, false otherwise
     */
    bool start(int port);
    
    /**
     * @brief Stop the server
     */
    void stop();
    
    /**
     * @brief Process network events (accept connections, receive data)
     */
    void processEvents();
    
    /**
     * @brief Broadcast data to all connected clients
     * @param data Data to send
     * @param size Size of data in bytes
     */
    void broadcast(const void* data, size_t size);
};

} // namespace Server
} // namespace Web2DEngine

#endif // NETWORK_SERVER_H
