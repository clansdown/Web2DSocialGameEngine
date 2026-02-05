#pragma once
#include <sqlite_modern_cpp/sqlite_modern_cpp.h>
#include <memory>
#include <string>
#include <sys/stat.h>

class Database {
public:
    static Database& getInstance() {
        static Database instance;
        return instance;
    }

    void init(const std::string& game_db_path, const std::string& messages_db_path) {
        ensure_directory(game_db_path);
        ensure_directory(messages_db_path);

        game_db = sqlite::database(game_db_path);
        messages_db = sqlite::database(messages_db_path);
        initializeSchemas();
    }

    sqlite::database& gameDB() {
        return game_db;
    }

    sqlite::database& messagesDB() {
        return messages_db;
    }

private:
    Database()
        : game_db("game.db")
        , messages_db("messages.db") {
    }

    void ensure_directory(const std::string& path) {
        size_t last_slash = path.find_last_of('/');
        if (last_slash != std::string::npos) {
            std::string dir = path.substr(0, last_slash);
            mkdir(dir.c_str(), 0755);
        }
    }

    void initializeSchemas() {
        game_db << "CREATE TABLE IF NOT EXISTS users ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "username TEXT UNIQUE NOT NULL,"
                    "password_hash TEXT NOT NULL,"
                    "created_at INTEGER NOT NULL,"
                    "adult INTEGER NOT NULL DEFAULT 0,"
                    "displayName TEXT,"
                    "safeDisplayName TEXT NOT NULL"
                    ");";

        game_db << "CREATE TABLE IF NOT EXISTS players ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "user_id INTEGER NOT NULL,"
                   "name TEXT NOT NULL,"
                   "level INTEGER DEFAULT 1,"
                   "FOREIGN KEY(user_id) REFERENCES users(id)"
                   ");";

        game_db << "CREATE TABLE IF NOT EXISTS fiefdoms ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "owner_id INTEGER NOT NULL,"
                   "name TEXT NOT NULL,"
                   "x INTEGER NOT NULL,"
                   "y INTEGER NOT NULL,"
                   "FOREIGN KEY(owner_id) REFERENCES players(id)"
                   ");";

        game_db << "CREATE INDEX IF NOT EXISTS idx_players_user_id ON players(user_id);";
        game_db << "CREATE INDEX IF NOT EXISTS idx_fiefdoms_owner ON fiefdoms(owner_id);";

        messages_db << "CREATE TABLE IF NOT EXISTS player_messages ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "from_player_id INTEGER NOT NULL,"
                       "to_player_id INTEGER NOT NULL,"
                       "message TEXT NOT NULL,"
                       "timestamp INTEGER NOT NULL,"
                       "read INTEGER DEFAULT 0"
                       ");";

        messages_db << "CREATE TABLE IF NOT EXISTS message_queues ("
                       "player_id INTEGER PRIMARY KEY NOT NULL,"
                       "unread_count INTEGER DEFAULT 0"
                       ");";

        messages_db << "CREATE INDEX IF NOT EXISTS idx_messages_to_player ON player_messages(to_player_id);";
        messages_db << "CREATE INDEX IF NOT EXISTS idx_messages_from_player ON player_messages(from_player_id);";
        messages_db << "CREATE INDEX IF NOT EXISTS idx_messages_timestamp ON player_messages(timestamp);";
    }

private:
    sqlite::database game_db;
    sqlite::database messages_db;
};