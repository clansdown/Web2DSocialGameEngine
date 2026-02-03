#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <sqlite3.h>
#include <memory>

namespace game {

class Database {
public:
    Database(const std::string& db_path);
    ~Database();
    
    bool init();
    bool execute(const std::string& sql);
    sqlite3_stmt* prepare(const std::string& sql);
    void finalize(sqlite3_stmt* stmt);
    
    sqlite3* getHandle() const { return db_; }
    
private:
    std::string db_path_;
    sqlite3* db_;
};

class GameDatabase {
public:
    GameDatabase(const std::string& db_path);
    ~GameDatabase();
    
    bool init();
    
private:
    std::unique_ptr<Database> db_;
};

class MessageDatabase {
public:
    MessageDatabase(const std::string& db_path);
    ~MessageDatabase();
    
    bool init();
    
private:
    std::unique_ptr<Database> db_;
};

} // namespace game

#endif // DATABASE_H
