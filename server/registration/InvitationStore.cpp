#include "registration/InvitationStore.hpp"

#include <openssl/rand.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <random>
#include <sstream>

#include "Database.hpp"
#include "init_db.hpp"

namespace {

constexpr const char* kCrockfordAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr std::size_t kCodeLength = 12;

std::int64_t now_seconds_utc() {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

bool random_bytes(unsigned char* out, std::size_t count) {
    if (RAND_bytes(out, static_cast<int>(count)) == 1) return true;
    // RAND_bytes failure is exceptionally rare; keep registration functional
    // with the system entropy source rather than aborting.
    std::random_device device;
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<unsigned char>(device() & 0xFF);
    }
    return true;
}

invitation_code_record read_record_row(
    const std::string& id, const std::string& code, std::int64_t created_by_account_id,
    const std::string& created_by_kind, std::int64_t created_at, std::int64_t used_at,
    std::int64_t used_by_account_id, std::int64_t revoked_at,
    std::int64_t revoked_by_account_id) {
    invitation_code_record record;
    record.id = id;
    record.code = code;
    record.created_by_account_id = created_by_account_id;
    record.created_by_kind = created_by_kind;
    record.created_at = created_at;
    record.used_at = used_at;
    record.used_by_account_id = used_by_account_id;
    record.revoked_at = revoked_at;
    record.revoked_by_account_id = revoked_by_account_id;
    return record;
}

constexpr const char* kSelectColumns =
    "id, code, created_by_account_id, created_by_kind, created_at, "
    "COALESCE(used_at, 0), COALESCE(used_by_account_id, 0), "
    "COALESCE(revoked_at, 0), COALESCE(revoked_by_account_id, 0)";

void bind_record_reader(sqlite::database& db, const std::string& sql,
                        const std::string& parameter,
                        std::optional<invitation_code_record>& out) {
    db << sql << parameter
       >> [&](std::string id, std::string code, std::int64_t created_by_account_id,
              std::string created_by_kind, std::int64_t created_at, std::int64_t used_at,
              std::int64_t used_by_account_id, std::int64_t revoked_at,
              std::int64_t revoked_by_account_id) {
              out = read_record_row(std::move(id), std::move(code), created_by_account_id,
                                    std::move(created_by_kind), created_at, used_at,
                                    used_by_account_id, revoked_at, revoked_by_account_id);
          };
}

}  // namespace

InvitationStore& InvitationStore::instance() {
    static InvitationStore store;
    return store;
}

void InvitationStore::ensure_ready() {
    if (initialized_) return;
    auto& db = Database::getInstance().invitesDB();
    initializeInvitesDB(db);
    initialized_ = true;
}

std::optional<invitation_code_record> InvitationStore::find_by_code(
    const std::string& canonical_code) {
    ensure_ready();
    std::optional<invitation_code_record> out;
    const std::string sql = std::string("SELECT ") + kSelectColumns +
                            " FROM invitation_codes WHERE code = ? LIMIT 1;";
    bind_record_reader(Database::getInstance().invitesDB(), sql, canonical_code, out);
    return out;
}

std::optional<invitation_code_record> InvitationStore::find_by_id(const std::string& id) {
    ensure_ready();
    std::optional<invitation_code_record> out;
    const std::string sql = std::string("SELECT ") + kSelectColumns +
                            " FROM invitation_codes WHERE id = ? LIMIT 1;";
    bind_record_reader(Database::getInstance().invitesDB(), sql, id, out);
    return out;
}

bool InvitationStore::is_redeemed_in_game(const std::string& id) {
    if (id.empty()) return false;
    bool found = false;
    auto& db = Database::getInstance().gameDB();
    db << "SELECT 1 FROM users WHERE invitation_code_id = ? LIMIT 1;" << id
       >> [&](int) { found = true; };
    return found;
}

invitation_code_draft InvitationStore::make_code_draft() const {
    invitation_code_draft draft;
    draft.id = generate_invitation_code_id();
    draft.code = generate_invitation_code();
    return draft;
}

