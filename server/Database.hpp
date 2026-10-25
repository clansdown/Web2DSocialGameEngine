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
              const std::string& staff_db_path, const std::string& invites_db_path) {
        ensure_directory(game_db_path);
        ensure_directory(chat_db_path);
        ensure_directory(staff_db_path);

        game_db = sqlite::database(game_db_path);
        chat_db = sqlite::database(chat_db_path);
        staff_db = sqlite::database(staff_db_path);

        // invites.db is intentionally lazy: the registration gate can be off
        // for the whole life of a deployment, and no invite file should exist.
        invites_db.reset();
        invites_db_path_ = invites_db_path;
    }

    // Backwards-compatible three-database init (invites.db lives beside them).
    void init(const std::string& game_db_path, const std::string& chat_db_path,
              const std::string& staff_db_path) {
        init(game_db_path, chat_db_path, staff_db_path,
             sibling_path(game_db_path, "invites.db"));
    }

    // Backwards-compatible two-database init (staff.db/invites.db live beside them).
    void init(const std::string& game_db_path, const std::string& chat_db_path) {
        init(game_db_path, chat_db_path, sibling_path(game_db_path, "staff.db"));
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

    // Opens invites.db on first use. Registration code never calls this in
    // `off` mode, so the file stays absent until an invite is actually needed.
    sqlite::database& invitesDB() {
        ensureInvitesDB();
        return *invites_db;
    }

    void ensureInvitesDB() {
        if (invites_db) return;
        if (invites_db_path_.empty()) {
            invites_db_path_ = "invites.db";
        }
        ensure_directory(invites_db_path_);
        invites_db = std::make_unique<sqlite::database>(invites_db_path_);
    }

    bool invitesDBOpen() const {
        return invites_db != nullptr;
    }

    const std::string& invitesDBPath() const {
        return invites_db_path_;
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

    static std::string sibling_path(const std::string& base_path, const std::string& filename) {
        const size_t slash = base_path.find_last_of('/');
        if (slash == std::string::npos) return filename;
        return base_path.substr(0, slash + 1) + filename;
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
    std::string invites_db_path_;
    std::unique_ptr<sqlite::database> invites_db;
};
