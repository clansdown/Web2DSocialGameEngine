#pragma once
#include <sqlite_modern_cpp.h>
#include <string>

void initializeGameDB(sqlite::database& db);

void initializeMessagesDB(sqlite::database& db);

void initializeAllDatabases(sqlite::database& game_db, sqlite::database& messages_db);

void ensureGameDBIndexes(sqlite::database& db);
void ensureMessagesDBIndexes(sqlite::database& db);