bool InvitationStore::insert_code(const invitation_code_draft& draft,
                                  std::int64_t account_id,
                                  const std::string& created_by_kind,
                                  std::int64_t created_at, std::string& error) {
    ensure_ready();
    if (draft.id.empty() || draft.code.empty()) {
        error = "invalid_invitation_code";
        return false;
    }
    try {
        auto& db = Database::getInstance().invitesDB();
        db << "INSERT INTO invitation_codes "
              "(id, code, created_by_account_id, created_by_kind, created_at) "
              "VALUES (?, ?, ?, ?, ?);"
           << draft.id << draft.code << account_id << created_by_kind << created_at;
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::optional<invitation_code_record> InvitationStore::create_code(
    std::int64_t account_id, const std::string& created_by_kind, std::string& error) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        const invitation_code_draft draft = make_code_draft();
        if (!insert_code(draft, account_id, created_by_kind, now_seconds_utc(), error)) {
            if (error.find("UNIQUE") != std::string::npos) {
                continue;  // extremely unlikely 12-char collision; try again
            }
            return std::nullopt;
        }
        return find_by_id(draft.id);
    }
    error = "invitation_code_generation_failed";
    return std::nullopt;
}

bool InvitationStore::mark_used(const std::string& id, std::int64_t account_id,
                                std::int64_t used_at, std::string& error) {
    ensure_ready();
    try {
        auto& db = Database::getInstance().invitesDB();
        db << "UPDATE invitation_codes SET used_at = ?, used_by_account_id = ? "
              "WHERE id = ? AND used_at IS NULL AND revoked_at IS NULL;"
           << used_at << account_id << id;
        int changed = 0;
        db << "SELECT changes();" >> changed;
        if (changed == 0) {
            error = "invitation_code_not_usable";
            return false;
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool InvitationStore::revoke_code(const std::string& id, std::int64_t staff_account_id,
                                  std::int64_t revoked_at, std::string& error) {
    ensure_ready();
    try {
        auto& db = Database::getInstance().invitesDB();
        db << "UPDATE invitation_codes SET revoked_at = ?, revoked_by_account_id = ? "
              "WHERE id = ? AND used_at IS NULL AND revoked_at IS NULL;"
           << revoked_at << staff_account_id << id;
        int changed = 0;
        db << "SELECT changes();" >> changed;
        if (changed == 0) {
            const auto existing = find_by_id(id);
            if (!existing) {
                error = "invitation_code_not_found";
            } else if (existing->used_at > 0) {
                error = "invitation_code_used";
            } else {
                error = "invitation_code_already_revoked";
            }
            return false;
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::vector<invitation_code_record> InvitationStore::list_for_account(
    std::int64_t account_id) {
    ensure_ready();
    std::vector<invitation_code_record> out;
    const std::string sql = std::string("SELECT ") + kSelectColumns +
                            " FROM invitation_codes WHERE created_by_account_id = ? "
                            "ORDER BY created_at DESC, id DESC;";
    auto& db = Database::getInstance().invitesDB();
    db << sql << account_id
       >> [&](std::string id, std::string code, std::int64_t created_by_account_id,
              std::string created_by_kind, std::int64_t created_at, std::int64_t used_at,
              std::int64_t used_by_account_id, std::int64_t revoked_at,
              std::int64_t revoked_by_account_id) {
              out.push_back(read_record_row(
                  std::move(id), std::move(code), created_by_account_id,
                  std::move(created_by_kind), created_at, used_at, used_by_account_id,
                  revoked_at, revoked_by_account_id));
          };
    return out;
}

std::vector<invitation_code_record> InvitationStore::list_all(
    const invitation_code_filter& filter) {
    ensure_ready();
    std::vector<invitation_code_record> out;

    const std::string status =
        (filter.status == "unused" || filter.status == "used" ||
         filter.status == "revoked")
            ? filter.status
            : std::string();
    const int limit = std::clamp(filter.limit, 1, 500);
    const int offset = std::max(0, filter.offset);
    const std::string query = filter.query;
    const std::string like = "%" + query + "%";

    const std::string sql = std::string("SELECT ") + kSelectColumns +
                            " FROM invitation_codes "
                            "WHERE (? = '' OR ? = 'all' OR "
                            "       (? = 'unused' AND used_at IS NULL AND revoked_at IS NULL) OR "
                            "       (? = 'used' AND used_at IS NOT NULL) OR "
                            "       (? = 'revoked' AND revoked_at IS NOT NULL)) "
                            "AND (? = '' OR created_by_kind = ?) "
                            "AND (? = '' OR code LIKE ? OR id LIKE ?) "
                            "ORDER BY created_at DESC, id DESC LIMIT ? OFFSET ?;";
    auto& db = Database::getInstance().invitesDB();
    db << sql << status << status << status << status << status
       << filter.created_by_kind << filter.created_by_kind
       << query << like << like << limit << offset
       >> [&](std::string id, std::string code, std::int64_t created_by_account_id,
              std::string created_by_kind, std::int64_t created_at, std::int64_t used_at,
              std::int64_t used_by_account_id, std::int64_t revoked_at,
              std::int64_t revoked_by_account_id) {
              out.push_back(read_record_row(
                  std::move(id), std::move(code), created_by_account_id,
                  std::move(created_by_kind), created_at, used_at, used_by_account_id,
                  revoked_at, revoked_by_account_id));
          };
    return out;
}

void InvitationStore::count_created_for_account(std::int64_t account_id,
                                                std::int64_t since,
                                                std::int64_t& today,
                                                std::int64_t& total) {
    ensure_ready();
    today = 0;
    total = 0;
    auto& db = Database::getInstance().invitesDB();
    db << "SELECT COUNT(*), "
          "COALESCE(SUM(CASE WHEN created_at >= ? THEN 1 ELSE 0 END), 0) "
          "FROM invitation_codes WHERE created_by_account_id = ?;"
       << since << account_id
       >> [&](std::int64_t total_count, std::int64_t today_count) {
              total = total_count;
              today = today_count;
          };
}

std::string canonicalize_invitation_code(const std::string& input) {
    std::string normalized;
    normalized.reserve(input.size());
    for (unsigned char c : input) {
        if (std::isspace(c) || c == '-' || c == '_') continue;
        const char upper = static_cast<char>(std::toupper(c));
        // Crockford-style confusion mapping: O -> 0, I/L -> 1.
        if (upper == 'O') {
            normalized.push_back('0');
        } else if (upper == 'I' || upper == 'L') {
            normalized.push_back('1');
        } else {
            normalized.push_back(upper);
        }
    }
    if (normalized.size() != kCodeLength) return std::string();
    for (char c : normalized) {
        if (std::string(kCrockfordAlphabet).find(c) == std::string::npos) {
            return std::string();
        }
    }
    return normalized;
}

std::string display_invitation_code(const std::string& canonical) {
    if (canonical.size() != kCodeLength) return canonical;
    return canonical.substr(0, 4) + "-" + canonical.substr(4, 4) + "-" +
           canonical.substr(8, 4);
}

std::string generate_invitation_code() {
    unsigned char bytes[kCodeLength];
    random_bytes(bytes, sizeof(bytes));
    std::string code;
    code.reserve(kCodeLength);
    for (unsigned char byte : bytes) {
        // Alphabet length is exactly 32, so & 31 is unbiased.
        code.push_back(kCrockfordAlphabet[byte & 0x1F]);
    }
    return code;
}

std::string generate_invitation_code_id() {
    const std::int64_t now = now_seconds_utc();
    unsigned char bytes[8];
    random_bytes(bytes, sizeof(bytes));
    static const char* hex = "0123456789abcdef";
    std::string id = "inv_" + std::to_string(now) + "_";
    id.reserve(id.size() + 16);
    for (unsigned char byte : bytes) {
        id.push_back(hex[(byte >> 4) & 0x0F]);
        id.push_back(hex[byte & 0x0F]);
    }
    return id;
}
