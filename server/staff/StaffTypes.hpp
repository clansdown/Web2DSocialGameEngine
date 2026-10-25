#pragma once
// Shared data structures for the game-staff system: staff identity, the
// action journal, support tickets, and account-level notifications.
//
// This is a separate authorization domain from scoped chat moderation and the
// legacy chat-admin allowlist (`game/config/chat_admins.json`). See
// docs/staff_authorization.md.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace staff {

using json = nlohmann::json;

// ── Staff identity ─────────────────────────────────────────────────────────

struct staff_context {
    int64_t account_id = 0;
    std::vector<std::string> roles;
    std::set<std::string> capabilities;

    bool is_staff() const { return account_id > 0 && !roles.empty(); }
    bool has(const std::string& capability) const {
        return capabilities.count(capability) > 0;
    }
};

struct staff_role {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> capabilities;
    int64_t created_at = 0;
};

struct staff_account_summary {
    int64_t account_id = 0;
    std::string username;
    std::vector<std::string> roles;
    int64_t created_at = 0;
    int64_t revoked_at = 0;
};

// ── Action journal ─────────────────────────────────────────────────────────

struct staff_resource_ref {
    std::string resource_type;
    std::string resource_id;
    std::string link_role = "effect";
};

struct staff_undo_descriptor {
    std::string action_type;
    int version = 1;
    json parameters = json::object();
    std::vector<staff_resource_ref> resources;
    bool reversible = false;
};

struct staff_action_request {
    std::string action_type;
    int version = 1;
    std::string target_type;
    std::string target_id;
    std::string scope;
    std::string reason;
    json parameters = json::object();
};

struct staff_action_result {
    bool ok = false;
    std::string error;
    std::string action_id;
    json data = json::object();
};

enum class staff_preflight_status {
    available,
    target_missing,
    target_changed,
    not_reversible,
    already_undone,
    blocked,
};

inline const char* preflight_status_name(staff_preflight_status status) {
    switch (status) {
        case staff_preflight_status::available: return "available";
        case staff_preflight_status::target_missing: return "target_missing";
        case staff_preflight_status::target_changed: return "target_changed";
        case staff_preflight_status::not_reversible: return "not_reversible";
        case staff_preflight_status::already_undone: return "already_undone";
        case staff_preflight_status::blocked: return "blocked";
    }
    return "blocked";
}

struct staff_preflight_result {
    staff_preflight_status status = staff_preflight_status::blocked;
    std::string reason;
    std::vector<json> resource_results = json::array();
};

struct staff_action_journal_entry {
    std::string id;
    std::string operation = "apply";  // apply | undo | read
    int64_t actor_account_id = 0;
    int64_t actor_character_id = 0;
    std::string actor_type = "staff";  // staff | chat_member | system | cli
    std::string authority = "staff_capability";  // staff_capability | chat_role | system | cli
    std::string capability;
    std::string action_type;
    int action_version = 1;
    std::string target_type;
    std::string target_id;
    std::string scope;
    std::string reason;
    json before_state = json::object();
    json after_state = json::object();
    staff_undo_descriptor undo;
    std::string status = "pending";
    std::string error;
    std::string parent_action_id;
    std::string undo_of;
    int64_t created_at = 0;
    int64_t updated_at = 0;
    int64_t applied_at = 0;
};

struct staff_journal_filter {
    int limit = 100;
    int offset = 0;
    std::string action_type;
    std::string actor_account_id;
    std::string target_type;
    std::string target_id;
    std::string status;
    std::string operation;
    std::string query;
};

// ── Notifications ──────────────────────────────────────────────────────────

struct staff_notification_spec {
    int64_t account_id = 0;
    int64_t character_id = 0;
    std::string kind;
    std::string text_id;
    json text_args = json::object();
    std::string ticket_id;
    std::string action_id;
    std::string route;
};

struct notification_record {
    std::string id;
    int64_t account_id = 0;
    int64_t character_id = 0;
    std::string kind;
    std::string text_id;
    json text_args = json::object();
    std::string ticket_id;
    std::string action_id;
    std::string route;
    int64_t created_at = 0;
    int64_t read_at = 0;

    bool read() const { return read_at > 0; }
};

// ── Support tickets ────────────────────────────────────────────────────────

struct support_ticket {
    std::string id;
    int64_t account_id = 0;
    int64_t character_id = 0;
    std::string category;
    std::string subject;
    std::string status;
    std::string priority = "normal";
    int64_t assigned_account_id = 0;
    int64_t player_last_read_at = 0;
    int64_t staff_last_read_at = 0;
    int64_t created_at = 0;
    int64_t updated_at = 0;
    int64_t closed_at = 0;
    int unread_for_player = 0;
    int unread_for_staff = 0;
    int message_count = 0;
    std::string last_message_preview;
    int64_t last_message_at = 0;
};

struct support_ticket_message {
    std::string id;
    std::string ticket_id;
    std::string author_kind;  // player | staff | system
    int64_t author_account_id = 0;
    int64_t author_character_id = 0;
    std::string body;
    bool is_internal = false;
    int64_t created_at = 0;
};

struct support_ticket_filter {
    int limit = 100;
    int offset = 0;
    std::string status;
    std::string category;
    std::string query;
};

// ── Avatar hash bans ───────────────────────────────────────────────────────

struct banned_avatar_hash {
    std::string sha256;
    std::string reason;
    int64_t banned_by_account_id = 0;
    int64_t created_at = 0;
    int64_t revoked_at = 0;
    int64_t revoked_by_account_id = 0;
    std::string action_id;
};

}  // namespace staff
