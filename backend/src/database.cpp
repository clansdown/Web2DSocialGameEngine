#include "database.h"
#include <iostream>

namespace game {

Database::Database(const std::string& db_path) 
    : db_path_(db_path), db_(nullptr) {
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
    }
}

bool Database::init() {
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    return true;
}

bool Database::execute(const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

sqlite3_stmt* Database::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << std::endl;
        return nullptr;
    }
    return stmt;
}

void Database::finalize(sqlite3_stmt* stmt) {
    if (stmt) {
        sqlite3_finalize(stmt);
    }
}

// GameDatabase implementation
GameDatabase::GameDatabase(const std::string& db_path) 
    : db_(std::make_unique<Database>(db_path)) {
}

GameDatabase::~GameDatabase() = default;

bool GameDatabase::init() {
    if (!db_->init()) {
        return false;
    }
    
    // Create game tables
    const char* create_players_table = R"(
        CREATE TABLE IF NOT EXISTS players (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            position_x REAL DEFAULT 0,
            position_y REAL DEFAULT 0,
            score INTEGER DEFAULT 0,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";
    
    const char* create_game_state_table = R"(
        CREATE TABLE IF NOT EXISTS game_state (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            player_id INTEGER,
            state_data TEXT,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (player_id) REFERENCES players(id)
        );
    )";
    
    return db_->execute(create_players_table) && 
           db_->execute(create_game_state_table);
}

// MessageDatabase implementation
MessageDatabase::MessageDatabase(const std::string& db_path) 
    : db_(std::make_unique<Database>(db_path)) {
}

MessageDatabase::~MessageDatabase() = default;

bool MessageDatabase::init() {
    if (!db_->init()) {
        return false;
    }
    
    // Create message tables
    const char* create_messages_table = R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sender_id INTEGER,
            receiver_id INTEGER,
            message TEXT NOT NULL,
            is_read INTEGER DEFAULT 0,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";
    
    const char* create_chat_rooms_table = R"(
        CREATE TABLE IF NOT EXISTS chat_rooms (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            room_name TEXT UNIQUE NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";
    
    return db_->execute(create_messages_table) && 
           db_->execute(create_chat_rooms_table);
}

} // namespace game
