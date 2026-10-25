#pragma once
#include <sqlite_modern_cpp.h>
#include <string>

void initializeGameDB(sqlite::database& db);
void initializeChatDB(sqlite::database& db);
void initializeStaffDB(sqlite::database& db);
void migrate_placements(sqlite::database& db);

void initializeAllDatabases(sqlite::database& game_db, sqlite::database& chat_db,
                            sqlite::database& staff_db);

void ensureGameDBIndexes(sqlite::database& db);
void ensureChatDBIndexes(sqlite::database& db);
void ensureStaffDBIndexes(sqlite::database& db);
