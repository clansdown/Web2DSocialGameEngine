#pragma once

// Backend-neutral database primitives. Production uses the compile-time
// postgres_connection; tests can supply any type satisfying connection_like.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

namespace db {

using blob = std::vector<std::byte>;

using value = std::variant<
    std::monostate,  // NULL
    int64_t,
    double,
    bool,
    std::string,
    blob
>;

enum class error_code {
    connection,
    timeout,
    unique_violation,
    foreign_key_violation,
    serialization_failure,
    deadlock,
    statement_failed,
    unknown
};

struct error {
    error_code code = error_code::unknown;
    std::string sqlstate;
    std::string message;
    bool retryable = false;
};

struct column {
    std::string name;
};

class result;

namespace detail {

template <typename T>
std::optional<T> parse_optional(const std::optional<std::string>& text) {
    if (!text.has_value()) return std::nullopt;
    const std::string& value = *text;
    if constexpr (std::is_same_v<T, bool>) {
        return value == "1" || value == "t" || value == "true";
    } else if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else if constexpr (std::is_integral_v<T>) {
        if (value == "t" || value == "true") return static_cast<T>(1);
        if (value == "f" || value == "false") return static_cast<T>(0);
        T out{};
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), out);
        if (parsed.ec != std::errc()) return std::nullopt;
        return out;
    } else if constexpr (std::is_floating_point_v<T>) {
        try {
            return static_cast<T>(std::stod(value));
        } catch (...) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
}

} // namespace detail

class row {
public:
    row() = default;
    row(const std::vector<std::optional<std::string>>* values,
        const std::vector<column>* columns)
        : values_(values), columns_(columns) {}

    bool is_null(std::size_t index) const {
        return !values_ || index >= values_->size() || !(*values_)[index].has_value();
    }

    template <typename T>
    T get(std::size_t index) const {
        auto value = get_optional<T>(index);
        if (!value.has_value()) {
            throw std::runtime_error("db: column is NULL or not convertible");
        }
        return *value;
    }

    template <typename T>
    std::optional<T> get_optional(std::size_t index) const {
        if (!values_ || index >= values_->size()) return std::nullopt;
        return detail::parse_optional<T>((*values_)[index]);
    }

    template <typename T>
    T get(std::string_view name) const {
        if (!columns_) throw std::runtime_error("db: row has no columns");
        for (std::size_t i = 0; i < columns_->size(); ++i) {
            if ((*columns_)[i].name == name) return get<T>(i);
        }
        throw std::runtime_error("db: unknown column " + std::string(name));
    }

private:
    const std::vector<std::optional<std::string>>* values_ = nullptr;
    const std::vector<column>* columns_ = nullptr;
};

class result {
public:
    result() = default;
    result(std::vector<column> columns,
           std::vector<std::vector<std::optional<std::string>>> rows,
           std::size_t affected_rows = 0)
        : columns_(std::move(columns)), rows_(std::move(rows)), affected_rows_(affected_rows) {}

    std::span<const column> columns() const { return columns_; }
    std::size_t row_count() const { return rows_.size(); }
    std::size_t affected_rows() const { return affected_rows_; }

    row at(std::size_t index) const {
        if (index >= rows_.size()) throw std::out_of_range("db: row index out of range");
        return row(&rows_[index], &columns_);
    }

private:
    std::vector<column> columns_;
    std::vector<std::vector<std::optional<std::string>>> rows_;
    std::size_t affected_rows_ = 0;
};

template <typename T>
concept connection_like = requires(T& connection,
                                   std::string_view sql,
                                   std::span<const value> params) {
    { connection.query(sql, params) } -> std::same_as<std::expected<result, error>>;
    { connection.execute(sql, params) } -> std::same_as<std::expected<std::size_t, error>>;
    { connection.insert_returning_id(sql, params) } -> std::same_as<std::expected<int64_t, error>>;
    { connection.begin_write() } -> std::same_as<std::expected<void, error>>;
    { connection.begin_read() } -> std::same_as<std::expected<void, error>>;
    { connection.commit() } -> std::same_as<std::expected<void, error>>;
    { connection.rollback() } -> std::same_as<std::expected<void, error>>;
    { connection.healthy() } -> std::same_as<bool>;
};

template <typename T>
concept query_executor = requires(T& executor,
                                  std::string_view sql,
                                  std::span<const value> params) {
    { executor.query(sql, params) } -> std::same_as<std::expected<result, error>>;
    { executor.execute(sql, params) } -> std::same_as<std::expected<std::size_t, error>>;
};

template <connection_like Connection>
class transaction {
public:
    explicit transaction(Connection& connection) : connection_(&connection) {}

    transaction(const transaction&) = delete;
    transaction& operator=(const transaction&) = delete;

    ~transaction() {
        if (active_) {
            (void)connection_->rollback();
        }
    }

    std::expected<void, error> begin_write() {
        auto result_value = connection_->begin_write();
        if (result_value) active_ = true;
        return result_value;
    }

    std::expected<void, error> begin_read() {
        auto result_value = connection_->begin_read();
        if (result_value) active_ = true;
        return result_value;
    }

    std::expected<result, error> query(std::string_view sql, std::span<const value> params) {
        return connection_->query(sql, params);
    }

    std::expected<std::size_t, error> execute(std::string_view sql, std::span<const value> params) {
        return connection_->execute(sql, params);
    }

    std::expected<int64_t, error> insert_returning_id(std::string_view sql,
                                                      std::span<const value> params) {
        return connection_->insert_returning_id(sql, params);
    }

    std::expected<void, error> commit() {
        auto result_value = connection_->commit();
        if (result_value) active_ = false;
        return result_value;
    }

    void rollback() {
        if (active_) {
            (void)connection_->rollback();
            active_ = false;
        }
    }

private:
    Connection* connection_ = nullptr;
    bool active_ = false;
};

} // namespace db
