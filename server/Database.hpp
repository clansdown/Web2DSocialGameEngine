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

    void init(const std::string& game_db_path, const std::string& chat_db_path,
              const std::string& staff_db_path) {
        ensure_directory(game_db_path);
        ensure_directory(chat_db_path);
        ensure_directory(staff_db_path);

        game_db = sqlite::database(game_db_path);
        chat_db = sqlite::database(chat_db_path);
        staff_db = sqlite::database(staff_db_path);
    }

    // Backwards-compatible two-database init (staff.db lives beside them).
    void init(const std::string& game_db_path, const std::string& chat_db_path) {
        std::string staff_path = "staff.db";
        const size_t slash = chat_db_path.find_last_of('/');
        if (slash != std::string::npos) {
            staff_path = chat_db_path.substr(0, slash + 1) + "staff.db";
        }
        init(game_db_path, chat_db_path, staff_path);
    }

    sqlite::database& gameDB() {
        return game_db;
    }

    sqlite::database& chatDB() {
        return chat_db;
    }

    sqlite::database& staffDB() {
        return staff_db;
    }

private:
    Database()
        : game_db(":memory:")
        , chat_db(":memory:")
        , staff_db(":memory:") {
        // init() reassigns these to the real --db-dir paths. Keeping the
        // pre-init handles in memory prevents short-lived CLI tools (which may
        // run from the repository root) from creating stray game.db/chat.db/
        // staff.db files before init() is called.
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
    sqlite::database chat_db;
    sqlite::database staff_db;
};
