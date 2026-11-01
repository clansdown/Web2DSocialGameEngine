#pragma once

// PostgreSQL-backed source-compatible adapter for the small subset of the
// sqlite_modern_cpp API used by the existing server modules.
//
// The rewrite moves the runtime to PostgreSQL, but the game has ~60 modules
// with embedded SQL. This adapter gives them one connection-per-thread and
// translates the few SQLite idioms still in use (`?` placeholders,
// INSERT OR IGNORE/REPLACE, LASTVAL, BEGIN IMMEDIATE). New code uses the
// db:: layer in server/db/Database.hpp directly.
//
// This is not a SQLite runtime: there is no SQLite dependency, no SQLite file,
// and no SQLite dialect support beyond the translations below.

#include "db/libpq_api.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace sqlite {

namespace detail {

struct param {
    bool is_null = true;
    std::string text;
};

inline std::string to_sql_string(const std::string& value) { return value; }
inline std::string to_sql_string(std::string_view value) { return std::string(value); }
inline std::string to_sql_string(const char* value) { return value ? std::string(value) : std::string(); }

inline std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

inline bool contains_ci(const std::string& haystack, std::string_view needle) {
    return upper(haystack).find(upper(std::string(needle))) != std::string::npos;
}

template <typename T>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
struct is_unique_ptr : std::false_type {};
template <typename T>
struct is_unique_ptr<std::unique_ptr<T>> : std::true_type {};

template <typename T>
struct function_traits;

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const> { using args = std::tuple<Args...>; };
template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...)> { using args = std::tuple<Args...>; };
template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const noexcept> { using args = std::tuple<Args...>; };
template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) noexcept> { using args = std::tuple<Args...>; };
template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> { using args = std::tuple<Args...>; };
template <typename R, typename... Args>
struct function_traits<std::function<R(Args...)>> { using args = std::tuple<Args...>; };

// Traits for callables: a lambda's operator() pointer is resolved through
// function_traits; function pointers and std::function have direct matches.
template <typename T>
struct callable_traits : function_traits<decltype(&T::operator())> {};
template <typename R, typename... Args>
struct callable_traits<R (*)(Args...)> { using args = std::tuple<Args...>; };
template <typename R, typename... Args>
struct callable_traits<std::function<R(Args...)>> { using args = std::tuple<Args...>; };

template <typename T, typename = void>
struct is_callable : std::false_type {};
template <typename T>
struct is_callable<T, std::void_t<decltype(&T::operator())>> : std::true_type {};
template <typename R, typename... Args>
struct is_callable<R (*)(Args...), void> : std::true_type {};
template <typename R, typename... Args>
struct is_callable<std::function<R(Args...)>, void> : std::true_type {};

template <typename T>
param to_param(const T& value) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, std::nullptr_t> || std::is_same_v<U, std::monostate>) {
        return {true, {}};
    } else if constexpr (is_optional<U>::value) {
        if (!value.has_value()) return {true, {}};
        return to_param(*value);
    } else if constexpr (is_unique_ptr<U>::value) {
        if (!value) return {true, {}};
        return to_param(*value);
    } else if constexpr (std::is_same_v<U, bool>) {
        return {false, value ? "1" : "0"};
    } else if constexpr (std::is_integral_v<U>) {
        return {false, std::to_string(value)};
    } else if constexpr (std::is_floating_point_v<U>) {
        std::ostringstream out;
        out << std::setprecision(17) << value;
        return {false, out.str()};
    } else if constexpr (std::is_same_v<U, std::string>) {
        return {false, value};
    } else if constexpr (std::is_same_v<U, std::string_view>) {
        return {false, std::string(value)};
    } else if constexpr (std::is_convertible_v<U, std::string>) {
        return {false, std::string(value)};
    } else if constexpr (requires { value.dump(); }) {
        return {false, value.dump()};
    } else {
        static_assert(sizeof(T) == 0, "sqlite compatibility adapter: unsupported bound parameter type");
    }
}

template <typename T>
T parse_integral(const std::string& text) {
    if (text == "t" || text == "true") return static_cast<T>(1);
    if (text == "f" || text == "false") return static_cast<T>(0);
    T out{};
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto result = std::from_chars(begin, end, out);
    if (result.ec != std::errc()) {
        throw std::runtime_error("sqlite compatibility adapter: cannot parse integer '" + text + "'");
    }
    return out;
}

