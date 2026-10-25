#pragma once
// invites.db persistence: single-use registration invitation codes.
//
// The database is opened lazily (see Database::invitesDB()). Codes are stored
// in plaintext because the creator/staff need to be able to read them again.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct invitation_code_record {
    std::string id;
    std::string code;  // canonical uppercase, no dashes
    std::int64_t created_by_account_id = 0;
    std::string created_by_kind;  // staff | player
    std::int64_t created_at = 0;
    std::int64_t used_at = 0;
    std::int64_t used_by_account_id = 0;
    std::int64_t revoked_at = 0;
    std::int64_t revoked_by_account_id = 0;
};

struct invitation_code_draft {
    std::string id;
    std::string code;  // canonical uppercase, no dashes
};

struct invitation_code_filter {
    std::string status;  // all | unused | used | revoked
    std::string created_by_kind;
    std::string query;
    int limit = 100;
    int offset = 0;
};

class InvitationStore {
public:
    static InvitationStore& instance();

    void ensure_ready();

    std::optional<invitation_code_record> find_by_code(const std::string& canonical_code);
    std::optional<invitation_code_record> find_by_id(const std::string& id);
    bool is_redeemed_in_game(const std::string& id);

    invitation_code_draft make_code_draft() const;
    bool insert_code(const invitation_code_draft& draft, std::int64_t account_id,
                     const std::string& created_by_kind, std::int64_t created_at,
                     std::string& error);

    std::optional<invitation_code_record> create_code(std::int64_t account_id,
                                                      const std::string& created_by_kind,
                                                      std::string& error);

    bool mark_used(const std::string& id, std::int64_t account_id, std::int64_t used_at,
                   std::string& error);
    bool revoke_code(const std::string& id, std::int64_t staff_account_id,
                     std::int64_t revoked_at, std::string& error);

    std::vector<invitation_code_record> list_for_account(std::int64_t account_id);
    std::vector<invitation_code_record> list_all(const invitation_code_filter& filter);

    // Today = created_at >= since (UTC day start). One aggregate query.
    void count_created_for_account(std::int64_t account_id, std::int64_t since,
                                   std::int64_t& today, std::int64_t& total);

private:
    InvitationStore() = default;
    bool initialized_ = false;
};

std::string canonicalize_invitation_code(const std::string& input);
std::string display_invitation_code(const std::string& canonical);
std::string generate_invitation_code();
std::string generate_invitation_code_id();
