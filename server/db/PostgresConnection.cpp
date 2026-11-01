#include "db/PostgresConnection.hpp"
#include "db/Executor.hpp"

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace db {

namespace {

constexpr int kSqlstateField = 'C';

std::string sqlstate_of(PGresult* result) {
    if (!result) return {};
    const char* value = PQresultErrorField(result, kSqlstateField);
    return value ? std::string(value) : std::string();
}

error map_error(PGconn* conn, PGresult* result) {
    error out;
    out.sqlstate = sqlstate_of(result);
    if (result) {
        const char* message = PQresultErrorMessage(result);
        out.message = message ? message : "";
    }
    if (out.message.empty() && conn) {
        const char* message = PQerrorMessage(conn);
        out.message = message ? message : "";
    }
    if (out.sqlstate == "23505") {
        out.code = error_code::unique_violation;
    } else if (out.sqlstate == "23503") {
        out.code = error_code::foreign_key_violation;
    } else if (out.sqlstate == "40001") {
        out.code = error_code::serialization_failure;
        out.retryable = true;
    } else if (out.sqlstate == "40P01") {
        out.code = error_code::deadlock;
        out.retryable = true;
    } else if (out.sqlstate == "57014") {
        out.code = error_code::timeout;
        out.retryable = true;
    } else if (out.sqlstate.rfind("08", 0) == 0 || out.sqlstate.empty()) {
        out.code = error_code::connection;
        out.retryable = true;
    } else {
        out.code = error_code::statement_failed;
    }
    if (out.message.empty()) out.message = "PostgreSQL operation failed";
    return out;
}

std::string value_to_text(const value& input) {
    if (std::holds_alternative<std::monostate>(input)) return {};
    if (const auto* number = std::get_if<int64_t>(&input)) return std::to_string(*number);
    if (const auto* flag = std::get_if<bool>(&input)) return *flag ? "1" : "0";
    if (const auto* number = std::get_if<double>(&input)) {
        std::ostringstream out;
        out << std::setprecision(17) << *number;
        return out.str();
    }
    if (const auto* text = std::get_if<std::string>(&input)) return *text;
    if (const auto* bytes = std::get_if<blob>(&input)) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out = "\\x";
        out.reserve(2 + bytes->size() * 2);
        for (std::byte byte : *bytes) {
            const unsigned int value = static_cast<unsigned int>(byte);
            out.push_back(digits[(value >> 4) & 0x0F]);
            out.push_back(digits[value & 0x0F]);
        }
        return out;
    }
    return {};
}

struct params_holder {
    std::vector<std::string> texts;
    std::vector<const char*> values;
    std::vector<int> lengths;
    std::vector<int> formats;
};

void bind_params(std::span<const value> params, params_holder& holder) {
    holder.texts.clear();
    holder.values.clear();
    holder.lengths.clear();
    holder.formats.clear();
    holder.texts.reserve(params.size());
    holder.values.reserve(params.size());
    holder.lengths.reserve(params.size());
    holder.formats.reserve(params.size());

    for (const auto& param : params) {
        if (!std::holds_alternative<std::monostate>(param)) {
            holder.texts.push_back(value_to_text(param));
        }
    }

    std::size_t text_index = 0;
    for (const auto& param : params) {
        if (std::holds_alternative<std::monostate>(param)) {
            holder.values.push_back(nullptr);
            holder.lengths.push_back(0);
        } else {
            holder.values.push_back(holder.texts[text_index].c_str());
            holder.lengths.push_back(static_cast<int>(holder.texts[text_index].size()));
            ++text_index;
        }
        holder.formats.push_back(0);
    }
}

result to_result(PGresult* raw) {
    std::vector<column> columns;
    const int field_count = PQnfields(raw);
    columns.reserve(static_cast<std::size_t>(field_count));
    for (int i = 0; i < field_count; ++i) {
        const char* name = PQfname(raw, i);
        columns.push_back(column{name ? name : ""});
    }

    std::vector<std::vector<std::optional<std::string>>> rows;
    const int row_count = PQntuples(raw);
    rows.reserve(static_cast<std::size_t>(row_count));
    for (int row = 0; row < row_count; ++row) {
        std::vector<std::optional<std::string>> values;
        values.reserve(static_cast<std::size_t>(field_count));
        for (int field = 0; field < field_count; ++field) {
            if (PQgetisnull(raw, row, field)) {
                values.emplace_back(std::nullopt);
            } else {
                values.emplace_back(std::string(PQgetvalue(raw, row, field)));
            }
        }
        rows.push_back(std::move(values));
    }

    std::size_t affected = 0;
    if (const char* command_tuples = PQcmdTuples(raw); command_tuples && *command_tuples) {
        affected = static_cast<std::size_t>(std::stoull(command_tuples));
    }
    return result(std::move(columns), std::move(rows), affected);
}

} // namespace

