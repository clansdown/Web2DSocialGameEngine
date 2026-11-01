#pragma once

#include "db/postgres_sqlite_compat.hpp"

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

// Process-wide database handles. Each handle is a PostgreSQL connection pool:
// one connection per worker thread, opened lazily. The four logical databases
// remain independent; there are no cross-database joins or transactions.
class Database {
public:
    static Database& getInstance() {
        static Database instance;
        return instance;
    }

    // The path arguments are kept for source compatibility with the old SQLite
    // call sites. They are ignored: connection URLs come from
    // RAVENEST_{GAME,CHAT,STAFF,INVITES}_DB_URL.
    void init(const std::string& game_db_path, const std::string& chat_db_path,
              const std::string& staff_db_path, const std::string& invites_db_path) {
        (void)game_db_path;
        (void)chat_db_path;
        (void)staff_db_path;
        (void)invites_db_path;
        init_from_env();
    }

    void init(const std::string& game_db_path, const std::string& chat_db_path,
              const std::string& staff_db_path) {
        init(game_db_path, chat_db_path, staff_db_path, std::string());
    }

    void init(const std::string& game_db_path, const std::string& chat_db_path) {
        init(game_db_path, chat_db_path, std::string());
    }

    void init_from_env() {
        game_db = sqlite::database(env_url("RAVENEST_GAME_DB_URL", "ravenest_game"));
        chat_db = sqlite::database(env_url("RAVENEST_CHAT_DB_URL", "ravenest_chat"));
        staff_db = sqlite::database(env_url("RAVENEST_STAFF_DB_URL", "ravenest_staff"));
        invites_conninfo_ = env_url("RAVENEST_INVITES_DB_URL", "ravenest_invites");
        invites_db.reset();
    }

    // CLI tools that only touch invitation codes do not need the other three
    // connection URLs.
    void init_invites_from_env() {
        invites_conninfo_ = env_url("RAVENEST_INVITES_DB_URL", "ravenest_invites");
        invites_db.reset();
    }

    sqlite::database& gameDB() { return game_db; }
    sqlite::database& chatDB() { return chat_db; }
    sqlite::database& staffDB() { return staff_db; }

    // Opens the invites handle on first use. In `off` registration mode no
    // invite operation runs, so the connection is never opened.
    sqlite::database& invitesDB() {
        ensureInvitesDB();
        return *invites_db;
    }

    void ensureInvitesDB() {
        if (invites_db) return;
        invites_db = std::make_unique<sqlite::database>(invites_conninfo_);
    }

    bool invitesDBOpen() const { return invites_db != nullptr; }
    const std::string& invitesDBPath() const { return invites_conninfo_; }

private:
    Database() = default;

    static std::string env_url(const char* variable, const char* database_name) {
        (void)database_name;
        if (const char* value = std::getenv(variable); value && *value) {
            return value;
        }
        throw std::runtime_error(std::string("missing database URL: ") + variable);
    }

    sqlite::database game_db;
    sqlite::database chat_db;
    sqlite::database staff_db;
    std::string invites_conninfo_ = "dbname=ravenest_invites";
    std::unique_ptr<sqlite::database> invites_db;
};
