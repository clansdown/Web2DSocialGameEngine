#include "init_db.hpp"

#include "db/BaselineSchema.hpp"
#include "db/Schema.hpp"

#include <stdexcept>
#include <string>

namespace {

void apply(sqlite::database& db, const char* schema, const char* label) {
    if (!db.initialized()) {
        throw std::runtime_error(std::string("database is not initialized: ") + label);
    }
    std::string error;
    if (!db::initialize_schema(db.conninfo(), schema, label, error)) {
        throw std::runtime_error(std::string("schema initialization failed for ") + label +
                                 ": " + error);
    }
}

void ensure_indexes(sqlite::database& db, const char* schema, const char* label) {
    // Indexes are part of the baseline. Re-applying the idempotent schema is
    // safe and picks up index additions between restarts.
    apply(db, schema, label);
}

} // namespace

void initializeGameDB(sqlite::database& db) {
    apply(db, db::k_game_schema, "game");
}

void initializeChatDB(sqlite::database& db) {
    apply(db, db::k_chat_schema, "chat");
}

void initializeStaffDB(sqlite::database& db) {
    apply(db, db::k_staff_schema, "staff");
}

void initializeInvitesDB(sqlite::database& db) {
    apply(db, db::k_invites_schema, "invites");
}

void initializeAllDatabases(sqlite::database& game_db, sqlite::database& chat_db,
                            sqlite::database& staff_db) {
    initializeGameDB(game_db);
    initializeChatDB(chat_db);
    initializeStaffDB(staff_db);
}

void ensureGameDBIndexes(sqlite::database& db) {
    ensure_indexes(db, db::k_game_schema, "game");
}

void ensureChatDBIndexes(sqlite::database& db) {
    ensure_indexes(db, db::k_chat_schema, "chat");
}

void ensureStaffDBIndexes(sqlite::database& db) {
    ensure_indexes(db, db::k_staff_schema, "staff");
}

void ensureInvitesDBIndexes(sqlite::database& db) {
    ensure_indexes(db, db::k_invites_schema, "invites");
}
