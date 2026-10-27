#include "MonthlyPass.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace monthly_pass {

namespace {

struct persisted_row {
    bool found = false;
    int64_t expires_at = 0;
    bool permanent = false;
};

persisted_row read_row(sqlite::database& db, int64_t character_id) {
    persisted_row row;
    db << "SELECT expires_at, permanent FROM character_monthly_pass WHERE character_id = ?;"
       << character_id
       >> [&](int64_t expires_at, int permanent) {
              row.found = true;
              row.expires_at = expires_at;
              row.permanent = permanent != 0;
          };
    return row;
}

void write_row(sqlite::database& db, int64_t character_id, int64_t expires_at,
               bool permanent, int64_t now) {
    db << "INSERT INTO character_monthly_pass "
          "(character_id, expires_at, permanent, updated_at) VALUES (?, ?, ?, ?) "
          "ON CONFLICT(character_id) DO UPDATE SET "
          "expires_at = excluded.expires_at, "
          "permanent = excluded.permanent, "
          "updated_at = excluded.updated_at;"
       << character_id << expires_at << (permanent ? 1 : 0) << now;
}

void append_log(sqlite::database& db, int64_t character_id, const std::string& event,
                const grant_origin& origin, int months, bool permanent,
                int64_t expires_at, int64_t now) {
    db << "INSERT INTO character_monthly_pass_log "
          "(character_id, event, source, months, permanent, expires_at, reason, "
          "staff_account_id, product_id, reference_id, created_at) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"
       << character_id << event << origin.source << months << (permanent ? 1 : 0)
       << expires_at << origin.reason << origin.staff_account_id << origin.product_id
       << origin.reference_id << now;
}

void delete_row(sqlite::database& db, int64_t character_id) {
    db << "DELETE FROM character_monthly_pass WHERE character_id = ?;" << character_id;
}

} // namespace

nlohmann::json status::to_json() const {
    return {
        {"active", active},
        {"permanent", permanent},
        {"expires_at", expires_at},
    };
}

nlohmann::json log_entry::to_json() const {
    return {
        {"id", id},
        {"character_id", character_id},
        {"event", event},
        {"source", source},
        {"months", months},
        {"permanent", permanent},
        {"expires_at", expires_at},
        {"reason", reason},
        {"staff_account_id", staff_account_id},
        {"product_id", product_id},
        {"reference_id", reference_id},
        {"created_at", created_at},
    };
}

status get_status(sqlite::database& db, int64_t character_id, int64_t now) {
    const persisted_row row = read_row(db, character_id);
    status current;
    current.permanent = row.permanent;
    current.expires_at = row.expires_at;
    current.active = row.permanent || (row.found && row.expires_at > now);
    return current;
}

status grant_months(sqlite::database& db, int64_t character_id, int months,
                    const grant_origin& origin, int64_t now) {
    if (months < 1 || months > k_max_months) {
        throw std::invalid_argument("invalid_month_count");
    }

    const persisted_row row = read_row(db, character_id);
    if (row.permanent) {
        // Permanent already covers every future month; do not append a no-op
        // event or extend a meaningless expiry.
        return get_status(db, character_id, now);
    }

    const int64_t base = row.found ? std::max(now, row.expires_at) : now;
    const int64_t expires_at = base + static_cast<int64_t>(months) * k_month_seconds;
    write_row(db, character_id, expires_at, false, now);
    append_log(db, character_id, "grant", origin, months, false, expires_at, now);
    return {true, false, expires_at};
}

status grant_permanent(sqlite::database& db, int64_t character_id,
                       const grant_origin& origin, int64_t now) {
    const persisted_row row = read_row(db, character_id);
    if (row.permanent) {
        return get_status(db, character_id, now);
    }

    write_row(db, character_id, 0, true, now);
    append_log(db, character_id, "grant", origin, 0, true, 0, now);
    return {true, true, 0};
}

void revoke(sqlite::database& db, int64_t character_id, const grant_origin& origin,
            int64_t now) {
    delete_row(db, character_id);
    append_log(db, character_id, "revoke", origin, 0, false, 0, now);
}

void restore(sqlite::database& db, int64_t character_id, const status& previous,
             const grant_origin& origin, int64_t now) {
    // A prior expired row is still a row worth restoring byte-for-byte; only a
    // truly absent entitlement becomes a revoke event.
    const bool had_row = previous.active || previous.permanent || previous.expires_at > 0;
    if (had_row) {
        write_row(db, character_id, previous.expires_at, previous.permanent, now);
        append_log(db, character_id, "restore", origin, 0, previous.permanent,
                   previous.expires_at, now);
        return;
    }

    delete_row(db, character_id);
    append_log(db, character_id, "revoke", origin, 0, false, 0, now);
}

std::vector<log_entry> list_log(sqlite::database& db, int64_t character_id,
                                int limit) {
    const int bounded_limit = std::clamp(limit, 1, 200);
    std::vector<log_entry> entries;
    db << "SELECT id, character_id, event, source, months, permanent, expires_at, "
          "reason, staff_account_id, product_id, reference_id, created_at "
          "FROM character_monthly_pass_log WHERE character_id = ? "
          "ORDER BY created_at DESC, id DESC LIMIT ?;"
       << character_id << bounded_limit
       >> [&](int64_t id, int64_t row_character_id, std::string event,
              std::string source, int months, int permanent, int64_t expires_at,
              std::string reason, int64_t staff_account_id, std::string product_id,
              std::string reference_id, int64_t created_at) {
              log_entry entry;
              entry.id = id;
              entry.character_id = row_character_id;
              entry.event = std::move(event);
              entry.source = std::move(source);
              entry.months = months;
              entry.permanent = permanent != 0;
              entry.expires_at = expires_at;
              entry.reason = std::move(reason);
              entry.staff_account_id = staff_account_id;
              entry.product_id = std::move(product_id);
              entry.reference_id = std::move(reference_id);
              entry.created_at = created_at;
              entries.push_back(std::move(entry));
          };
    return entries;
}

} // namespace monthly_pass
