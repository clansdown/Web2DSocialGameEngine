#include "init_db.hpp"
#include <iostream>
#include <sqlite_modern_cpp.h>

namespace {

    void createTable(sqlite::database& db, const std::string& table_name, const std::string& schema) {
        db << "CREATE TABLE IF NOT EXISTS " + table_name + " (" + schema + ");";
    }

    void ensureIndex(sqlite::database& db, const std::string& index_name, const std::string& table_name, const std::string& columns, const std::string& extra = "") {
        std::string sql = "CREATE INDEX IF NOT EXISTS " + index_name + " ON " + table_name + " (" + columns + ")";
        if (!extra.empty()) {
            sql += " " + extra;
        }
        sql += ";";
        db << sql;
    }

    void ensureGameDBIndexes_private(sqlite::database& db) {
        ensureIndex(db, "idx_characters_user_id", "characters", "user_id");
        ensureIndex(db, "idx_fiefdoms_owner", "fiefdoms", "owner_id");
    }

    void ensureMessagesDBIndexes_private(sqlite::database& db) {
        ensureIndex(db, "idx_messages_to_character", "player_messages", "to_character_id");
        ensureIndex(db, "idx_messages_from_character", "player_messages", "from_character_id");
        ensureIndex(db, "idx_messages_timestamp", "player_messages", "timestamp");
    }

    void createGameDBTables(sqlite::database& db) {
        createTable(db, "users",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "username TEXT UNIQUE NOT NULL,"
            "password_hash TEXT NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "adult INTEGER NOT NULL DEFAULT 0"
        );

        createTable(db, "characters",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "user_id INTEGER NOT NULL,"
            "display_name TEXT NOT NULL,"
            "safe_display_name TEXT NOT NULL,"
            "level INTEGER DEFAULT 1,"
            "FOREIGN KEY(user_id) REFERENCES users(id)"
        );

        createTable(db, "fiefdoms",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "owner_id INTEGER NOT NULL,"
            "name TEXT NOT NULL,"
            "x INTEGER NOT NULL,"
            "y INTEGER NOT NULL,"
            "FOREIGN KEY(owner_id) REFERENCES characters(id)"
        );
    }

    void createMessagesDBTables(sqlite::database& db) {
        createTable(db, "player_messages",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "from_character_id INTEGER NOT NULL,"
            "to_character_id INTEGER NOT NULL,"
            "message TEXT NOT NULL,"
            "timestamp INTEGER NOT NULL,"
            "read INTEGER DEFAULT 0"
        );

        createTable(db, "message_queues",
            "character_id INTEGER PRIMARY KEY NOT NULL,"
            "unread_count INTEGER DEFAULT 0"
        );
    }

}

void initializeGameDB(sqlite::database& db) {
    createGameDBTables(db);
    ensureGameDBIndexes_private(db);
}

void initializeMessagesDB(sqlite::database& db) {
    createMessagesDBTables(db);
    ensureMessagesDBIndexes_private(db);
}

void ensureGameDBIndexes(sqlite::database& db) {
    ensureGameDBIndexes_private(db);
}

void ensureMessagesDBIndexes(sqlite::database& db) {
    ensureMessagesDBIndexes_private(db);
}

void initializeAllDatabases(sqlite::database& game_db, sqlite::database& messages_db) {
    initializeGameDB(game_db);
    initializeMessagesDB(messages_db);
}