template <typename U>
U read_value(PGresult* result, int row, int column) {
    if (PQgetisnull(result, row, column)) {
        if constexpr (is_optional<U>::value) {
            return std::nullopt;
        } else if constexpr (is_unique_ptr<U>::value) {
            return nullptr;
        } else if constexpr (std::is_same_v<U, std::string>) {
            return {};
        } else {
            return U{};
        }
    }
    const std::string text = PQgetvalue(result, row, column);
    if constexpr (is_optional<U>::value) {
        using V = typename U::value_type;
        return U(read_value<V>(result, row, column));
    } else if constexpr (is_unique_ptr<U>::value) {
        using V = typename U::element_type;
        return std::make_unique<V>(read_value<V>(result, row, column));
    } else if constexpr (std::is_same_v<U, bool>) {
        return text == "t" || text == "true" || text == "1";
    } else if constexpr (std::is_integral_v<U>) {
        return parse_integral<U>(text);
    } else if constexpr (std::is_floating_point_v<U>) {
        return static_cast<U>(std::stod(text));
    } else if constexpr (std::is_same_v<U, std::string>) {
        return text;
    } else if constexpr (std::is_same_v<U, std::string_view>) {
        return std::string_view(text);
    } else if constexpr (std::is_convertible_v<std::string, U>) {
        return U(text);
    } else {
        static_assert(sizeof(U) == 0, "sqlite compatibility adapter: unsupported result column type");
    }
}

template <typename Tuple, typename F, std::size_t... I>
void call_row(F& function, PGresult* result, int row, std::index_sequence<I...>) {
    function(read_value<std::remove_cvref_t<std::tuple_element_t<I, Tuple>>>(result, row, static_cast<int>(I))...);
}

inline std::string join(const std::vector<std::string>& values, const char* separator) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += separator;
        out += values[i];
    }
    return out;
}

inline std::vector<std::string> split_columns(const std::string& text) {
    std::vector<std::string> columns;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            if (!current.empty()) columns.push_back(current);
            current.clear();
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            current += c;
        }
    }
    if (!current.empty()) columns.push_back(current);
    return columns;
}

inline std::string trim_statement(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    while (!value.empty() && value.back() == ';') value.pop_back();
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

} // namespace detail

class database;

class statement {
public:
    statement(database& owner, std::string sql)
        : owner_(owner), sql_(std::move(sql)) {}

    statement(const statement&) = delete;
    statement& operator=(const statement&) = delete;

    statement(statement&& other) noexcept
        : owner_(other.owner_), sql_(std::move(other.sql_)),
          params_(std::move(other.params_)), consumed_(other.consumed_) {
        other.consumed_ = true;
    }

    statement& operator=(statement&&) = delete;
    ~statement();

    template <typename T>
    statement& operator<<(const T& value) {
        params_.push_back(detail::to_param(value));
        return *this;
    }

    template <typename F>
        requires detail::is_callable<std::remove_cvref_t<F>>::value
    statement& operator>>(F&& function);

    template <typename T>
        requires (!detail::is_callable<std::remove_cvref_t<T>>::value)
    statement& operator>>(T& value);

private:
    database& owner_;
    std::string sql_;
    std::vector<detail::param> params_;
    bool consumed_ = false;
};

class database {
public:
    database() = default;
    explicit database(std::string conninfo)
        : pool_(std::make_shared<connection_pool>(std::move(conninfo))) {}

    database(const database&) = default;
    database& operator=(const database&) = default;

    template <typename Sql>
    statement operator<<(Sql&& sql) {
        return statement(*this, detail::to_sql_string(std::forward<Sql>(sql)));
    }

