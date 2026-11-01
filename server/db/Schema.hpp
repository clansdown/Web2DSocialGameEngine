#pragma once

#include <string>

namespace db {

// Connects with the libpq connection string, applies the baseline schema once,
// and records the migration version in schema_migrations. Idempotent.
bool initialize_schema(const std::string& conninfo,
                       const char* schema_sql,
                       const char* label,
                       std::string& error);

// Small admin helpers used by the invite CLI and operations scripts.
bool exec_command(const std::string& conninfo, const std::string& sql, std::string& error);

} // namespace db