postgres_connection::postgres_connection(std::string conninfo)
    : conninfo_(std::move(conninfo)) {}

postgres_connection::~postgres_connection() {
    if (conn_) PQfinish(conn_);
}

bool postgres_connection::ensure_connected() {
    if (conn_ && PQstatus(conn_) == CONNECTION_OK) return true;
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
    conn_ = PQconnectdb(conninfo_.c_str());
    if (!conn_) {
        last_error_ = "PQconnectdb returned null";
        return false;
    }
    if (PQstatus(conn_) != CONNECTION_OK) {
        const char* message = PQerrorMessage(conn_);
        last_error_ = message ? message : "connection failed";
        PQfinish(conn_);
        conn_ = nullptr;
        return false;
    }
    PQsetClientEncoding(conn_, "UTF8");
    PGresult* settings = PQexec(conn_,
        "SET statement_timeout = '5s';"
        "SET lock_timeout = '2s';"
        "SET idle_in_transaction_session_timeout = '10s';"
        "SET application_name = 'ravenest';");
    if (settings) PQclear(settings);
    return true;
}

std::expected<result, error> postgres_connection::query(std::string_view sql,
                                                        std::span<const value> params) {
    if (!ensure_connected()) {
        return std::unexpected(error{error_code::connection, {}, last_error_, true});
    }
    params_holder holder;
    bind_params(params, holder);
    const std::string statement(sql);
    PGresult* raw = PQexecParams(conn_, statement.c_str(), static_cast<int>(params.size()),
                                 nullptr,
                                 holder.values.empty() ? nullptr : holder.values.data(),
                                 holder.lengths.empty() ? nullptr : holder.lengths.data(),
                                 holder.formats.empty() ? nullptr : holder.formats.data(), 0);
    if (!raw) return std::unexpected(map_error(conn_, nullptr));
    const int status = PQresultStatus(raw);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        error out = map_error(conn_, raw);
        PQclear(raw);
        return std::unexpected(out);
    }
    result out = to_result(raw);
    PQclear(raw);
    return out;
}

std::expected<std::size_t, error> postgres_connection::execute(std::string_view sql,
                                                               std::span<const value> params) {
    auto rows = query(sql, params);
    if (!rows) return std::unexpected(rows.error());
    return rows->affected_rows();
}

std::expected<int64_t, error> postgres_connection::insert_returning_id(
    std::string_view sql, std::span<const value> params) {
    auto inserted = execute(sql, params);
    if (!inserted) return std::unexpected(inserted.error());
    auto rows = query("SELECT LASTVAL()", {});
    if (!rows) return std::unexpected(rows.error());
    if (rows->row_count() == 0) {
        return std::unexpected(error{error_code::statement_failed, {}, "LASTVAL() returned no row"});
    }
    auto value = rows->at(0).get_optional<int64_t>(0);
    if (!value) {
        return std::unexpected(error{error_code::statement_failed, {}, "LASTVAL() returned NULL"});
    }
    return *value;
}

std::expected<void, error> postgres_connection::begin_write() {
    auto result_value = execute("BEGIN", {});
    if (!result_value) return std::unexpected(result_value.error());
    return {};
}

std::expected<void, error> postgres_connection::begin_read() {
    auto result_value = execute("BEGIN READ ONLY", {});
    if (!result_value) return std::unexpected(result_value.error());
    return {};
}

std::expected<void, error> postgres_connection::commit() {
    auto result_value = execute("COMMIT", {});
    if (!result_value) return std::unexpected(result_value.error());
    return {};
}

std::expected<void, error> postgres_connection::rollback() {
    auto result_value = execute("ROLLBACK", {});
    if (!result_value) return std::unexpected(result_value.error());
    return {};
}

bool postgres_connection::healthy() {
    auto rows = query("SELECT 1", {});
    return rows.has_value();
}

static_assert(connection_like<postgres_connection>,
              "postgres_connection must satisfy db::connection_like");

// Compile-time probe: keeps the executor/registry templates building even
// before every repository is migrated to the typed layer.
[[maybe_unused]] void compile_executor_probe() {
    auto factory = []() { return std::unique_ptr<postgres_connection>(); };
    db_registry<postgres_connection> registry(factory, factory, factory, factory, 1, 1);
    registry.shutdown();
}

} // namespace db
