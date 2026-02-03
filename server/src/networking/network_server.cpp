#include "network_server.h"
#include "logger.h"

namespace Web2DEngine {
namespace Server {

NetworkServer::NetworkServer() {
}

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start(int port) {
    Utils::Logger::info("Starting network server on port...");
    // TODO: Create listening socket
    return true;
}

void NetworkServer::stop() {
    Utils::Logger::info("Stopping network server...");
    // TODO: Close all connections and listening socket
}

void NetworkServer::processEvents() {
    // TODO: Accept new connections
    // TODO: Receive data from clients
    // TODO: Handle disconnections
}

void NetworkServer::broadcast(const void* data, size_t size) {
    // TODO: Send data to all connected clients
}

} // namespace Server
} // namespace Web2DEngine
