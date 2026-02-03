#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <string>
#include <functional>
#include <map>

namespace game {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct HttpResponse {
    int status_code;
    std::string body;
    std::map<std::string, std::string> headers;
    
    HttpResponse() : status_code(200) {
        headers["Content-Type"] = "application/json";
    }
};

using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

class WebServer {
public:
    WebServer(int port);
    ~WebServer();
    
    void registerHandler(const std::string& path, RequestHandler handler);
    void start();
    void stop();
    
private:
    int port_;
    int server_socket_;
    bool running_;
    std::map<std::string, RequestHandler> handlers_;
    
    void handleClient(int client_socket);
    HttpRequest parseRequest(const std::string& raw_request);
    std::string buildResponse(const HttpResponse& response);
};

} // namespace game

#endif // WEB_SERVER_H