    int64_t last_insert_rowid() const {
        PGconn* conn = connection();
        PGresult* result = PQexec(conn, "SELECT LASTVAL()");
        if (!result) {
            throw std::runtime_error(std::string("postgres: ") + PQerrorMessage(conn));
        }
        int64_t value = 0;
        if (PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) > 0 &&
            !PQgetisnull(result, 0, 0)) {
            try {
                value = std::stoll(PQgetvalue(result, 0, 0));
            } catch (...) {
                value = 0;
            }
        }
        PQclear(result);
        return value;
    }

    const std::string& conninfo() const {
        static const std::string empty;
        return pool_ ? pool_->conninfo : empty;
    }
    bool initialized() const { return pool_ && !pool_->conninfo.empty(); }

    template <typename F>
    void execute_query(const std::string& sql, const std::vector<detail::param>& params, F&& function) const {
        PGconn* conn = connection();
        std::string rewritten = rewrite_sql(conn, sql);
        PGresult* result = run_params(conn, rewritten, params);
        struct result_guard {
            PGresult* value;
            ~result_guard() { if (value) PQclear(value); }
        } guard{result};
        throw_on_error(conn, result, rewritten);
        using args = typename detail::callable_traits<std::decay_t<F>>::args;
        constexpr std::size_t count = std::tuple_size_v<args>;
        if (PQnfields(result) < static_cast<int>(count)) {
            throw std::runtime_error("sqlite compatibility adapter: result has fewer columns than the row callback");
        }
        for (int row = 0; row < PQntuples(result); ++row) {
            detail::call_row<args>(function, result, row, std::make_index_sequence<count>{});
        }
    }

    void execute_command(const std::string& sql, const std::vector<detail::param>& params) const {
        PGconn* conn = connection();
        std::string rewritten = rewrite_sql(conn, sql);
        PGresult* result = run_params(conn, rewritten, params);
        struct result_guard {
            PGresult* value;
            ~result_guard() { if (value) PQclear(value); }
        } guard{result};
        throw_on_error(conn, result, rewritten);
    }

    // Single-value form: `db << "SELECT ..." >> value;`
    template <typename U>
    void read_one(const std::string& sql, const std::vector<detail::param>& params, U& out) const {
        PGconn* conn = connection();
        std::string rewritten = rewrite_sql(conn, sql);
        PGresult* result = run_params(conn, rewritten, params);
        struct result_guard {
            PGresult* value;
            ~result_guard() { if (value) PQclear(value); }
        } guard{result};
        throw_on_error(conn, result, rewritten);
        if (PQntuples(result) > 0 && PQnfields(result) > 0) {
            out = detail::read_value<U>(result, 0, 0);
        }
    }

