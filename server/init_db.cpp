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
    ensureIndex(db, "idx_character_letters_character", "character_letters", "character_id, created_at");
    ensureIndex(db, "idx_character_milestones_character", "character_milestones", "character_id");
    ensureIndex(db, "idx_character_counters_character", "character_counters", "character_id");
    ensureIndex(db, "idx_mission_completions_character", "character_mission_completions", "character_id");
    ensureIndex(db, "idx_combat_attempts_offering", "combat_mission_attempts", "offering_id");
    ensureIndex(db, "idx_game_sessions_character", "game_sessions", "character_id");
    ensureIndex(db, "idx_weeding_sessions_character", "weeding_sessions", "character_id");
    ensureIndex(db, "idx_retinue_members_character", "retinue_members", "character_id");
    ensureIndex(db, "idx_character_cosmetics_character", "character_cosmetics", "character_id");
    ensureIndex(db, "idx_platinum_transactions_character", "platinum_transactions", "character_id");
    ensureIndex(db, "idx_fiefdom_armory_fiefdom", "fiefdom_armory", "fiefdom_id");
    ensureIndex(db, "idx_fiefdom_armory_member", "fiefdom_armory", "member_id");
    ensureIndex(db, "idx_character_identity_history_character", "character_identity_history", "character_id");
    }

    void ensureStaffDBIndexes_private(sqlite::database& db) {
        ensureIndex(db, "idx_staff_journal_created", "staff_action_journal", "created_at");
        ensureIndex(db, "idx_staff_journal_action_type", "staff_action_journal", "action_type");
        ensureIndex(db, "idx_staff_journal_target", "staff_action_journal", "target_type, target_id");
        ensureIndex(db, "idx_staff_journal_status", "staff_action_journal", "status");
        ensureIndex(db, "idx_staff_journal_undo_of", "staff_action_journal", "undo_of");
        ensureIndex(db, "idx_staff_events_action", "staff_action_events", "action_id");
        ensureIndex(db, "idx_support_tickets_account", "support_tickets", "account_id");
        ensureIndex(db, "idx_support_tickets_status", "support_tickets", "status");
        ensureIndex(db, "idx_support_tickets_updated", "support_tickets", "updated_at");
        ensureIndex(db, "idx_support_messages_ticket", "support_ticket_messages", "ticket_id, created_at");
        ensureIndex(db, "idx_notifications_account", "notifications", "account_id, created_at");
        ensureIndex(db, "idx_notifications_unread", "notifications", "account_id, read_at");
        ensureIndex(db, "idx_avatar_hash_bans_active", "banned_avatar_hashes", "revoked_at");
    }

    void ensureChatDBIndexes_private(sqlite::database& db) {
        // messages(chat_id, seq) is UNIQUE at the table level, so no separate
        // index is needed for that pair.
        ensureIndex(db, "idx_chat_messages_change", "messages", "change_id");
        ensureIndex(db, "idx_chat_messages_chat_change", "messages", "chat_id, change_id");
        ensureIndex(db, "idx_chat_members_character", "chat_members", "character_id");
        ensureIndex(db, "idx_chat_members_change", "chat_members", "change_id");
        ensureIndex(db, "idx_chat_members_chat_muted", "chat_members", "chat_id, muted");
        ensureIndex(db, "idx_chat_invites_invited", "chat_invites", "invited_character_id");
        ensureIndex(db, "idx_chat_translations_change", "message_translations", "change_id");
        ensureIndex(db, "idx_chat_translations_msg_lang", "message_translations", "message_id, target_language");
        ensureIndex(db, "idx_chat_reactions_message", "message_reactions", "message_id");
        ensureIndex(db, "idx_chat_friends_character", "friends", "character_id");
        ensureIndex(db, "idx_chat_friends_friend", "friends", "friend_character_id");
        ensureIndex(db, "idx_chat_requests_to", "friend_requests", "to_character_id");
        ensureIndex(db, "idx_chat_blocks_blocker", "blocks", "blocker_character_id");
        ensureIndex(db, "idx_chat_blocks_blocked", "blocks", "blocked_character_id");
        ensureIndex(db, "idx_chat_mutes_character", "character_mutes", "character_id");
        ensureIndex(db, "idx_chat_mutes_muted", "character_mutes", "muted_character_id");
        ensureIndex(db, "idx_chat_push_account", "push_subscriptions", "account_id");
        ensureIndex(db, "idx_chat_moderation_actor", "moderation_log", "actor_character_id");
        ensureIndex(db, "idx_chat_moderation_chat", "moderation_log", "chat_id");
        ensureIndex(db, "idx_chat_violations_account", "rate_limit_violations", "account_id, created_at");
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

    // The player's chosen interface / display language (per account). Chosen at
    // first launch and persisted at account creation; drives UI text and the
    // default chat translation target. See docs/chat.md.
    void migrate_user_language(sqlite::database& db) {
        try {
            db << "ALTER TABLE users ADD COLUMN language TEXT NOT NULL DEFAULT 'en';";
        } catch (const std::exception&) {
            // Column already exists — ignore
        }
    }

    // Adult status is account-level and customer-service/digital-credential
    // verified. The provenance columns are admin-only and audited in chat.db.
    void migrate_adult_verification(sqlite::database& db) {
        try {
            db << "ALTER TABLE users ADD COLUMN adult_verified_at INTEGER NOT NULL DEFAULT 0;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE users ADD COLUMN adult_verified_by INTEGER;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE users ADD COLUMN adult_verification_method TEXT;";
        } catch (const std::exception&) {}
    }

    // Account-level social/support restrictions (staff console). They are
    // deliberately separate: a social restriction keeps solo play available,
    // while a support lock only blocks creating/replying to support tickets.
    void migrate_account_restrictions(sqlite::database& db) {
        try {
            db << "ALTER TABLE users ADD COLUMN social_restricted INTEGER NOT NULL DEFAULT 0;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE users ADD COLUMN support_locked INTEGER NOT NULL DEFAULT 0;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE users ADD COLUMN restriction_updated_at INTEGER NOT NULL DEFAULT 0;";
        } catch (const std::exception&) {}
    }

    // The barony keeps a mandatory safe name/tag selection and an optional
    // adult-only custom name/description. `name` stores the custom value when
    // present and the safe label otherwise, so the NOT NULL/UNIQUE schema stays
    // intact; projections always choose the viewer-relative value.
    void migrate_safe_barony_identity(sqlite::database& db) {
        try {
            db << "ALTER TABLE baronies ADD COLUMN safe_name_id TEXT NOT NULL DEFAULT '';";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE baronies ADD COLUMN safe_tag_ids TEXT NOT NULL DEFAULT '[]';";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE baronies ADD COLUMN accepts_minors INTEGER NOT NULL DEFAULT 0;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE player_game_state ADD COLUMN safe_barony_name_id TEXT;";
        } catch (const std::exception&) {}
    }

    // Safe messages are server-owned catalog ids. Custom text remains in
    // `body`; a safe message has `safe_message_id` and no custom text. GIFs are
    // catalog ids too (the legacy `gif` column is never read).
    void migrate_message_safety(sqlite::database& db) {
        try {
            db << "ALTER TABLE messages ADD COLUMN safe_message_id TEXT;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE messages ADD COLUMN gif_id TEXT;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE messages ADD COLUMN reply_safe_excerpt TEXT;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE messages ADD COLUMN deletion_reason TEXT;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE messages ADD COLUMN deleted_by_account_id INTEGER;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE message_versions ADD COLUMN safe_message_id TEXT;";
        } catch (const std::exception&) {}
        try {
            db << "ALTER TABLE message_versions ADD COLUMN gif_id TEXT;";
        } catch (const std::exception&) {}
    }

    // Admin identity lock flags. A locked name/avatar can be reverted but not
    // freely changed by the player. See docs/chat_identity_lock.md.
    void migrate_identity_lock(sqlite::database& db) {
        try {
            db << "ALTER TABLE characters ADD COLUMN name_locked INTEGER NOT NULL DEFAULT 0;";
        } catch (const std::exception&) {
            // Column already exists — ignore
        }
        try {
            db << "ALTER TABLE characters ADD COLUMN avatar_locked INTEGER NOT NULL DEFAULT 0;";
        } catch (const std::exception&) {
            // Column already exists — ignore
        }
        try {
            db << "ALTER TABLE characters ADD COLUMN identity_locked_by INTEGER;";
        } catch (const std::exception&) {
            // Column already exists — ignore
        }
        try {
            db << "ALTER TABLE characters ADD COLUMN identity_locked_at INTEGER;";
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

// Tenure: when a character last left a barony (0 = never). Drives the join
// cooldown in progression.json ("barony.join_cooldown_days").
void migrate_barony_left_at(sqlite::database& db) {
    try {
        db << "ALTER TABLE player_game_state ADD COLUMN barony_left_at INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

// A barony whose founder left while it had no other members is tombstoned
// (defunct_at > 0) rather than deleted: the name stays reserved and it is
// hidden from the barony list.
void migrate_barony_defunct(sqlite::database& db) {
    try {
        db << "ALTER TABLE baronies ADD COLUMN defunct_at INTEGER NOT NULL DEFAULT 0;";
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

void migrate_retinue_xp(sqlite::database& db) {
    try {
        db << "ALTER TABLE retinue_members ADD COLUMN xp INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

// Persistent member identity: the headshot id is the path of the image relative
// to game/images/retinue/headshots/ (e.g. "male/03.png"). The knight keeps an
// empty value and resolves its portrait from the character's PFP instead.
void migrate_retinue_headshot(sqlite::database& db) {
    try {
        db << "ALTER TABLE retinue_members ADD COLUMN headshot_id TEXT NOT NULL DEFAULT '';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

// Per-character player representative image (PFP): a safe-pool pick plus an
// optional 18+ custom upload, with a change timestamp for the cooldown.
void migrate_character_avatar(sqlite::database& db) {
    try {
        db << "ALTER TABLE characters ADD COLUMN avatar_safe_id TEXT;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE characters ADD COLUMN avatar_custom_id TEXT;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE characters ADD COLUMN avatar_changed_at INTEGER NOT NULL DEFAULT 0;";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
    try {
        db << "ALTER TABLE characters ADD COLUMN avatar_storage TEXT NOT NULL DEFAULT 'local';";
    } catch (const std::exception&) {
        // Column already exists — ignore
    }
}

// The safe-avatar pool became gender-split
// (images/avatars/safe/{male,female}/), so ids are now gender-qualified
// ("male/3.png"). Prefix any legacy bare id with the character's sex. Runs
// after migrate_character_sex; idempotent because every id then contains '/'.
void migrate_safe_avatar_gender(sqlite::database& db) {
    try {
        db << "UPDATE characters "
              "SET avatar_safe_id = "
              "(CASE WHEN sex = 'female' THEN 'female/' ELSE 'male/' END) || avatar_safe_id "
              "WHERE avatar_safe_id IS NOT NULL AND avatar_safe_id != '' "
              "AND instr(avatar_safe_id, '/') = 0;";
    } catch (const std::exception&) {
        // Best-effort — a malformed legacy value must not block startup.
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
            "adult INTEGER NOT NULL DEFAULT 0,"
            "adult_verified_at INTEGER NOT NULL DEFAULT 0,"
            "adult_verified_by INTEGER,"
            "adult_verification_method TEXT"
        );

        createTable(db, "characters",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "user_id INTEGER NOT NULL,"
            "display_name TEXT NOT NULL,"
            "safe_display_name TEXT NOT NULL,"
            "level INTEGER DEFAULT 1,"
            "avatar_safe_id TEXT,"
            "avatar_custom_id TEXT,"
            "avatar_changed_at INTEGER NOT NULL DEFAULT 0,"
            "avatar_storage TEXT NOT NULL DEFAULT 'local',"
            "FOREIGN KEY(user_id) REFERENCES users(id)"
        );

        createTable(db, "fiefdoms",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "owner_id INTEGER NOT NULL,"
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
            "xp INTEGER NOT NULL DEFAULT 0,"
            "headshot_id TEXT NOT NULL DEFAULT '',"
            "created_at INTEGER NOT NULL,"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        // Premium variety (rts_design.md §15): Platinum is a separate ledger,
        // never fungible with gold/silver and never used by the economy.
        createTable(db, "platinum_ledger",
            "character_id INTEGER PRIMARY KEY,"
            "balance INTEGER NOT NULL DEFAULT 0,"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        createTable(db, "platinum_transactions",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "product_id TEXT NOT NULL,"
            "platinum INTEGER NOT NULL DEFAULT 0,"
            "status TEXT NOT NULL DEFAULT 'pending',"
            "idempotency_key TEXT NOT NULL UNIQUE,"
            "created_at INTEGER NOT NULL,"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        createTable(db, "character_cosmetics",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "cosmetic_id TEXT NOT NULL,"
            "source TEXT NOT NULL DEFAULT 'store',"
            "acquired_at INTEGER NOT NULL,"
            "revoked INTEGER NOT NULL DEFAULT 0,"
            "UNIQUE(character_id, cosmetic_id),"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        createTable(db, "character_loadout",
            "character_id INTEGER PRIMARY KEY,"
            "loadout TEXT NOT NULL DEFAULT '{}',"
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
            "safe_barony_name_id TEXT,"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        // Prior display names / avatars, retained so an admin can force a revert
        // to any earlier value (chat identity lock). See server/tables/character_identity_history.md
        // and docs/chat_identity_lock.md.
        createTable(db, "character_identity_history",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "kind TEXT NOT NULL,"
            "value TEXT NOT NULL,"
            "changed_at INTEGER NOT NULL,"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        createTable(db, "baronies",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL UNIQUE,"
            "safe_name_id TEXT NOT NULL DEFAULT '',"
            "safe_tag_ids TEXT NOT NULL DEFAULT '[]',"
            "accepts_minors INTEGER NOT NULL DEFAULT 0,"
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

        // In-game letters (the king's missives and future notices). A letter is
        // stored as a text-system key + timestamps; the client renders it through
        // the character-text path so gender/name/honor substitution still works.
        createTable(db, "character_letters",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "letter_key TEXT NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "read_at INTEGER NOT NULL DEFAULT 0,"
            "FOREIGN KEY(character_id) REFERENCES characters(id)"
        );

        // Generic per-character progression milestones (e.g. defeating a boss
        // enemy in the RTS). Config maps enemy classes to milestone ids; the
        // honor qualification gate reads them.
        createTable(db, "character_milestones",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "milestone_id TEXT NOT NULL,"
            "achieved_at INTEGER NOT NULL,"
            "FOREIGN KEY(character_id) REFERENCES characters(id),"
            "UNIQUE(character_id, milestone_id)"
        );

        // One-shot PvE mission offerings (auto-generated, deterministic). An
        // offering is consumed once its mission is won; a defeat/draw/forfeit
        // leaves it available to retry.
        createTable(db, "character_mission_completions",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "offering_id TEXT NOT NULL,"
            "completed_at INTEGER NOT NULL,"
            "FOREIGN KEY(character_id) REFERENCES characters(id),"
            "UNIQUE(character_id, offering_id)"
        );

        // Every finished combat mission attempt (win or lose). The warband's
        // end state is captured so a future retry could resume from survivors.
        createTable(db, "combat_mission_attempts",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "offering_id TEXT NOT NULL,"
            "host_character_id INTEGER NOT NULL,"
            "match_id TEXT NOT NULL,"
            "started_at INTEGER NOT NULL,"
            "ended_at INTEGER,"
            "result TEXT,"
            "participants_json TEXT NOT NULL DEFAULT '[]',"
            "warband_end_state_json TEXT NOT NULL DEFAULT '[]',"
            "FOREIGN KEY(host_character_id) REFERENCES characters(id)"
        );

        // Generic per-character counters (namespaced keys). Enemy-slain tallies
        // are "slain.<unit_class>"; progression conditions key off them.
        createTable(db, "character_counters",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "character_id INTEGER NOT NULL,"
            "counter_key TEXT NOT NULL,"
            "value INTEGER NOT NULL DEFAULT 0,"
            "updated_at INTEGER NOT NULL,"
            "FOREIGN KEY(character_id) REFERENCES characters(id),"
            "UNIQUE(character_id, counter_key)"
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

    // The chat database is intentionally independent from game.db (like the old
    // messages.db): it is a self-contained world with its own change_id cursor.
    // There are no cross-database FOREIGN KEYs — character/chat ids are stored
    // as plain integers. See server/docs/chat.md and server/tables/chat_*.md.
    void createChatDBTables(sqlite::database& db) {
        createTable(db, "change_counter",
            "id INTEGER PRIMARY KEY CHECK (id = 1),"
            "next_change_id INTEGER NOT NULL"
        );
        // Seed the single counter row. INSERT OR IGNORE makes this idempotent.
        db << "INSERT OR IGNORE INTO change_counter (id, next_change_id) VALUES (1, 1);";

        createTable(db, "chats",
            "id TEXT PRIMARY KEY,"
            "type TEXT NOT NULL CHECK (type IN ('direct','group','barony','dukedom','wildlands')),"
            "name TEXT,"
            "name_changed_at INTEGER,"
            "owner_character_id INTEGER,"
            "direct_key TEXT UNIQUE,"
            "topic_id TEXT,"
            "source_kind TEXT,"
            "source_id INTEGER,"
            "members_can_invite INTEGER NOT NULL DEFAULT 0,"
            "created_at INTEGER NOT NULL,"
            "archived_at INTEGER,"
            "last_seq INTEGER NOT NULL DEFAULT 0,"
            "change_id INTEGER NOT NULL,"
            "UNIQUE (source_kind, source_id)"
        );

        createTable(db, "chat_members",
            "chat_id TEXT NOT NULL,"
            "character_id INTEGER NOT NULL,"
            "role TEXT NOT NULL DEFAULT 'member',"
            "joined_at INTEGER NOT NULL,"
            "left_at INTEGER,"
            "sticky INTEGER NOT NULL DEFAULT 0,"
            "muted INTEGER NOT NULL DEFAULT 0,"
            "notify_level TEXT NOT NULL DEFAULT 'all',"
            "last_read_seq INTEGER NOT NULL DEFAULT 0,"
            "unread_count INTEGER NOT NULL DEFAULT 0,"
            "change_id INTEGER NOT NULL,"
            "PRIMARY KEY (chat_id, character_id)"
        );

        createTable(db, "chat_invites",
            "chat_id TEXT NOT NULL,"
            "invited_character_id INTEGER NOT NULL,"
            "invited_by_character_id INTEGER NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "expires_at INTEGER NOT NULL,"
            "change_id INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (chat_id, invited_character_id)"
        );

        createTable(db, "messages",
            "id TEXT PRIMARY KEY,"
            "chat_id TEXT NOT NULL,"
            "seq INTEGER NOT NULL,"
            "author_character_id INTEGER,"
            "body TEXT,"
            "safe_message_id TEXT,"
            "source_language TEXT,"
            "source_language_confidence REAL,"
            "gif TEXT,"
            "gif_id TEXT,"
            "reply_to_message_id TEXT,"
            "reply_author_character_id INTEGER,"
            "reply_excerpt TEXT,"
            "reply_safe_excerpt TEXT,"
            "reply_source_language TEXT,"
            "system_id TEXT,"
            "system_args TEXT,"
            "created_at INTEGER NOT NULL,"
            "edited_at INTEGER,"
            "deleted_at INTEGER,"
            "deletion_reason TEXT,"
            "deleted_by_account_id INTEGER,"
            "version INTEGER NOT NULL DEFAULT 1,"
            "change_id INTEGER NOT NULL,"
            "UNIQUE (chat_id, seq)"
        );

        createTable(db, "message_versions",
            "message_id TEXT NOT NULL,"
            "version INTEGER NOT NULL,"
            "body TEXT,"
            "safe_message_id TEXT,"
            "gif_id TEXT,"
            "editor_character_id INTEGER,"
            "edited_at INTEGER,"
            "PRIMARY KEY (message_id, version)"
        );

        // Admin-only forensic copies of messages deleted during an account
        // demotion. Normal readers never query this table.
        createTable(db, "message_deletions",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "message_id TEXT NOT NULL,"
            "chat_id TEXT NOT NULL,"
            "author_character_id INTEGER,"
            "author_account_id INTEGER NOT NULL,"
            "custom_body TEXT,"
            "safe_message_id TEXT,"
            "gif_id TEXT,"
            "source_language TEXT,"
            "reply_to_message_id TEXT,"
            "reply_custom_excerpt TEXT,"
            "reply_safe_excerpt TEXT,"
            "created_at INTEGER NOT NULL,"
            "deleted_at INTEGER NOT NULL,"
            "deletion_reason TEXT NOT NULL,"
            "deleted_by_account_id INTEGER NOT NULL"
        );

        createTable(db, "message_translations",
            "message_id TEXT NOT NULL,"
            "target_language TEXT NOT NULL,"
            "engine TEXT NOT NULL,"
            "model_version TEXT NOT NULL,"
            "body TEXT NOT NULL,"
            "detected_source_language TEXT,"
            "confidence REAL,"
            "created_at INTEGER NOT NULL,"
            "change_id INTEGER NOT NULL,"
            "PRIMARY KEY (message_id, target_language, engine, model_version)"
        );

        createTable(db, "message_reactions",
            "message_id TEXT NOT NULL,"
            "character_id INTEGER NOT NULL,"
            "emoji TEXT NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "PRIMARY KEY (message_id, character_id, emoji)"
        );

        createTable(db, "bookmarks",
            "character_id INTEGER NOT NULL,"
            "message_id TEXT NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "PRIMARY KEY (character_id, message_id)"
        );

        createTable(db, "friends",
            "character_id INTEGER NOT NULL,"
            "friend_character_id INTEGER NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "change_id INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (character_id, friend_character_id)"
        );

        createTable(db, "friend_requests",
            "from_character_id INTEGER NOT NULL,"
            "to_character_id INTEGER NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "change_id INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (from_character_id, to_character_id)"
        );

        createTable(db, "blocks",
            "blocker_character_id INTEGER NOT NULL,"
            "blocked_character_id INTEGER NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "change_id INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (blocker_character_id, blocked_character_id)"
        );

        createTable(db, "character_mutes",
            "character_id INTEGER NOT NULL,"
            "muted_character_id INTEGER NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "change_id INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (character_id, muted_character_id)"
        );

        // Fast-path counter: the number of block/mute relationships in which a
        // character participates in EITHER direction. When zero (the common
        // case), per-message eligibility checks can be skipped entirely.
        createTable(db, "character_relation_counts",
            "character_id INTEGER PRIMARY KEY,"
            "relation_count INTEGER NOT NULL DEFAULT 0"
        );

        createTable(db, "chat_bans",
            "chat_id TEXT NOT NULL,"
            "character_id INTEGER NOT NULL,"
            "banned_by_character_id INTEGER,"
            "reason TEXT,"
            "created_at INTEGER NOT NULL,"
            "change_id INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (chat_id, character_id)"
        );

        createTable(db, "moderation_log",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "created_at INTEGER NOT NULL,"
            "actor_character_id INTEGER,"
            "action TEXT NOT NULL,"
            "chat_id TEXT,"
            "target_character_id INTEGER,"
            "payload TEXT"
        );

        createTable(db, "rate_limit_violations",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "account_id INTEGER NOT NULL,"
            "kind TEXT NOT NULL,"
            "created_at INTEGER NOT NULL"
        );

        createTable(db, "rate_limit_timeouts",
            "account_id INTEGER PRIMARY KEY,"
            "level INTEGER NOT NULL DEFAULT 0,"
            "until INTEGER NOT NULL"
        );

        createTable(db, "push_subscriptions",
            "id TEXT PRIMARY KEY,"
            "account_id INTEGER NOT NULL,"
            "endpoint TEXT NOT NULL,"
            "p256dh TEXT NOT NULL,"
            "auth TEXT NOT NULL,"
            "vapid_key_id TEXT,"
            "created_at INTEGER NOT NULL,"
            "last_success_at INTEGER,"
            "fail_count INTEGER NOT NULL DEFAULT 0,"
            "UNIQUE (account_id, endpoint)"
        );

        createTable(db, "push_preferences",
            "character_id INTEGER PRIMARY KEY,"
            "push_enabled INTEGER NOT NULL DEFAULT 1,"
            "dm_from_friends INTEGER NOT NULL DEFAULT 1,"
            "group_mentions INTEGER NOT NULL DEFAULT 0"
        );

        // Per-character translation language preferences. Absence of a row means
        // "enabled" (default: translate every source language except the game's
        // own language). Only disabled languages are stored.
        createTable(db, "chat_language_prefs",
            "character_id INTEGER NOT NULL,"
            "language TEXT NOT NULL,"
            "enabled INTEGER NOT NULL DEFAULT 1,"
            "change_id INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (character_id, language)"
        );
    }

    // Game-staff, the shared action journal, support tickets, notifications,
    // and banned avatar hashes. This database is deliberately independent from
    // game.db and chat.db: actions can span both, so the journal cannot live in
    // either. There are no cross-database FOREIGN KEYs.
    void createStaffDBTables(sqlite::database& db) {
        createTable(db, "staff_roles",
            "id TEXT PRIMARY KEY,"
            "name TEXT NOT NULL UNIQUE,"
            "description TEXT NOT NULL DEFAULT '',"
            "created_at INTEGER NOT NULL"
        );

        createTable(db, "staff_role_capabilities",
            "role_id TEXT NOT NULL,"
            "capability TEXT NOT NULL,"
            "PRIMARY KEY (role_id, capability)"
        );

        createTable(db, "staff_accounts",
            "account_id INTEGER PRIMARY KEY,"
            "created_at INTEGER NOT NULL,"
            "created_by_label TEXT,"
            "revoked_at INTEGER"
        );

        createTable(db, "staff_account_roles",
            "account_id INTEGER NOT NULL,"
            "role_id TEXT NOT NULL,"
            "PRIMARY KEY (account_id, role_id)"
        );

        createTable(db, "staff_preferences",
            "account_id INTEGER PRIMARY KEY,"
            "chat_moderation_mode INTEGER NOT NULL DEFAULT 0,"
            "updated_at INTEGER NOT NULL"
        );

        createTable(db, "staff_action_journal",
            "id TEXT PRIMARY KEY,"
            "operation TEXT NOT NULL,"
            "actor_account_id INTEGER,"
            "actor_character_id INTEGER,"
            "actor_type TEXT NOT NULL,"
            "authority TEXT NOT NULL,"
            "capability TEXT,"
            "action_type TEXT NOT NULL,"
            "action_version INTEGER NOT NULL,"
            "target_type TEXT NOT NULL,"
            "target_id TEXT NOT NULL,"
            "scope TEXT,"
            "reason TEXT,"
            "before_state TEXT,"
            "after_state TEXT,"
            "undo_descriptor TEXT,"
            "status TEXT NOT NULL,"
            "error TEXT,"
            "parent_action_id TEXT,"
            "undo_of TEXT,"
            "created_at INTEGER NOT NULL,"
            "updated_at INTEGER NOT NULL,"
            "applied_at INTEGER"
        );

        createTable(db, "staff_action_events",
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "action_id TEXT NOT NULL,"
            "event_type TEXT NOT NULL,"
            "detail TEXT,"
            "created_at INTEGER NOT NULL"
        );

        createTable(db, "staff_action_links",
            "action_id TEXT NOT NULL,"
            "resource_type TEXT NOT NULL,"
            "resource_id TEXT NOT NULL,"
            "link_role TEXT NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "PRIMARY KEY (action_id, resource_type, resource_id, link_role)"
        );

        createTable(db, "banned_avatar_hashes",
            "sha256 TEXT PRIMARY KEY,"
            "reason TEXT,"
            "banned_by_account_id INTEGER NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "revoked_at INTEGER,"
            "revoked_by_account_id INTEGER,"
            "action_id TEXT"
        );

        createTable(db, "support_tickets",
            "id TEXT PRIMARY KEY,"
            "account_id INTEGER NOT NULL,"
            "character_id INTEGER,"
            "category TEXT NOT NULL,"
            "subject TEXT NOT NULL,"
            "status TEXT NOT NULL,"
            "priority TEXT NOT NULL DEFAULT 'normal',"
            "assigned_account_id INTEGER,"
            "player_last_read_at INTEGER NOT NULL DEFAULT 0,"
            "staff_last_read_at INTEGER NOT NULL DEFAULT 0,"
            "created_at INTEGER NOT NULL,"
            "updated_at INTEGER NOT NULL,"
            "closed_at INTEGER"
        );

        createTable(db, "support_ticket_messages",
            "id TEXT PRIMARY KEY,"
            "ticket_id TEXT NOT NULL,"
            "author_kind TEXT NOT NULL,"
            "author_account_id INTEGER,"
            "author_character_id INTEGER,"
            "body TEXT NOT NULL,"
            "is_internal INTEGER NOT NULL DEFAULT 0,"
            "created_at INTEGER NOT NULL"
        );

        createTable(db, "support_ticket_action_links",
            "ticket_id TEXT NOT NULL,"
            "action_id TEXT NOT NULL,"
            "created_by_account_id INTEGER NOT NULL,"
            "created_at INTEGER NOT NULL,"
            "PRIMARY KEY (ticket_id, action_id)"
        );

        createTable(db, "notifications",
            "id TEXT PRIMARY KEY,"
            "account_id INTEGER NOT NULL,"
            "character_id INTEGER,"
            "kind TEXT NOT NULL,"
            "text_id TEXT NOT NULL,"
            "text_args TEXT NOT NULL DEFAULT '{}',"
            "ticket_id TEXT,"
            "action_id TEXT,"
            "route TEXT,"
            "created_at INTEGER NOT NULL,"
            "read_at INTEGER"
        );

        // Seed starter roles. v1 intentionally enables every capability on every
        // role; capability checks are already live so roles can be tightened
        // later without a schema change.
        const std::string capabilities[] = {
            "account.view", "account.set_adult_status", "account.set_social_restriction",
            "account.set_support_lock", "identity.view_history", "identity.set_name_lock",
            "identity.set_avatar_lock", "identity.revert_name", "identity.revert_avatar",
            "identity.remove_custom_avatar", "avatar.ban_hash", "barony.view",
            "barony.set_safe_identity", "barony.set_accepts_minors", "barony.reassign_owner",
            "chat.moderate_any", "support.ticket.view", "support.ticket.reply",
            "support.ticket.manage", "support.ticket.internal_notes",
            "support.unstick_session", "support.set_game_phase", "support.set_march_progress",
            "support.cancel_construction", "support.adjust_fiefdom_resource", "staff.undo",
            "staff.audit.read", "staff.read_sensitive"
        };
        const struct { const char* id; const char* name; const char* description; } roles[] = {
            {"superadmin", "Superadmin", "Full staff access."},
            {"customer_service", "Customer Service", "Account, identity, and support tools."},
            {"moderator", "Moderator", "Chat and identity moderation."},
            {"avatar_moderator", "Avatar Moderator", "Identity and avatar moderation."},
            {"support_agent", "Support Agent", "Support queue and unstick tools."}
        };
        const int64_t now = 0;  // Roles are static; timestamp is informational.
        for (const auto& role : roles) {
            db << "INSERT OR IGNORE INTO staff_roles (id, name, description, created_at) "
                  "VALUES (?, ?, ?, ?);"
               << std::string(role.id) << std::string(role.name)
               << std::string(role.description) << now;
            for (const auto& capability : capabilities) {
                db << "INSERT OR IGNORE INTO staff_role_capabilities (role_id, capability) "
                      "VALUES (?, ?);"
                   << std::string(role.id) << capability;
            }
        }
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

// Fiefdoms are unnamed. The legacy `fiefdoms.name` column is removed; no API
// field or generated "Manor of <username>" remains.
void migrate_drop_fiefdom_name(sqlite::database& db) {
    drop_column_if_exists(db, "fiefdoms", "name");
}

void initializeGameDB(sqlite::database& db) {
    migrate_barony_tables(db);
    createGameDBTables(db);
    migrate_character_archetype(db);
    migrate_character_sex(db);
    migrate_character_avatar(db);
    migrate_safe_avatar_gender(db);
    migrate_identity_lock(db);
    migrate_user_language(db);
    migrate_adult_verification(db);
    migrate_account_restrictions(db);
    migrate_safe_barony_identity(db);
    migrate_manor_level(db);
    migrate_spawn_schedule(db);
    migrate_placements(db);
    migrate_import_settings(db);
    migrate_land_patent_acknowledged(db);
    migrate_honor_name(db);
    migrate_barony_left_at(db);
    migrate_barony_defunct(db);
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
    migrate_retinue_xp(db);
    migrate_retinue_headshot(db);
    migrate_building_tech_actions(db);
    migrate_fiefdom_morale_ts(db);
    migrate_drop_peasants(db);
    migrate_drop_fiefdom_name(db);
    migrate_beams_boards(db);
    ensureGameDBIndexes_private(db);
}

void initializeChatDB(sqlite::database& db) {
    createChatDBTables(db);
    migrate_message_safety(db);
    ensureChatDBIndexes_private(db);
}

void initializeStaffDB(sqlite::database& db) {
    createStaffDBTables(db);
    ensureStaffDBIndexes_private(db);
}

void ensureGameDBIndexes(sqlite::database& db) {
    ensureGameDBIndexes_private(db);
}

void ensureChatDBIndexes(sqlite::database& db) {
    ensureChatDBIndexes_private(db);
}

void ensureStaffDBIndexes(sqlite::database& db) {
    ensureStaffDBIndexes_private(db);
}

void initializeAllDatabases(sqlite::database& game_db, sqlite::database& chat_db,
                            sqlite::database& staff_db) {
    initializeGameDB(game_db);
    initializeChatDB(chat_db);
    initializeStaffDB(staff_db);
}