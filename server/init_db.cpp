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
    ensureIndex(db, "idx_fiefdom_river_fiefdom", "fiefdom_river", "fiefdom_id");
    ensureIndex(db, "idx_player_game_state_character", "player_game_state", "character_id");
    ensureIndex(db, "idx_mini_game_progress_character", "mini_game_progress", "character_id, mini_game");
    ensureIndex(db, "idx_td_player_unlocks_character", "td_player_unlocks", "character_id");
    ensureIndex(db, "idx_baronies_owner", "baronies", "owner_character_id");
    ensureIndex(db, "idx_barony_members_barony", "barony_members", "barony_id");
    ensureIndex(db, "idx_barony_members_character", "barony_members", "character_id");
    ensureIndex(db, "idx_game_sessions_character", "game_sessions", "character_id");
    ensureIndex(db, "idx_weeding_sessions_character", "weeding_sessions", "character_id");
    ensureIndex(db, "idx_retinue_members_character", "retinue_members", "character_id");
    ensureIndex(db, "idx_fiefdom_armory_fiefdom", "fiefdom_armory", "fiefdom_id");
    ensureIndex(db, "idx_fiefdom_armory_member", "fiefdom_armory", "member_id");
    }

    void ensureMessagesDBIndexes_private(sqlite::database& db) {
        ensureIndex(db, "idx_messages_to_character", "player_messages", "to_character_id");
        ensureIndex(db, "idx_messages_from_character", "player_messages", "from_character_id");
        ensureIndex(db, "idx_messages_timestamp", "player_messages", "timestamp");
    }

    void migrate_character_archetype(sqlite::database& db) {
        try {
            db << "ALTER TABLE characters ADD COLUMN archetype TEXT;";
        } catch (const std::exception&) {
            // Column already exists — ignore
        }
    }

    void migrate_character_sex(sqlite::database& db) {
        try {
            db << "ALTER TABLE characters ADD COLUMN sex TEXT;";
        } catch (const std::exception&) {
            // Column already exists — ignore
        }
    }

    void migrate_manor_level(sqlite::database& db) {
        try {
            db << "ALTER TABLE fiefdoms ADD COLUMN manor_level INTEGER NOT NULL DEFAULT 1;";
        } catch (const std::exception&) {
            // Column already exists — ignore
        }
    }

