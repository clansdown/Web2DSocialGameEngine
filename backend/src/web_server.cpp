#include "web_server.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>

namespace game {

WebServer::WebServer(int port) 
    : port_(port), server_socket_(-1), running_(false) {
}

WebServer::~WebServer() {
    stop();
}

void WebServer::registerHandler(const std::string& path, RequestHandler handler) {
    handlers_[path] = handler;
}

void WebServer::start() {
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }
    
    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);
    
    if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind socket" << std::endl;
        close(server_socket_);
        return;
    }
    
    if (listen(server_socket_, 10) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(server_socket_);
        return;
    }
    
    running_ = true;
    std::cout << "Server started on port " << port_ << std::endl;
    
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            if (running_) {
                std::cerr << "Failed to accept connection" << std::endl;
            }
            continue;
        }
        
        std::thread(&WebServer::handleClient, this, client_socket).detach();
    }
}

void WebServer::stop() {
    running_ = false;
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
}

void WebServer::handleClient(int client_socket) {
    char buffer[4096] = {0};
    int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        HttpRequest request = parseRequest(std::string(buffer, bytes_read));
        
        // Route to appropriate handler
        HttpResponse response;
        auto handler_it = handlers_.find(request.path);
        
        if (handler_it != handlers_.end()) {
            // Call registered handler
            response = handler_it->second(request);
        } else {
            // Default response for unknown paths
            response.status_code = 200;
            response.body = R"({"status":"ok","message":"Web2D Game Server"})";
        }
        
        std::string response_str = buildResponse(response);
        write(client_socket, response_str.c_str(), response_str.length());
    }
    
    close(client_socket);
}

HttpRequest WebServer::parseRequest(const std::string& raw_request) {
    HttpRequest request;
    std::istringstream stream(raw_request);
    std::string line;
    
    // Parse request line
    if (std::getline(stream, line)) {
        std::istringstream line_stream(line);
        line_stream >> request.method >> request.path;
    }
    
    // Parse headers
    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 2);
            if (!value.empty() && value.back() == '\r') {
                value.pop_back();
            }
            request.headers[key] = value;
        }
    }
    
    // Parse body
    std::string body;
    while (std::getline(stream, line)) {
        body += line;
    }
    request.body = body;
    
    return request;
}

std::string WebServer::buildResponse(const HttpResponse& response) {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << response.status_code << " OK\r\n";
    
    for (const auto& header : response.headers) {
        stream << header.first << ": " << header.second << "\r\n";
    }
    
    stream << "Content-Length: " << response.body.length() << "\r\n";
    stream << "Connection: close\r\n";
    stream << "\r\n";
    stream << response.body;
    
    return stream.str();
}

} // namespace game
