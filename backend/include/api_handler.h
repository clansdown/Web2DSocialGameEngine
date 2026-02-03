#ifndef API_HANDLER_H
#define API_HANDLER_H

#include "web_server.h"
#include "database.h"
#include <memory>

namespace game {

class ApiHandler {
public:
    ApiHandler(std::shared_ptr<GameDatabase> game_db, 
               std::shared_ptr<MessageDatabase> msg_db);
    
    HttpResponse handleGameState(const HttpRequest& req);
    HttpResponse handleMessages(const HttpRequest& req);
    HttpResponse handlePlayerAction(const HttpRequest& req);
    
private:
    std::shared_ptr<GameDatabase> game_db_;
    std::shared_ptr<MessageDatabase> msg_db_;
};

} // namespace game

#endif // API_HANDLER_H
