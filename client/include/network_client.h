#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include <string>

/**
 * @file network_client.h
 * @brief Client-side networking
 */

namespace Web2DEngine {
namespace Client {

/**
 * @brief Network client for connecting to game server
 */
class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();
    
    /**
     * @brief Connect to a game server
     * @param host Server hostname or IP address
     * @param port Server port
     * @return true if connected successfully, false otherwise
     */
    bool connect(const std::string& host, int port);
    
    /**
     * @brief Disconnect from server
     */
    void disconnect();
    
    /**
     * @brief Send data to server
     * @param data Data to send
     * @param size Size of data in bytes
     * @return true if sent successfully, false otherwise
     */
    bool send(const void* data, size_t size);
    
    /**
     * @brief Receive data from server
     * @param buffer Buffer to store received data
     * @param maxSize Maximum size to receive
     * @return Number of bytes received
     */
    int receive(void* buffer, size_t maxSize);
    
private:
    bool connected;
};

} // namespace Client
} // namespace Web2DEngine

#endif // NETWORK_CLIENT_H