void migrate_import_settings(sqlite::database& db) {
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN import_settings TEXT NOT NULL DEFAULT "
              "'{\"grain\":true,\"wood\":true,\"steel\":true,\"bronze\":true,"
              "\"stone\":true,\"leather\":true,\"mana\":true,\"charcoal\":true,\"iron\":true,\"ironwork\":true}';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_land_patent_acknowledged(sqlite::database& db) {
    try {
        db << "ALTER TABLE player_game_state ADD COLUMN land_patent_acknowledged INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_honor_name(sqlite::database& db) {
    try {
        db << "ALTER TABLE player_game_state ADD COLUMN honor_name TEXT;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_silver_pence(sqlite::database& db) {
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN silver_pence INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_fancy_ironwork(sqlite::database& db) {
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN fancy_ironwork INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_beams_boards(sqlite::database& db) {
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN beams INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN boards INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_building_output_rates(sqlite::database& db) {
    try {
        db << "ALTER TABLE fiefdom_buildings ADD COLUMN output_rates TEXT NOT NULL DEFAULT '{}';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_pond_type(sqlite::database& db) {
    try {
        db << "ALTER TABLE fiefdom_buildings ADD COLUMN pond_type TEXT NOT NULL DEFAULT '';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_retinue_gender(sqlite::database& db) {
    try {
        db << "ALTER TABLE retinue_members ADD COLUMN gender TEXT NOT NULL DEFAULT 'male';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_retinue_health(sqlite::database& db) {
    try {
        db << "ALTER TABLE retinue_members ADD COLUMN health REAL NOT NULL DEFAULT 100;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE retinue_members ADD COLUMN health_updated INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_retinue_priority(sqlite::database& db) {
    try {
        db << "ALTER TABLE retinue_members ADD COLUMN priority INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_retinue_equipment(sqlite::database& db) {
    try {
        db << "ALTER TABLE retinue_members ADD COLUMN equipment TEXT NOT NULL DEFAULT '{}';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_retinue_maintained(sqlite::database& db) {
    try {
        db << "ALTER TABLE retinue_members ADD COLUMN maintained INTEGER NOT NULL DEFAULT 1;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_fiefdom_morale_ts(sqlite::database& db) {
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN last_victory_ts INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN last_defeat_ts INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_building_tech_actions(sqlite::database& db) {
    try {
        db << "ALTER TABLE fiefdom_buildings ADD COLUMN tech_xp REAL NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE fiefdom_buildings ADD COLUMN tech_nodes TEXT NOT NULL DEFAULT '[]';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE fiefdom_buildings ADD COLUMN forge_order TEXT NOT NULL DEFAULT '';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE fiefdom_buildings ADD COLUMN training TEXT NOT NULL DEFAULT '';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_charcoal_iron(sqlite::database& db) {
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN charcoal INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN iron INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN ironwork INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_reserves(sqlite::database& db) {
    try {
        db << "ALTER TABLE fiefdoms ADD COLUMN reserves TEXT NOT NULL DEFAULT '{}';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_baron_character_id(sqlite::database& db) {
    try {
        db << "ALTER TABLE baronies ADD COLUMN baron_character_id INTEGER;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

    void migrate_barony_tables(sqlite::database& db) {
        try {
            db << "DROP TABLE IF EXISTS dukedom_members;";
            db << "DROP TABLE IF EXISTS dukedoms;";
        } catch (const std::exception&) {
            // Tables may not exist — ignore
        }
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
            "gold INTEGER NOT NULL DEFAULT 0,"
            "silver_pence REAL NOT NULL DEFAULT 0,"
            "grain INTEGER NOT NULL DEFAULT 0,"
            "wood INTEGER NOT NULL DEFAULT 0,"
            "steel INTEGER NOT NULL DEFAULT 0,"
            "bronze INTEGER NOT NULL DEFAULT 0,"
            "stone INTEGER NOT NULL DEFAULT 0,"
            "leather INTEGER NOT NULL DEFAULT 0,"
            "mana INTEGER NOT NULL DEFAULT 0,"
            "charcoal INTEGER NOT NULL DEFAULT 0,"
            "iron INTEGER NOT NULL DEFAULT 0,"
            "ironwork INTEGER NOT NULL DEFAULT 0,"
            "fancy_ironwork INTEGER NOT NULL DEFAULT 0,"
            "beams INTEGER NOT NULL DEFAULT 0,"
            "boards INTEGER NOT NULL DEFAULT 0,"
            "wall_count INTEGER NOT NULL DEFAULT 0,"
            "morale REAL NOT NULL DEFAULT 0,"
            "last_update_time INTEGER NOT NULL DEFAULT 0,"
            "reserves TEXT NOT NULL DEFAULT '{}',"
            "last_victory_ts INTEGER NOT NULL DEFAULT 0,"
            "last_defeat_ts INTEGER NOT NULL DEFAULT 0,"
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
            "output_rates TEXT NOT NULL DEFAULT '{}',"
            "tech_xp REAL NOT NULL DEFAULT 0,"
            "tech_nodes TEXT NOT NULL DEFAULT '[]',"
            "forge_order TEXT NOT NULL DEFAULT '',"
            "training TEXT NOT NULL DEFAULT '',"
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

        createTable(db, "retinue_members",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "display_name TEXT NOT NULL,"
            "unit_class TEXT NOT NULL,"
            "is_knight INTEGER NOT NULL DEFAULT 0,"
            "level INTEGER NOT NULL DEFAULT 1,"
            "gender TEXT NOT NULL DEFAULT 'male',"
            "health REAL NOT NULL DEFAULT 100,"
            "health_updated INTEGER NOT NULL DEFAULT 0,"
            "priority INTEGER NOT NULL DEFAULT 0,"
            "weapons TEXT NOT NULL DEFAULT '{}',"
            "armor TEXT NOT NULL DEFAULT '{}',"
            "equipment TEXT NOT NULL DEFAULT '{}',"
            "abilities TEXT NOT NULL DEFAULT '[]',"
            "status TEXT NOT NULL DEFAULT 'active',"
            "maintained INTEGER NOT NULL DEFAULT 1,"
            "created_at INTEGER NOT NULL,"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        createTable(db, "fiefdom_armory",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "fiefdom_id INTEGER NOT NULL,"
            "item_id TEXT NOT NULL,"
            "member_id INTEGER,"
            "created_at INTEGER NOT NULL,"
            "FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)"
        );

        createTable(db, "fiefdom_storage",
            "fiefdom_id INTEGER NOT NULL,"
            "item_id TEXT NOT NULL,"
            "count INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY(fiefdom_id, item_id),"
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

        createTable(db, "fiefdom_river",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "fiefdom_id INTEGER NOT NULL,"
            "x INTEGER NOT NULL,"
            "y INTEGER NOT NULL,"
            "FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id),"
            "UNIQUE(fiefdom_id, x, y)"
        );

        createTable(db, "player_game_state",
            "character_id INTEGER PRIMARY KEY NOT NULL,"
            "game_phase TEXT NOT NULL DEFAULT 'initial_mission',"
            "current_mini_game TEXT,"
            "current_level_id INTEGER,"
            "base_unlocked INTEGER NOT NULL DEFAULT 0,"
            "entered_at INTEGER NOT NULL DEFAULT 0,"
            "last_updated INTEGER NOT NULL DEFAULT 0,"
            "honor_name TEXT,"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        createTable(db, "baronies",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL UNIQUE,"
            "owner_character_id INTEGER NOT NULL,"
            "description TEXT DEFAULT '',"
            "created_at INTEGER NOT NULL,"
            "FOREIGN KEY(owner_character_id) REFERENCES characters(id)"
        );

        createTable(db, "barony_members",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "barony_id INTEGER NOT NULL,"
            "character_id INTEGER NOT NULL UNIQUE,"
            "fiefdom_id INTEGER NOT NULL,"
            "joined_at INTEGER NOT NULL,"
            "role TEXT NOT NULL DEFAULT 'member',"
            "FOREIGN KEY(barony_id) REFERENCES baronies(id),"
            "FOREIGN KEY(character_id) REFERENCES characters(id),"
            "FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)"
        );

        createTable(db, "td_player_unlocks",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "item_type TEXT NOT NULL,"
            "item_id TEXT NOT NULL,"
            "unlocked_at INTEGER NOT NULL,"
            "FOREIGN KEY(character_id) REFERENCES characters(id),"
            "UNIQUE(character_id, item_type, item_id)"
        );

        createTable(db, "game_sessions",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "mini_game TEXT NOT NULL,"
            "level_id INTEGER NOT NULL DEFAULT 0,"
            "started_at INTEGER NOT NULL,"
            "last_activity INTEGER NOT NULL,"
            "total_rounds INTEGER NOT NULL DEFAULT 1,"
            "current_round INTEGER NOT NULL DEFAULT 0,"
            "difficulty INTEGER NOT NULL DEFAULT 1,"
            "lives INTEGER NOT NULL DEFAULT 20,"
            "gold INTEGER NOT NULL DEFAULT 100,"
            "state TEXT NOT NULL DEFAULT 'active',"
            "current_spawn_schedule TEXT DEFAULT NULL,"
            "placements TEXT NOT NULL DEFAULT '[]',"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        createTable(db, "weeding_sessions",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "level_id INTEGER NOT NULL,"
            "started_at INTEGER NOT NULL,"
            "last_activity INTEGER NOT NULL,"
            "state TEXT NOT NULL DEFAULT 'active',"
            "session_json TEXT NOT NULL,"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        createTable(db, "mini_game_progress",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "mini_game TEXT NOT NULL,"
            "level_id INTEGER NOT NULL,"
            "completed INTEGER NOT NULL DEFAULT 0,"
            "best_score INTEGER DEFAULT 0,"
            "times_played INTEGER DEFAULT 0,"
            "last_played INTEGER DEFAULT 0,"
            "FOREIGN KEY(character_id) REFERENCES characters(id),"
            "UNIQUE(character_id, mini_game, level_id)"
        );

        createTable(db, "reward_pools",
            "character_id INTEGER PRIMARY KEY NOT NULL,"
            "full_pool INTEGER NOT NULL DEFAULT 15,"
            "half_pool INTEGER NOT NULL DEFAULT 5,"
            "last_consumed_at INTEGER NOT NULL DEFAULT 0,"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
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

void migrate_spawn_schedule(sqlite::database& db) {
    try {
        db << "ALTER TABLE game_sessions ADD COLUMN current_spawn_schedule TEXT DEFAULT NULL;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

void migrate_placements(sqlite::database& db) {
    try {
        db << "ALTER TABLE game_sessions ADD COLUMN placements TEXT NOT NULL DEFAULT '[]';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

// Drops a column from a table if it exists. Unlike the ADD-column migrations
// (which rely on try/catch for idempotency), DROP COLUMN must be guarded by an
// existence check because SQLite errors on a missing column.
void drop_column_if_exists(sqlite::database& db, const std::string& table_name,
                           const std::string& column) {
    bool exists = false;
    db << "PRAGMA table_info(" + table_name + ");"
       >> [&](int cid, std::string name, std::string type, int notnull,
              std::string dflt, int pk) {
              (void)cid; (void)type; (void)notnull; (void)dflt; (void)pk;
              if (name == column) {
                  exists = true;
              }
          };
    if (exists) {
        db << "ALTER TABLE " + table_name + " DROP COLUMN " + column + ";";
    }
}

// Removes the legacy population counter. Peasant is a building class/chain, not
// a tracked population resource, so the fiefdoms.peasants column is dropped.
void migrate_drop_peasants(sqlite::database& db) {
    drop_column_if_exists(db, "fiefdoms", "peasants");
}

void initializeGameDB(sqlite::database& db) {
    migrate_barony_tables(db);
    createGameDBTables(db);
    migrate_character_archetype(db);
    migrate_character_sex(db);
    migrate_manor_level(db);
    migrate_spawn_schedule(db);
    migrate_placements(db);
    migrate_import_settings(db);
    migrate_land_patent_acknowledged(db);
    migrate_honor_name(db);
    migrate_silver_pence(db);
    migrate_fancy_ironwork(db);
    migrate_charcoal_iron(db);
    migrate_reserves(db);
    migrate_baron_character_id(db);
    migrate_building_output_rates(db);
    migrate_pond_type(db);
    migrate_retinue_gender(db);
    migrate_retinue_health(db);
    migrate_retinue_priority(db);
    migrate_retinue_equipment(db);
    migrate_retinue_maintained(db);
    migrate_building_tech_actions(db);
    migrate_fiefdom_morale_ts(db);
    migrate_drop_peasants(db);
    migrate_beams_boards(db);
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