private:
    struct connection_pool {
        explicit connection_pool(std::string conninfo_value) : conninfo(std::move(conninfo_value)) {}
        ~connection_pool() {
            for (auto& entry : connections) {
                if (entry.second) PQfinish(entry.second);
            }
        }

        std::string conninfo;
        mutable std::mutex mutex;
        mutable std::unordered_map<std::thread::id, PGconn*> connections;
    };

    PGconn* connection() const {
        if (!pool_ || pool_->conninfo.empty()) {
            throw std::runtime_error("postgres: database is not initialized");
        }
        const std::thread::id thread_id = std::this_thread::get_id();
        {
            std::lock_guard<std::mutex> lock(pool_->mutex);
            auto it = pool_->connections.find(thread_id);
            if (it != pool_->connections.end()) {
                if (PQstatus(it->second) == CONNECTION_OK) return it->second;
                PQfinish(it->second);
                it->second = nullptr;
            }
        }

        PGconn* conn = PQconnectdb(pool_->conninfo.c_str());
        if (!conn) {
            throw std::runtime_error("postgres: PQconnectdb returned null");
        }
        if (PQstatus(conn) != CONNECTION_OK) {
            const std::string message = PQerrorMessage(conn);
            PQfinish(conn);
            throw std::runtime_error("postgres: connection failed: " + message);
        }
        configure_connection(conn);
        std::lock_guard<std::mutex> lock(pool_->mutex);
        pool_->connections[thread_id] = conn;
        return conn;
    }

    static void configure_connection(PGconn* conn) {
        PQsetClientEncoding(conn, "UTF8");
        const char* settings =
            "SET statement_timeout = '5s';"
            "SET lock_timeout = '2s';"
            "SET idle_in_transaction_session_timeout = '10s';"
            "SET application_name = 'ravenest';";
        PGresult* result = PQexec(conn, settings);
        if (result) PQclear(result);
    }

    static void throw_on_error(PGconn* conn, PGresult* result, const std::string& sql) {
        if (!result) {
            throw std::runtime_error(std::string("postgres: ") + PQerrorMessage(conn));
        }
        const int status = PQresultStatus(result);
        if (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK || status == PGRES_EMPTY_QUERY) {
            return;
        }
        std::string message = PQresultErrorMessage(result);
        if (message.empty()) message = PQerrorMessage(conn);
        std::string excerpt = sql.size() > 400 ? sql.substr(0, 400) + "..." : sql;
        throw std::runtime_error("postgres: " + message + " [sql: " + excerpt + "]");
    }

    static PGresult* run_params(PGconn* conn, const std::string& sql,
                                const std::vector<detail::param>& params) {
        std::vector<const char*> values;
        std::vector<int> lengths;
        std::vector<int> formats;
        values.reserve(params.size());
        lengths.reserve(params.size());
        formats.reserve(params.size());
        for (const auto& value : params) {
            values.push_back(value.is_null ? nullptr : value.text.c_str());
            lengths.push_back(static_cast<int>(value.text.size()));
            formats.push_back(0);
        }
        return PQexecParams(conn, sql.c_str(), static_cast<int>(params.size()), nullptr,
                            values.empty() ? nullptr : values.data(),
                            lengths.empty() ? nullptr : lengths.data(),
                            formats.empty() ? nullptr : formats.data(), 0);
    }

    static std::vector<std::string> primary_key_columns(PGconn* conn, const std::string& table) {
        const char* sql =
            "SELECT a.attname "
            "FROM pg_index i "
            "JOIN pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = ANY(i.indkey) "
            "WHERE i.indrelid = to_regclass($1) AND i.indisprimary "
            "ORDER BY array_position(i.indkey, a.attnum)";
        const char* values[1] = {table.c_str()};
        PGresult* result = PQexecParams(conn, sql, 1, nullptr, values, nullptr, nullptr, 0);
        std::vector<std::string> columns;
        if (result && PQresultStatus(result) == PGRES_TUPLES_OK) {
            for (int row = 0; row < PQntuples(result); ++row) {
                columns.emplace_back(PQgetvalue(result, row, 0));
            }
        }
        if (result) PQclear(result);
        return columns;
    }

    static std::string append_conflict_clause(std::string body,
                                              const std::vector<std::string>& columns,
                                              const std::vector<std::string>& primary_keys) {
        body = detail::trim_statement(std::move(body));
        if (detail::contains_ci(body, "ON CONFLICT")) {
            return body + ";";
        }
        if (primary_keys.empty()) {
            return body + " ON CONFLICT DO NOTHING;";
        }
        body += " ON CONFLICT (" + detail::join(primary_keys, ", ") + ")";
        std::vector<std::string> updates;
        for (const auto& column : columns) {
            if (std::find(primary_keys.begin(), primary_keys.end(), column) == primary_keys.end()) {
                updates.push_back(column + " = EXCLUDED." + column);
            }
        }
        if (updates.empty()) {
            body += " DO NOTHING;";
        } else {
            body += " DO UPDATE SET " + detail::join(updates, ", ") + ";";
        }
        return body;
    }

    static std::string rewrite_sql(PGconn* conn, const std::string& input) {
        std::string sql = input;

        // SQLite placeholders: ? -> $1, $2, ... outside string literals.
        std::string placeholders;
        placeholders.reserve(sql.size() + 16);
        bool in_single = false;
        bool in_double = false;
        int parameter_index = 0;
        for (std::size_t i = 0; i < sql.size(); ++i) {
            const char c = sql[i];
            if (c == '\'' && !in_double) {
                if (in_single && i + 1 < sql.size() && sql[i + 1] == '\'') {
                    placeholders += "''";
                    ++i;
                    continue;
                }
                in_single = !in_single;
                placeholders += c;
            } else if (c == '"' && !in_single) {
                in_double = !in_double;
                placeholders += c;
            } else if (c == '?' && !in_single && !in_double) {
                ++parameter_index;
                placeholders += "$" + std::to_string(parameter_index);
            } else {
                placeholders += c;
            }
        }
        sql = std::move(placeholders);

        // SQLite statement idioms.
        if (detail::contains_ci(sql, "BEGIN IMMEDIATE")) {
            const std::string upper = detail::upper(sql);
            const std::size_t pos = upper.find("BEGIN IMMEDIATE");
            sql.replace(pos, std::string("BEGIN IMMEDIATE").size(), "BEGIN");
        }
        if (detail::contains_ci(sql, "STRFTIME('%S','NOW')")) {
            // Best-effort translation for the only legacy timestamp form.
            const std::string upper = detail::upper(sql);
            const std::size_t pos = upper.find("STRFTIME('%S','NOW')");
            if (pos != std::string::npos) {
                sql.replace(pos, std::string("STRFTIME('%S','NOW')").size(),
                            "EXTRACT(EPOCH FROM NOW())::bigint");
            }
        }
        if (detail::contains_ci(sql, "LAST_INSERT_ROWID()")) {
            const std::string upper = detail::upper(sql);
            const std::size_t pos = upper.find("LAST_INSERT_ROWID()");
            if (pos != std::string::npos) {
                sql.replace(pos, std::string("LAST_INSERT_ROWID()").size(), "LASTVAL()");
            }
        }
        if (detail::contains_ci(sql, "INSTR(")) {
            const std::string upper = detail::upper(sql);
            const std::size_t pos = upper.find("INSTR(");
            if (pos != std::string::npos) sql.replace(pos, 6, "STRPOS(");
        }
        if (detail::contains_ci(sql, "IFNULL(")) {
            const std::string upper = detail::upper(sql);
            const std::size_t pos = upper.find("IFNULL(");
            if (pos != std::string::npos) sql.replace(pos, 7, "COALESCE(");
        }
        if (detail::contains_ci(sql, "GROUP_CONCAT(")) {
            const std::string upper = detail::upper(sql);
            const std::size_t pos = upper.find("GROUP_CONCAT(");
            if (pos != std::string::npos) sql.replace(pos, 13, "STRING_AGG(");
        }

        // INSERT OR IGNORE / INSERT OR REPLACE.
        if (detail::contains_ci(sql, "INSERT OR IGNORE INTO")) {
            const std::string upper = detail::upper(sql);
            const std::size_t pos = upper.find("INSERT OR IGNORE INTO");
            sql.replace(pos, std::string("INSERT OR IGNORE INTO").size(), "INSERT INTO");
            sql = detail::trim_statement(std::move(sql));
            if (!detail::contains_ci(sql, "ON CONFLICT")) {
                sql += " ON CONFLICT DO NOTHING";
            }
            sql += ";";
        } else if (detail::contains_ci(sql, "INSERT OR REPLACE INTO")) {
            const std::string upper = detail::upper(sql);
            const std::size_t pos = upper.find("INSERT OR REPLACE INTO");
            const std::string rest = sql.substr(pos + std::string("INSERT OR REPLACE INTO").size());
            std::string body = "INSERT INTO" + rest;
            const std::string body_upper = detail::upper(body);
            const std::size_t into_pos = body_upper.find("INTO") + 4;
            std::size_t table_begin = into_pos;
            while (table_begin < body.size() && std::isspace(static_cast<unsigned char>(body[table_begin]))) {
                ++table_begin;
            }
            std::size_t table_end = table_begin;
            while (table_end < body.size() && !std::isspace(static_cast<unsigned char>(body[table_end])) &&
                   body[table_end] != '(') {
                ++table_end;
            }
            const std::string table = body.substr(table_begin, table_end - table_begin);
            std::vector<std::string> columns;
            const std::size_t open = body.find('(', table_end);
            const std::size_t close = open == std::string::npos ? std::string::npos : body.find(')', open);
            if (open != std::string::npos && close != std::string::npos) {
                columns = detail::split_columns(body.substr(open + 1, close - open - 1));
            }
            const auto primary_keys = primary_key_columns(conn, table);
            sql = append_conflict_clause(std::move(body), columns, primary_keys);
        }

        return sql;
    }

    std::shared_ptr<connection_pool> pool_;
};

template <typename F>
    requires detail::is_callable<std::remove_cvref_t<F>>::value
statement& statement::operator>>(F&& function) {
    consumed_ = true;
    owner_.execute_query(sql_, params_, std::forward<F>(function));
    return *this;
}

template <typename T>
    requires (!detail::is_callable<std::remove_cvref_t<T>>::value)
statement& statement::operator>>(T& value) {
    consumed_ = true;
    owner_.read_one(sql_, params_, value);
    return *this;
}

inline statement::~statement() {
    if (consumed_) return;
    try {
        owner_.execute_command(sql_, params_);
    } catch (...) {
        // Destructors never throw. The caller either used >> (which propagates
        // errors) or intentionally ignored the result of an execute.
    }
}

} // namespace sqlite
