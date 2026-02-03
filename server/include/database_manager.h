#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <string>

/**
 * @file database_manager.h
 * @brief Database management for persistent storage
 */

namespace Web2DEngine {
namespace Server {

/**
 * @brief Manages database operations for player data and game state
 */
class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();
    
    /**
     * @brief Connect to the database
     * @param connectionString Database connection string
     * @return true if connected successfully, false otherwise
     */
    bool connect(const std::string& connectionString);
    
    /**
     * @brief Disconnect from the database
     */
    void disconnect();
    
    /**
     * @brief Execute a query
     * @param query SQL query string
     * @return true if executed successfully, false otherwise
     */
    bool executeQuery(const std::string& query);
};

} // namespace Server
} // namespace Web2DEngine

#endif // DATABASE_MANAGER_H
