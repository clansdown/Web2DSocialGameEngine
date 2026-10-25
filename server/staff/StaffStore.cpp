#include "staff/StaffStore.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <sstream>

#include "Database.hpp"

namespace staff {

namespace {

int64_t unix_now() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

json parse_json_or_object(const std::string& value) {
    json parsed = json::parse(value, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return json::object();
    return parsed;
}

std::string column_text(const std::unique_ptr<std::string>& value) {
    return value ? *value : std::string();
}

std::string safe_status(const std::string& status) {
    if (status == "open" || status == "awaiting_staff" || status == "awaiting_player" ||
        status == "resolved" || status == "closed") {
        return status;
    }
    return "open";
}

void load_ticket_counters(sqlite::database& db, support_ticket& ticket) {
    ticket.unread_for_player = 0;
    ticket.unread_for_staff = 0;
    ticket.message_count = 0;
    db << "SELECT "
          "(SELECT COUNT(*) FROM support_ticket_messages WHERE ticket_id = ? "
          " AND is_internal = 0 AND author_kind != 'player' AND created_at > ?),"
          "(SELECT COUNT(*) FROM support_ticket_messages WHERE ticket_id = ? "
          " AND author_kind = 'player' AND created_at > ?),"
          "(SELECT COUNT(*) FROM support_ticket_messages WHERE ticket_id = ? AND is_internal = 0),"
          "COALESCE((SELECT body FROM support_ticket_messages WHERE ticket_id = ? "
          " AND is_internal = 0 ORDER BY created_at DESC, id DESC LIMIT 1), ''),"
          "COALESCE((SELECT created_at FROM support_ticket_messages WHERE ticket_id = ? "
          " AND is_internal = 0 ORDER BY created_at DESC, id DESC LIMIT 1), 0);"
       << ticket.id << ticket.player_last_read_at
       << ticket.id << ticket.staff_last_read_at
       << ticket.id
       << ticket.id
       << ticket.id
       >> [&](int player_unread, int staff_unread, int count, std::string preview,
              int64_t last_at) {
              ticket.unread_for_player = player_unread;
              ticket.unread_for_staff = staff_unread;
              ticket.message_count = count;
              ticket.last_message_preview = std::move(preview);
              ticket.last_message_at = last_at;
          };
}

const char* ticket_columns() {
    return "id, account_id, COALESCE(character_id, 0), category, subject, status, priority, "
           "COALESCE(assigned_account_id, 0), player_last_read_at, staff_last_read_at, "
           "created_at, updated_at, COALESCE(closed_at, 0)";
}

}  // namespace

std::string new_id(const std::string& prefix) {
    static std::atomic<uint64_t> counter{0};
    static std::mt19937_64 rng([] {
        std::random_device rd;
        return (static_cast<uint64_t>(rd()) << 32) ^ rd();
    }());
    const uint64_t n = ++counter;
    const uint64_t r = rng();
    std::ostringstream out;
    out << prefix << unix_now() << "_" << n << "_" << std::hex << r;
    return out.str();
}

int64_t now_seconds() {
    return unix_now();
}

StaffStore& StaffStore::instance() {
    static StaffStore store;
    return store;
}

staff_context StaffStore::load_context(int64_t account_id) {
    staff_context context;
    context.account_id = account_id;
    if (account_id <= 0) return context;
    auto& db = Database::getInstance().staffDB();
    bool active = false;
    db << "SELECT 1 FROM staff_accounts WHERE account_id = ? AND revoked_at IS NULL LIMIT 1;"
       << account_id
       >> [&](int) { active = true; };
    if (!active) {
        context.account_id = 0;
        return context;
    }
    db << "SELECT r.id FROM staff_roles r JOIN staff_account_roles ar ON ar.role_id = r.id "
          "WHERE ar.account_id = ? ORDER BY r.id;"
       << account_id
       >> [&](std::string role) { context.roles.push_back(std::move(role)); };
    db << "SELECT DISTINCT rc.capability FROM staff_role_capabilities rc "
          "JOIN staff_account_roles ar ON ar.role_id = rc.role_id "
          "WHERE ar.account_id = ?;"
       << account_id
       >> [&](std::string capability) { context.capabilities.insert(std::move(capability)); };
    if (context.roles.empty()) context.account_id = 0;
    return context;
}

bool StaffStore::is_staff(int64_t account_id) {
    return load_context(account_id).is_staff();
}

bool StaffStore::has_capability(int64_t account_id, const std::string& capability) {
    return load_context(account_id).has(capability);
}

bool StaffStore::role_exists(const std::string& role_id) {
    auto& db = Database::getInstance().staffDB();
    bool exists = false;
    db << "SELECT 1 FROM staff_roles WHERE id = ? LIMIT 1;" << role_id
       >> [&](int) { exists = true; };
    return exists;
}

std::vector<staff_role> StaffStore::list_roles() {
    auto& db = Database::getInstance().staffDB();
    std::vector<staff_role> roles;
    db << "SELECT id, name, description, created_at FROM staff_roles ORDER BY id;"
       >> [&](std::string id, std::string name, std::string description, int64_t created_at) {
              staff_role role;
              role.id = std::move(id);
              role.name = std::move(name);
              role.description = std::move(description);
              role.created_at = created_at;
              roles.push_back(std::move(role));
          };
    for (auto& role : roles) {
        db << "SELECT capability FROM staff_role_capabilities WHERE role_id = ? ORDER BY capability;"
           << role.id
           >> [&](std::string capability) { role.capabilities.push_back(std::move(capability)); };
    }
    return roles;
}

std::vector<staff_account_summary> StaffStore::list_staff() {
    auto& db = Database::getInstance().staffDB();
    auto& game_db = Database::getInstance().gameDB();
    std::vector<staff_account_summary> out;
    db << "SELECT account_id, created_at, COALESCE(revoked_at, 0) FROM staff_accounts "
          "ORDER BY created_at DESC;"
       >> [&](int64_t account_id, int64_t created_at, int64_t revoked_at) {
              staff_account_summary summary;
              summary.account_id = account_id;
              summary.created_at = created_at;
              summary.revoked_at = revoked_at;
              game_db << "SELECT username FROM users WHERE id = ?;" << account_id
                      >> [&](std::string username) { summary.username = std::move(username); };
              db << "SELECT role_id FROM staff_account_roles WHERE account_id = ? ORDER BY role_id;"
                 << account_id
                 >> [&](std::string role) { summary.roles.push_back(std::move(role)); };
              out.push_back(std::move(summary));
          };
    return out;
}

bool StaffStore::grant_staff(int64_t account_id, const std::string& role_id,
                             const std::string& granted_by_label, std::string& error) {
    if (account_id <= 0) {
        error = "account_id required";
        return false;
    }
    if (!role_exists(role_id)) {
        error = "unknown_role";
        return false;
    }
    auto& db = Database::getInstance().staffDB();
    const int64_t now = now_seconds();
    db << "INSERT INTO staff_accounts (account_id, created_at, created_by_label, revoked_at) "
          "VALUES (?, ?, ?, NULL) "
          "ON CONFLICT(account_id) DO UPDATE SET revoked_at = NULL, "
          "created_by_label = excluded.created_by_label;"
       << account_id << now << granted_by_label;
    db << "DELETE FROM staff_account_roles WHERE account_id = ?;" << account_id;
    db << "INSERT OR IGNORE INTO staff_account_roles (account_id, role_id) VALUES (?, ?);"
       << account_id << role_id;
    return true;
}

bool StaffStore::revoke_staff(int64_t account_id, const std::string& revoked_by_label,
                              std::string& error) {
    (void)revoked_by_label;
    auto& db = Database::getInstance().staffDB();
    bool exists = false;
    db << "SELECT 1 FROM staff_accounts WHERE account_id = ? AND revoked_at IS NULL LIMIT 1;"
       << account_id
       >> [&](int) { exists = true; };
    if (!exists) {
        error = "not_staff";
        return false;
    }
    db << "UPDATE staff_accounts SET revoked_at = ? WHERE account_id = ?;"
       << now_seconds() << account_id;
    db << "DELETE FROM staff_account_roles WHERE account_id = ?;" << account_id;
    return true;
}

bool StaffStore::set_chat_moderation_mode(int64_t account_id, bool enabled) {
    auto& db = Database::getInstance().staffDB();
    db << "INSERT INTO staff_preferences (account_id, chat_moderation_mode, updated_at) "
          "VALUES (?, ?, ?) ON CONFLICT(account_id) DO UPDATE SET "
          "chat_moderation_mode = excluded.chat_moderation_mode, "
          "updated_at = excluded.updated_at;"
       << account_id << (enabled ? 1 : 0) << now_seconds();
    return true;
}

bool StaffStore::chat_moderation_mode(int64_t account_id) {
    auto& db = Database::getInstance().staffDB();
    bool enabled = false;
    db << "SELECT chat_moderation_mode FROM staff_preferences WHERE account_id = ?;"
       << account_id
       >> [&](int value) { enabled = value != 0; };
    return enabled;
}

std::string StaffStore::append_action(staff_action_journal_entry& entry, std::string& error) {
    if (entry.action_type.empty()) {
        error = "action_type required";
        return std::string();
    }
    if (entry.id.empty()) entry.id = new_id("act_");
    const int64_t now = now_seconds();
    if (entry.created_at == 0) entry.created_at = now;
    entry.updated_at = now;
    if (entry.status.empty()) entry.status = "pending";
    auto& db = Database::getInstance().staffDB();
    try {
        db << "INSERT INTO staff_action_journal (id, operation, actor_account_id, "
              "actor_character_id, actor_type, authority, capability, action_type, "
              "action_version, target_type, target_id, scope, reason, before_state, after_state, "
              "undo_descriptor, status, error, parent_action_id, undo_of, created_at, updated_at, "
              "applied_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"
           << entry.id << entry.operation
           << (entry.actor_account_id > 0 ? std::optional<int64_t>(entry.actor_account_id)
                                          : std::optional<int64_t>())
           << (entry.actor_character_id > 0 ? std::optional<int64_t>(entry.actor_character_id)
                                            : std::optional<int64_t>())
           << entry.actor_type << entry.authority
           << (entry.capability.empty() ? std::optional<std::string>()
                                        : std::optional<std::string>(entry.capability))
           << entry.action_type << entry.action_version << entry.target_type << entry.target_id
           << (entry.scope.empty() ? std::optional<std::string>()
                                   : std::optional<std::string>(entry.scope))
           << (entry.reason.empty() ? std::optional<std::string>()
                                    : std::optional<std::string>(entry.reason))
           << entry.before_state.dump() << entry.after_state.dump()
           << entry.undo.parameters.dump() << entry.status
           << (entry.error.empty() ? std::optional<std::string>()
                                   : std::optional<std::string>(entry.error))
           << (entry.parent_action_id.empty() ? std::optional<std::string>()
                                              : std::optional<std::string>(entry.parent_action_id))
           << (entry.undo_of.empty() ? std::optional<std::string>()
                                     : std::optional<std::string>(entry.undo_of))
           << entry.created_at << entry.updated_at
           << (entry.applied_at > 0 ? std::optional<int64_t>(entry.applied_at)
                                    : std::optional<int64_t>());
        append_event(entry.id, "created");
        return entry.id;
    } catch (const std::exception& e) {
        error = e.what();
        return std::string();
    }
}

void StaffStore::append_event(const std::string& action_id, const std::string& event_type,
                              const json& detail) {
    try {
        auto& db = Database::getInstance().staffDB();
        db << "INSERT INTO staff_action_events (action_id, event_type, detail, created_at) "
              "VALUES (?, ?, ?, ?);"
           << action_id << event_type << detail.dump() << now_seconds();
    } catch (const std::exception&) {
        // Audit events are best-effort; the journal row remains authoritative.
    }
}

bool StaffStore::update_action_status(const std::string& action_id, const std::string& status,
                                      const std::string& error) {
    try {
        auto& db = Database::getInstance().staffDB();
        db << "UPDATE staff_action_journal SET status = ?, error = ?, updated_at = ?, "
              "applied_at = CASE WHEN ? = 'applied' THEN ? ELSE applied_at END WHERE id = ?;"
           << status
           << (error.empty() ? std::optional<std::string>() : std::optional<std::string>(error))
           << now_seconds() << status << now_seconds() << action_id;
        append_event(action_id, status, error.empty() ? json::object() : json{{"error", error}});
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<staff_action_journal_entry> StaffStore::find_action(const std::string& action_id) {
    auto& db = Database::getInstance().staffDB();
    std::optional<staff_action_journal_entry> out;
    db << "SELECT id, operation, COALESCE(actor_account_id, 0), COALESCE(actor_character_id, 0), "
          "actor_type, authority, COALESCE(capability, ''), action_type, action_version, "
          "target_type, target_id, COALESCE(scope, ''), COALESCE(reason, ''), before_state, "
          "after_state, undo_descriptor, status, COALESCE(error, ''), "
          "COALESCE(parent_action_id, ''), COALESCE(undo_of, ''), created_at, updated_at, "
          "COALESCE(applied_at, 0) FROM staff_action_journal WHERE id = ?;"
       << action_id
       >> [&](std::string id, std::string operation, int64_t actor_account, int64_t actor_character,
              std::string actor_type, std::string authority, std::string capability,
              std::string action_type, int action_version, std::string target_type,
              std::string target_id, std::string scope, std::string reason,
              std::string before_state, std::string after_state, std::string undo_descriptor,
              std::string status, std::string error, std::string parent_action_id,
              std::string undo_of, int64_t created_at, int64_t updated_at, int64_t applied_at) {
              staff_action_journal_entry entry;
              entry.id = std::move(id);
              entry.operation = std::move(operation);
              entry.actor_account_id = actor_account;
              entry.actor_character_id = actor_character;
              entry.actor_type = std::move(actor_type);
              entry.authority = std::move(authority);
              entry.capability = std::move(capability);
              entry.action_type = std::move(action_type);
              entry.action_version = action_version;
              entry.target_type = std::move(target_type);
              entry.target_id = std::move(target_id);
              entry.scope = std::move(scope);
              entry.reason = std::move(reason);
              entry.before_state = parse_json_or_object(before_state);
              entry.after_state = parse_json_or_object(after_state);
              entry.undo.action_type = entry.action_type;
              entry.undo.version = entry.action_version;
              entry.undo.parameters = parse_json_or_object(undo_descriptor);
              entry.undo.reversible = !entry.undo.parameters.empty();
              entry.status = std::move(status);
              entry.error = std::move(error);
              entry.parent_action_id = std::move(parent_action_id);
              entry.undo_of = std::move(undo_of);
              entry.created_at = created_at;
              entry.updated_at = updated_at;
              entry.applied_at = applied_at;
              out = std::move(entry);
          };
    return out;
}

std::vector<staff_action_journal_entry> StaffStore::list_actions(const staff_journal_filter& filter) {
    auto& db = Database::getInstance().staffDB();
    std::vector<staff_action_journal_entry> out;
    const std::string action_type = filter.action_type;
    const std::string actor = filter.actor_account_id;
    const std::string target_type = filter.target_type;
    const std::string target_id = filter.target_id;
    const std::string status = filter.status;
    const std::string operation = filter.operation;
    const std::string like = "%" + filter.query + "%";
    const int limit = std::clamp(filter.limit, 1, 500);
    const int offset = std::max(0, filter.offset);
    db << "SELECT id, operation, COALESCE(actor_account_id, 0), COALESCE(actor_character_id, 0), "
          "actor_type, authority, COALESCE(capability, ''), action_type, action_version, "
          "target_type, target_id, COALESCE(scope, ''), COALESCE(reason, ''), before_state, "
          "after_state, undo_descriptor, status, COALESCE(error, ''), "
          "COALESCE(parent_action_id, ''), COALESCE(undo_of, ''), created_at, updated_at, "
          "COALESCE(applied_at, 0) FROM staff_action_journal "
          "WHERE (? = '' OR action_type = ?) "
          "AND (? = '' OR CAST(COALESCE(actor_account_id, 0) AS TEXT) = ?) "
          "AND (? = '' OR target_type = ?) "
          "AND (? = '' OR target_id = ?) "
          "AND (? = '' OR status = ?) "
          "AND (? = '' OR operation = ?) "
          "AND (? = '' OR action_type LIKE ? OR target_id LIKE ? OR reason LIKE ?) "
          "ORDER BY created_at DESC, id DESC LIMIT ? OFFSET ?;"
       << action_type << action_type
       << actor << actor
       << target_type << target_type
       << target_id << target_id
       << status << status
       << operation << operation
       << filter.query << like << like << like
       << limit << offset
       >> [&](std::string id, std::string operation_value, int64_t actor_account,
              int64_t actor_character, std::string actor_type, std::string authority,
              std::string capability, std::string action_type_value, int action_version,
              std::string target_type_value, std::string target_id_value, std::string scope,
              std::string reason, std::string before_state, std::string after_state,
              std::string undo_descriptor, std::string status_value, std::string error,
              std::string parent_action_id, std::string undo_of, int64_t created_at,
              int64_t updated_at, int64_t applied_at) {
              staff_action_journal_entry entry;
              entry.id = std::move(id);
              entry.operation = std::move(operation_value);
              entry.actor_account_id = actor_account;
              entry.actor_character_id = actor_character;
              entry.actor_type = std::move(actor_type);
              entry.authority = std::move(authority);
              entry.capability = std::move(capability);
              entry.action_type = std::move(action_type_value);
              entry.action_version = action_version;
              entry.target_type = std::move(target_type_value);
              entry.target_id = std::move(target_id_value);
              entry.scope = std::move(scope);
              entry.reason = std::move(reason);
              entry.before_state = parse_json_or_object(before_state);
              entry.after_state = parse_json_or_object(after_state);
              entry.undo.action_type = entry.action_type;
              entry.undo.version = entry.action_version;
              entry.undo.parameters = parse_json_or_object(undo_descriptor);
              entry.undo.reversible = !entry.undo.parameters.empty();
              entry.status = std::move(status_value);
              entry.error = std::move(error);
              entry.parent_action_id = std::move(parent_action_id);
              entry.undo_of = std::move(undo_of);
              entry.created_at = created_at;
              entry.updated_at = updated_at;
              entry.applied_at = applied_at;
              out.push_back(std::move(entry));
          };
    return out;
}

void StaffStore::link_resource(const std::string& action_id, const staff_resource_ref& resource) {
    if (action_id.empty() || resource.resource_type.empty() || resource.resource_id.empty()) return;
    try {
        auto& db = Database::getInstance().staffDB();
        db << "INSERT OR IGNORE INTO staff_action_links "
              "(action_id, resource_type, resource_id, link_role, created_at) "
              "VALUES (?, ?, ?, ?, ?);"
           << action_id << resource.resource_type << resource.resource_id << resource.link_role
           << now_seconds();
    } catch (const std::exception&) {
    }
}

std::vector<std::string> StaffStore::action_ids_for_ticket(const std::string& ticket_id) {
    auto& db = Database::getInstance().staffDB();
    std::vector<std::string> out;
    db << "SELECT action_id FROM support_ticket_action_links WHERE ticket_id = ? "
          "ORDER BY created_at DESC;"
       << ticket_id
       >> [&](std::string action_id) { out.push_back(std::move(action_id)); };
    return out;
}

void StaffStore::recover_interrupted_actions() {
    auto& db = Database::getInstance().staffDB();
    std::vector<std::string> ids;
    db << "SELECT id FROM staff_action_journal WHERE status = 'pending';"
       >> [&](std::string id) { ids.push_back(std::move(id)); };
    for (const auto& id : ids) {
        db << "UPDATE staff_action_journal SET status = 'partial', "
              "error = 'interrupted; verification required', updated_at = ? WHERE id = ?;"
           << now_seconds() << id;
        append_event(id, "partial", json{{"error", "interrupted; verification required"}});
    }
}

// ── Notifications ──────────────────────────────────────────────────────────

std::string StaffStore::create_notification(staff_notification_spec spec) {
    if (spec.account_id <= 0 || spec.text_id.empty()) return std::string();
    auto& db = Database::getInstance().staffDB();
    const std::string id = new_id("ntf_");
    const int64_t now = now_seconds();
    db << "INSERT INTO notifications (id, account_id, character_id, kind, text_id, text_args, "
          "ticket_id, action_id, route, created_at, read_at) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL);"
       << id << spec.account_id
       << (spec.character_id > 0 ? std::optional<int64_t>(spec.character_id)
                                 : std::optional<int64_t>())
       << spec.kind << spec.text_id << spec.text_args.dump()
       << (spec.ticket_id.empty() ? std::optional<std::string>()
                                  : std::optional<std::string>(spec.ticket_id))
       << (spec.action_id.empty() ? std::optional<std::string>()
                                  : std::optional<std::string>(spec.action_id))
       << (spec.route.empty() ? std::optional<std::string>()
                              : std::optional<std::string>(spec.route))
       << now;
    return id;
}

std::vector<notification_record> StaffStore::list_notifications(int64_t account_id, int limit,
                                                               int offset) {
    auto& db = Database::getInstance().staffDB();
    std::vector<notification_record> out;
    limit = std::clamp(limit, 1, 200);
    offset = std::max(0, offset);
    db << "SELECT id, account_id, COALESCE(character_id, 0), kind, text_id, text_args, "
          "COALESCE(ticket_id, ''), COALESCE(action_id, ''), COALESCE(route, ''), created_at, "
          "COALESCE(read_at, 0) FROM notifications WHERE account_id = ? "
          "ORDER BY created_at DESC, id DESC LIMIT ? OFFSET ?;"
       << account_id << limit << offset
       >> [&](std::string id, int64_t account, int64_t character, std::string kind,
              std::string text_id, std::string text_args, std::string ticket_id,
              std::string action_id, std::string route, int64_t created_at, int64_t read_at) {
              notification_record record;
              record.id = std::move(id);
              record.account_id = account;
              record.character_id = character;
              record.kind = std::move(kind);
              record.text_id = std::move(text_id);
              record.text_args = parse_json_or_object(text_args);
              record.ticket_id = std::move(ticket_id);
              record.action_id = std::move(action_id);
              record.route = std::move(route);
              record.created_at = created_at;
              record.read_at = read_at;
              out.push_back(std::move(record));
          };
    return out;
}

int64_t StaffStore::unread_notifications(int64_t account_id) {
    auto& db = Database::getInstance().staffDB();
    int64_t count = 0;
    db << "SELECT COUNT(*) FROM notifications WHERE account_id = ? AND read_at IS NULL;"
       << account_id
       >> [&](int64_t value) { count = value; };
    return count;
}

bool StaffStore::mark_notification_read(int64_t account_id, const std::string& notification_id) {
    auto& db = Database::getInstance().staffDB();
    db << "UPDATE notifications SET read_at = ? WHERE id = ? AND account_id = ? AND read_at IS NULL;"
       << now_seconds() << notification_id << account_id;
    return true;
}

void StaffStore::mark_all_notifications_read(int64_t account_id) {
    auto& db = Database::getInstance().staffDB();
    db << "UPDATE notifications SET read_at = ? WHERE account_id = ? AND read_at IS NULL;"
       << now_seconds() << account_id;
}

// ── Support tickets ────────────────────────────────────────────────────────

std::string StaffStore::create_ticket(support_ticket& ticket, std::string& error) {
    if (ticket.account_id <= 0) {
        error = "account_id required";
        return std::string();
    }
    if (ticket.subject.empty()) {
        error = "subject_required";
        return std::string();
    }
    if (ticket.category.empty()) ticket.category = "other";
    if (ticket.status.empty()) ticket.status = "open";
    if (ticket.priority.empty()) ticket.priority = "normal";
    if (ticket.id.empty()) ticket.id = new_id("tkt_");
    const int64_t now = now_seconds();
    ticket.created_at = ticket.created_at > 0 ? ticket.created_at : now;
    ticket.updated_at = now;
    auto& db = Database::getInstance().staffDB();
    try {
        db << "INSERT INTO support_tickets (id, account_id, character_id, category, subject, "
              "status, priority, assigned_account_id, player_last_read_at, staff_last_read_at, "
              "created_at, updated_at, closed_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"
           << ticket.id << ticket.account_id
           << (ticket.character_id > 0 ? std::optional<int64_t>(ticket.character_id)
                                       : std::optional<int64_t>())
           << ticket.category << ticket.subject << safe_status(ticket.status) << ticket.priority
           << (ticket.assigned_account_id > 0
                   ? std::optional<int64_t>(ticket.assigned_account_id)
                   : std::optional<int64_t>())
           << ticket.player_last_read_at << ticket.staff_last_read_at << ticket.created_at
           << ticket.updated_at
           << (ticket.closed_at > 0 ? std::optional<int64_t>(ticket.closed_at)
                                    : std::optional<int64_t>());
        return ticket.id;
    } catch (const std::exception& e) {
        error = e.what();
        return std::string();
    }
}

bool StaffStore::ticket_exists(const std::string& ticket_id) {
    auto& db = Database::getInstance().staffDB();
    bool exists = false;
    db << "SELECT 1 FROM support_tickets WHERE id = ? LIMIT 1;" << ticket_id
       >> [&](int) { exists = true; };
    return exists;
}

std::optional<support_ticket> StaffStore::get_ticket(const std::string& ticket_id) {
    auto& db = Database::getInstance().staffDB();
    std::optional<support_ticket> out;
    db << "SELECT " + std::string(ticket_columns()) +
              " FROM support_tickets WHERE id = ?;"
       << ticket_id
       >> [&](std::string id, int64_t account_id, int64_t character_id, std::string category,
              std::string subject, std::string status, std::string priority, int64_t assigned,
              int64_t player_read, int64_t staff_read, int64_t created_at, int64_t updated_at,
              int64_t closed_at) {
              support_ticket ticket;
              ticket.id = std::move(id);
              ticket.account_id = account_id;
              ticket.character_id = character_id;
              ticket.category = std::move(category);
              ticket.subject = std::move(subject);
              ticket.status = std::move(status);
              ticket.priority = std::move(priority);
              ticket.assigned_account_id = assigned;
              ticket.player_last_read_at = player_read;
              ticket.staff_last_read_at = staff_read;
              ticket.created_at = created_at;
              ticket.updated_at = updated_at;
              ticket.closed_at = closed_at;
              load_ticket_counters(db, ticket);
              out = std::move(ticket);
          };
    return out;
}

std::vector<support_ticket> StaffStore::list_tickets_for_account(
    int64_t account_id, const support_ticket_filter& filter) {
    auto& db = Database::getInstance().staffDB();
    std::vector<support_ticket> out;
    const int limit = std::clamp(filter.limit, 1, 200);
    const int offset = std::max(0, filter.offset);
    const std::string like = "%" + filter.query + "%";
    db << "SELECT " + std::string(ticket_columns()) +
              " FROM support_tickets WHERE account_id = ? "
              "AND (? = '' OR status = ?) AND (? = '' OR category = ?) "
              "AND (? = '' OR subject LIKE ?) ORDER BY updated_at DESC, id DESC LIMIT ? OFFSET ?;"
       << account_id << filter.status << filter.status << filter.category << filter.category
       << filter.query << like << limit << offset
       >> [&](std::string id, int64_t account, int64_t character, std::string category,
              std::string subject, std::string status, std::string priority, int64_t assigned,
              int64_t player_read, int64_t staff_read, int64_t created_at, int64_t updated_at,
              int64_t closed_at) {
              support_ticket ticket;
              ticket.id = std::move(id);
              ticket.account_id = account;
              ticket.character_id = character;
              ticket.category = std::move(category);
              ticket.subject = std::move(subject);
              ticket.status = std::move(status);
              ticket.priority = std::move(priority);
              ticket.assigned_account_id = assigned;
              ticket.player_last_read_at = player_read;
              ticket.staff_last_read_at = staff_read;
              ticket.created_at = created_at;
              ticket.updated_at = updated_at;
              ticket.closed_at = closed_at;
              load_ticket_counters(db, ticket);
              out.push_back(std::move(ticket));
          };
    return out;
}

std::vector<support_ticket> StaffStore::list_staff_tickets(const support_ticket_filter& filter) {
    auto& db = Database::getInstance().staffDB();
    std::vector<support_ticket> out;
    const int limit = std::clamp(filter.limit, 1, 200);
    const int offset = std::max(0, filter.offset);
    const std::string like = "%" + filter.query + "%";
    db << "SELECT " + std::string(ticket_columns()) +
              " FROM support_tickets WHERE "
              "(? = '' OR status = ?) AND (? = '' OR category = ?) "
              "AND (? = '' OR subject LIKE ?) ORDER BY "
              "CASE status WHEN 'open' THEN 0 WHEN 'awaiting_staff' THEN 1 "
              "WHEN 'awaiting_player' THEN 2 WHEN 'resolved' THEN 3 ELSE 4 END, "
              "updated_at DESC, id DESC LIMIT ? OFFSET ?;"
       << filter.status << filter.status << filter.category << filter.category
       << filter.query << like << limit << offset
       >> [&](std::string id, int64_t account, int64_t character, std::string category,
              std::string subject, std::string status, std::string priority, int64_t assigned,
              int64_t player_read, int64_t staff_read, int64_t created_at, int64_t updated_at,
              int64_t closed_at) {
              support_ticket ticket;
              ticket.id = std::move(id);
              ticket.account_id = account;
              ticket.character_id = character;
              ticket.category = std::move(category);
              ticket.subject = std::move(subject);
              ticket.status = std::move(status);
              ticket.priority = std::move(priority);
              ticket.assigned_account_id = assigned;
              ticket.player_last_read_at = player_read;
              ticket.staff_last_read_at = staff_read;
              ticket.created_at = created_at;
              ticket.updated_at = updated_at;
              ticket.closed_at = closed_at;
              load_ticket_counters(db, ticket);
              out.push_back(std::move(ticket));
          };
    return out;
}

std::string StaffStore::append_ticket_message(support_ticket_message& message, std::string& error) {
    if (message.ticket_id.empty()) {
        error = "ticket_id required";
        return std::string();
    }
    if (message.body.empty()) {
        error = "body_required";
        return std::string();
    }
    if (message.author_kind.empty()) message.author_kind = "player";
    if (message.id.empty()) message.id = new_id("msg_");
    const int64_t now = now_seconds();
    message.created_at = message.created_at > 0 ? message.created_at : now;
    auto& db = Database::getInstance().staffDB();
    try {
        db << "INSERT INTO support_ticket_messages (id, ticket_id, author_kind, "
              "author_account_id, author_character_id, body, is_internal, created_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?);"
           << message.id << message.ticket_id << message.author_kind
           << (message.author_account_id > 0
                   ? std::optional<int64_t>(message.author_account_id)
                   : std::optional<int64_t>())
           << (message.author_character_id > 0
                   ? std::optional<int64_t>(message.author_character_id)
                   : std::optional<int64_t>())
           << message.body << (message.is_internal ? 1 : 0) << message.created_at;
        std::string next_status;
        if (message.author_kind == "player") next_status = "awaiting_staff";
        else if (message.author_kind == "staff" && !message.is_internal) next_status = "awaiting_player";
        if (!next_status.empty()) {
            db << "UPDATE support_tickets SET status = ?, updated_at = ? "
                  "WHERE id = ? AND status != 'closed';"
               << next_status << now << message.ticket_id;
        } else {
            db << "UPDATE support_tickets SET updated_at = ? WHERE id = ?;"
               << now << message.ticket_id;
        }
        return message.id;
    } catch (const std::exception& e) {
        error = e.what();
        return std::string();
    }
}

std::vector<support_ticket_message> StaffStore::list_ticket_messages(
    const std::string& ticket_id, bool include_internal) {
    auto& db = Database::getInstance().staffDB();
    std::vector<support_ticket_message> out;
    db << "SELECT id, ticket_id, author_kind, COALESCE(author_account_id, 0), "
          "COALESCE(author_character_id, 0), body, is_internal, created_at "
          "FROM support_ticket_messages WHERE ticket_id = ? "
          "AND (? = 1 OR is_internal = 0) ORDER BY created_at, id;"
       << ticket_id << (include_internal ? 1 : 0)
       >> [&](std::string id, std::string ticket, std::string author_kind, int64_t account,
              int64_t character, std::string body, int internal, int64_t created_at) {
              support_ticket_message message;
              message.id = std::move(id);
              message.ticket_id = std::move(ticket);
              message.author_kind = std::move(author_kind);
              message.author_account_id = account;
              message.author_character_id = character;
              message.body = std::move(body);
              message.is_internal = internal != 0;
              message.created_at = created_at;
              out.push_back(std::move(message));
          };
    return out;
}

bool StaffStore::set_ticket_status(const std::string& ticket_id, const std::string& status,
                                   std::string& error) {
    if (status != "open" && status != "awaiting_staff" && status != "awaiting_player" &&
        status != "resolved" && status != "closed") {
        error = "invalid_status";
        return false;
    }
    auto& db = Database::getInstance().staffDB();
    db << "UPDATE support_tickets SET status = ?, updated_at = ?, "
          "closed_at = CASE WHEN ? = 'closed' THEN ? ELSE NULL END WHERE id = ?;"
       << status << now_seconds() << status << now_seconds() << ticket_id;
    return true;
}

bool StaffStore::assign_ticket(const std::string& ticket_id, int64_t staff_account_id,
                               std::string& error) {
    (void)error;
    auto& db = Database::getInstance().staffDB();
    db << "UPDATE support_tickets SET assigned_account_id = ?, updated_at = ? WHERE id = ?;"
       << (staff_account_id > 0 ? std::optional<int64_t>(staff_account_id)
                                : std::optional<int64_t>())
       << now_seconds() << ticket_id;
    return true;
}

bool StaffStore::link_ticket_action(const std::string& ticket_id, const std::string& action_id,
                                    int64_t staff_account_id) {
    try {
        auto& db = Database::getInstance().staffDB();
        db << "INSERT OR IGNORE INTO support_ticket_action_links "
              "(ticket_id, action_id, created_by_account_id, created_at) VALUES (?, ?, ?, ?);"
           << ticket_id << action_id << staff_account_id << now_seconds();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void StaffStore::mark_ticket_player_read(const std::string& ticket_id) {
    auto& db = Database::getInstance().staffDB();
    db << "UPDATE support_tickets SET player_last_read_at = ? WHERE id = ?;"
       << now_seconds() << ticket_id;
}

void StaffStore::mark_ticket_staff_read(const std::string& ticket_id) {
    auto& db = Database::getInstance().staffDB();
    db << "UPDATE support_tickets SET staff_last_read_at = ? WHERE id = ?;"
       << now_seconds() << ticket_id;
}

int StaffStore::unread_tickets_for_player(int64_t account_id) {
    auto& db = Database::getInstance().staffDB();
    int count = 0;
    db << "SELECT COUNT(*) FROM support_tickets t WHERE t.account_id = ? AND EXISTS ("
          "SELECT 1 FROM support_ticket_messages m WHERE m.ticket_id = t.id "
          "AND m.is_internal = 0 AND m.author_kind != 'player' "
          "AND m.created_at > t.player_last_read_at);"
       << account_id
       >> [&](int value) { count = value; };
    return count;
}

int StaffStore::unread_tickets_for_staff(int64_t account_id) {
    (void)account_id;
    auto& db = Database::getInstance().staffDB();
    int count = 0;
    db << "SELECT COUNT(*) FROM support_tickets t WHERE EXISTS ("
          "SELECT 1 FROM support_ticket_messages m WHERE m.ticket_id = t.id "
          "AND m.author_kind = 'player' AND m.created_at > t.staff_last_read_at);"
       >> [&](int value) { count = value; };
    return count;
}

// ── Avatar hash bans ───────────────────────────────────────────────────────

bool StaffStore::is_hash_banned(const std::string& sha256) {
    if (sha256.empty()) return false;
    auto& db = Database::getInstance().staffDB();
    bool banned = false;
    db << "SELECT 1 FROM banned_avatar_hashes WHERE sha256 = ? AND revoked_at IS NULL LIMIT 1;"
       << sha256
       >> [&](int) { banned = true; };
    return banned;
}

bool StaffStore::ban_hash(const std::string& sha256, const std::string& reason,
                          int64_t staff_account_id, const std::string& action_id,
                          std::string& error) {
    if (sha256.size() != 64) {
        error = "invalid_sha256";
        return false;
    }
    auto& db = Database::getInstance().staffDB();
    const int64_t now = now_seconds();
    db << "INSERT INTO banned_avatar_hashes (sha256, reason, banned_by_account_id, created_at, "
          "revoked_at, revoked_by_account_id, action_id) VALUES (?, ?, ?, ?, NULL, NULL, ?) "
          "ON CONFLICT(sha256) DO UPDATE SET reason = excluded.reason, "
          "banned_by_account_id = excluded.banned_by_account_id, created_at = excluded.created_at, "
          "revoked_at = NULL, revoked_by_account_id = NULL, action_id = excluded.action_id;"
       << sha256 << reason << staff_account_id << now << action_id;
    return true;
}

bool StaffStore::unban_hash(const std::string& sha256, int64_t staff_account_id,
                            const std::string& action_id, std::string& error) {
    (void)action_id;
    auto& db = Database::getInstance().staffDB();
    bool active = false;
    db << "SELECT 1 FROM banned_avatar_hashes WHERE sha256 = ? AND revoked_at IS NULL LIMIT 1;"
       << sha256
       >> [&](int) { active = true; };
    if (!active) {
        error = "hash_not_banned";
        return false;
    }
    db << "UPDATE banned_avatar_hashes SET revoked_at = ?, revoked_by_account_id = ? "
          "WHERE sha256 = ?;"
       << now_seconds() << staff_account_id << sha256;
    return true;
}

std::vector<banned_avatar_hash> StaffStore::list_hash_bans(bool include_revoked) {
    auto& db = Database::getInstance().staffDB();
    std::vector<banned_avatar_hash> out;
    db << "SELECT sha256, COALESCE(reason, ''), banned_by_account_id, created_at, "
          "COALESCE(revoked_at, 0), COALESCE(revoked_by_account_id, 0), COALESCE(action_id, '') "
          "FROM banned_avatar_hashes WHERE (? = 1 OR revoked_at IS NULL) "
          "ORDER BY created_at DESC;"
       << (include_revoked ? 1 : 0)
       >> [&](std::string sha, std::string reason, int64_t banned_by, int64_t created_at,
              int64_t revoked_at, int64_t revoked_by, std::string action_id) {
              banned_avatar_hash item;
              item.sha256 = std::move(sha);
              item.reason = std::move(reason);
              item.banned_by_account_id = banned_by;
              item.created_at = created_at;
              item.revoked_at = revoked_at;
              item.revoked_by_account_id = revoked_by;
              item.action_id = std::move(action_id);
              out.push_back(std::move(item));
          };
    return out;
}

}  // namespace staff
