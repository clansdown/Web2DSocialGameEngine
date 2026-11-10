#include "db/Schema.hpp"

#include "db/libpq_api.hpp"

#include <ctime>
#include <string>

namespace db {

namespace {

std::string result_error(PGconn* conn, PGresult* result) {
    if (result) {
        std::string message = PQresultErrorMessage(result);
        if (!message.empty()) return message;
    }
    if (conn) {
        std::string message = PQerrorMessage(conn);
        if (!message.empty()) return message;
    }
    return "unknown PostgreSQL error";
}

bool run(PGconn* conn, const std::string& sql, std::string& error) {
    PGresult* result = PQexec(conn, sql.c_str());
    if (!result) {
        error = result_error(conn, nullptr);
        return false;
    }
    const int status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK && status != PGRES_EMPTY_QUERY) {
        error = result_error(conn, result);
        PQclear(result);
        return false;
    }
    PQclear(result);
    return true;
}

} // namespace

bool initialize_schema(const std::string& conninfo,
                       const char* schema_sql,
                       const char* label,
                       std::string& error) {
    if (conninfo.empty()) {
        error = std::string("database connection string is empty for ") + label;
        return false;
    }

    PGconn* conn = PQconnectdb(conninfo.c_str());
    if (!conn) {
        error = std::string("could not allocate PostgreSQL connection for ") + label;
        return false;
    }
    if (PQstatus(conn) != CONNECTION_OK) {
        error = std::string("could not connect to ") + label + ": " + result_error(conn, nullptr);
        PQfinish(conn);
        return false;
    }

    PQsetClientEncoding(conn, "UTF8");
    // `CREATE TABLE IF NOT EXISTS` emits a NOTICE for every already-migrated
    // database. Keep CLI output clean; warnings and errors still surface.
    std::string notice_error;
    run(conn, "SET client_min_messages = 'warning'", notice_error);
    run(conn, "SET statement_timeout = '30s'", error);
    run(conn, "SET lock_timeout = '10s'", error);

    if (!run(conn,
             "CREATE TABLE IF NOT EXISTS schema_migrations ("
             "version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at BIGINT NOT NULL)",
             error)) {
        PQfinish(conn);
        return false;
    }

    PGresult* existing = PQexec(conn, "SELECT 1 FROM schema_migrations WHERE version = 1");
    if (!existing) {
        error = result_error(conn, nullptr);
        PQfinish(conn);
        return false;
    }
    const bool applied = PQresultStatus(existing) == PGRES_TUPLES_OK && PQntuples(existing) > 0;
    PQclear(existing);
    if (applied) {
        PQfinish(conn);
        return true;
    }

    // One multi-statement PQexec is a single transaction in PostgreSQL.
    std::string script = "BEGIN;\n";
    script += schema_sql;
    script += "\nINSERT INTO schema_migrations (version, name, applied_at) VALUES (1, '";
    script += label;
    script += "', ";
    script += std::to_string(static_cast<long long>(std::time(nullptr)));
    script += ") ON CONFLICT (version) DO NOTHING;\nCOMMIT;\n";

    if (!run(conn, script, error)) {
        std::string ignored;
        run(conn, "ROLLBACK", ignored);
        PQfinish(conn);
        return false;
    }

    PQfinish(conn);
    return true;
}

bool exec_command(const std::string& conninfo, const std::string& sql, std::string& error) {
    if (conninfo.empty()) {
        error = "database connection string is empty";
        return false;
    }
    PGconn* conn = PQconnectdb(conninfo.c_str());
    if (!conn) {
        error = "could not allocate PostgreSQL connection";
        return false;
    }
    if (PQstatus(conn) != CONNECTION_OK) {
        error = std::string("could not connect: ") + result_error(conn, nullptr);
        PQfinish(conn);
        return false;
    }
    const bool ok = run(conn, sql, error);
    PQfinish(conn);
    return ok;
}

} // namespace db
