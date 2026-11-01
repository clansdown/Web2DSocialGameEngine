#pragma once

#include "db/postgres_sqlite_compat.hpp"

// Schema bootstrap. The SQLite DDL and incremental ALTERs have been replaced by
// the PostgreSQL baseline in db/BaselineSchema.hpp.
void initializeGameDB(sqlite::database& db);
void initializeChatDB(sqlite::database& db);
void initializeStaffDB(sqlite::database& db);
void initializeInvitesDB(sqlite::database& db);

void initializeAllDatabases(sqlite::database& game_db, sqlite::database& chat_db,
                            sqlite::database& staff_db);

void ensureGameDBIndexes(sqlite::database& db);
void ensureChatDBIndexes(sqlite::database& db);
void ensureStaffDBIndexes(sqlite::database& db);
void ensureInvitesDBIndexes(sqlite::database& db);
