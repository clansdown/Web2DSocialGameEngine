#pragma once

#include "db/Database.hpp"
#include "db/libpq_api.hpp"

#include <string>

namespace db {

class postgres_connection {
public:
    explicit postgres_connection(std::string conninfo);
    ~postgres_connection();

    postgres_connection(const postgres_connection&) = delete;
    postgres_connection& operator=(const postgres_connection&) = delete;
    postgres_connection(postgres_connection&&) = delete;
    postgres_connection& operator=(postgres_connection&&) = delete;

    std::expected<result, error> query(std::string_view sql, std::span<const value> params);
    std::expected<std::size_t, error> execute(std::string_view sql, std::span<const value> params);
    std::expected<int64_t, error> insert_returning_id(std::string_view sql, std::span<const value> params);

    std::expected<void, error> begin_write();
    std::expected<void, error> begin_read();
    std::expected<void, error> commit();
    std::expected<void, error> rollback();

    bool healthy();

    const std::string& conninfo() const { return conninfo_; }

private:
    bool ensure_connected();
    PGconn* conn_ = nullptr;
    std::string conninfo_;
    std::string last_error_;
};

} // namespace db
