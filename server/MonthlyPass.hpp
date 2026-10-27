#pragma once
#include <nlohmann/json.hpp>
#include <sqlite_modern_cpp.h>
#include <cstdint>
#include <string>
#include <vector>

// Per-character monthly-pass entitlement plus its append-only event log.
//
// The current state lives in `character_monthly_pass` (one row per character).
// Every grant / revoke / undo transition appends an immutable row to
// `character_monthly_pass_log`, including the source (staff grant, purchase,
// promo) and the staff-entered reason. The log is never updated or deleted, so
// it remains a historical record even after a later revocation or undo.
namespace monthly_pass {

// A "month" is deliberately 31 days: deterministic, timezone-free, and
// convenient for staff grants of arbitrary month counts.
inline constexpr int64_t k_month_seconds = 31LL * 24LL * 60LL * 60LL;

// 100 years. This is effectively unrestricted for staff grants while keeping
// integer arithmetic far away from overflow.
inline constexpr int k_max_months = 1200;

struct status {
    bool active = false;
    bool permanent = false;
    int64_t expires_at = 0;

    nlohmann::json to_json() const;
};

// Provenance written to the log alongside every state transition.
struct grant_origin {
    std::string source;             // staff_grant | staff_revoke | staff_undo | purchase | promo
    std::string reason;             // staff-entered reason, or purchase description
    int64_t staff_account_id = 0;   // 0 for non-staff origins
    std::string product_id;         // future purchases
    std::string reference_id;       // staff action id, or purchase idempotency key
};

struct log_entry {
    int64_t id = 0;
    int64_t character_id = 0;
    std::string event;              // grant | revoke | restore
    std::string source;
    int months = 0;
    bool permanent = false;
    int64_t expires_at = 0;
    std::string reason;
    int64_t staff_account_id = 0;
    std::string product_id;
    std::string reference_id;
    int64_t created_at = 0;

    nlohmann::json to_json() const;
};

status get_status(sqlite::database& db, int64_t character_id, int64_t now);

status grant_months(sqlite::database& db, int64_t character_id, int months,
                    const grant_origin& origin, int64_t now);

status grant_permanent(sqlite::database& db, int64_t character_id,
                       const grant_origin& origin, int64_t now);

// Removes the current entitlement. Always appends a `revoke` log row.
void revoke(sqlite::database& db, int64_t character_id, const grant_origin& origin,
            int64_t now);

// Restores a prior status for an undo. Appends `restore` when the prior state
// had a row (including an expired one), or `revoke` when no prior row existed.
void restore(sqlite::database& db, int64_t character_id, const status& previous,
             const grant_origin& origin, int64_t now);

std::vector<log_entry> list_log(sqlite::database& db, int64_t character_id,
                                int limit = 20);

} // namespace monthly_pass
