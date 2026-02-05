#pragma once
#include <sqlite_modern_cpp.h>
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

private:
    sqlite::database game_db;
    sqlite::database messages_db;
};