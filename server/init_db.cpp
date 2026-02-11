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
        ensureIndex(db, "idx_fiefdom_buildings_fiefdom", "fiefdom_buildings", "fiefdom_id");
        ensureIndex(db, "idx_fiefdom_buildings_fiefdom_xy", "fiefdom_buildings", "fiefdom_id, x, y");
        ensureIndex(db, "idx_officials_fiefdom", "officials", "fiefdom_id");
        ensureIndex(db, "idx_fiefdom_heroes_fiefdom", "fiefdom_heroes", "fiefdom_id");
        ensureIndex(db, "idx_stationed_combatants_fiefdom", "stationed_combatants", "fiefdom_id");
        ensureIndex(db, "idx_fiefdom_walls_fiefdom", "fiefdom_walls", "fiefdom_id");
        ensureIndex(db, "idx_fiefdom_walls_fiefdom_gen", "fiefdom_walls", "fiefdom_id, generation");
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
            "peasants INTEGER NOT NULL DEFAULT 0,"
            "gold INTEGER NOT NULL DEFAULT 0,"
            "grain INTEGER NOT NULL DEFAULT 0,"
            "wood INTEGER NOT NULL DEFAULT 0,"
            "steel INTEGER NOT NULL DEFAULT 0,"
            "bronze INTEGER NOT NULL DEFAULT 0,"
            "stone INTEGER NOT NULL DEFAULT 0,"
            "leather INTEGER NOT NULL DEFAULT 0,"
            "mana INTEGER NOT NULL DEFAULT 0,"
            "wall_count INTEGER NOT NULL DEFAULT 0,"
            "morale REAL NOT NULL DEFAULT 0,"
            "last_update_time INTEGER NOT NULL DEFAULT 0,"
            "FOREIGN KEY(owner_id) REFERENCES characters(id)"
        );

        createTable(db, "fiefdom_buildings",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "fiefdom_id INTEGER NOT NULL,"
            "name TEXT NOT NULL,"
            "level INTEGER NOT NULL DEFAULT 0,"
            "x INTEGER NOT NULL DEFAULT 0,"
            "y INTEGER NOT NULL DEFAULT 0,"
            "construction_start_ts INTEGER NOT NULL DEFAULT 0,"
            "last_updated INTEGER NOT NULL DEFAULT 0,"
            "action_start_ts INTEGER NOT NULL DEFAULT 0,"
            "action_tag TEXT NOT NULL DEFAULT '',"
            "FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)"
        );

        createTable(db, "officials",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "fiefdom_id INTEGER NOT NULL,"
            "role TEXT NOT NULL,"
            "template_id TEXT NOT NULL,"
            "portrait_id INTEGER NOT NULL,"
            "name TEXT NOT NULL,"
            "level INTEGER NOT NULL DEFAULT 1,"
            "intelligence INTEGER NOT NULL,"
            "charisma INTEGER NOT NULL,"
            "wisdom INTEGER NOT NULL,"
            "diligence INTEGER NOT NULL,"
            "FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)"
        );

        createTable(db, "fiefdom_heroes",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "fiefdom_id INTEGER NOT NULL,"
            "hero_config_id TEXT NOT NULL,"
            "level INTEGER NOT NULL DEFAULT 1,"
            "FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)"
        );

        createTable(db, "stationed_combatants",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "fiefdom_id INTEGER NOT NULL,"
            "combatant_config_id TEXT NOT NULL,"
            "level INTEGER NOT NULL DEFAULT 1,"
            "FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)"
        );

        createTable(db, "fiefdom_walls",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "fiefdom_id INTEGER NOT NULL,"
            "generation INTEGER NOT NULL,"
            "level INTEGER NOT NULL DEFAULT 1,"
            "hp INTEGER NOT NULL DEFAULT 0,"
            "construction_start_ts INTEGER NOT NULL DEFAULT 0,"
            "last_updated INTEGER NOT NULL DEFAULT 0,"
            "FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id),"
            "UNIQUE(fiefdom_id, generation)"
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