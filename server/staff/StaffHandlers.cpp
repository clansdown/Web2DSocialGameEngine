#include "staff/StaffHandlers.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "Database.hpp"
#include "Visibility.hpp"
#include "staff/StaffActionEngine.hpp"
#include "staff/StaffAuth.hpp"
#include "staff/StaffStore.hpp"

namespace {

using json = nlohmann::json;
using staff::StaffStore;
using staff::staff_context;

ApiResponse with_token(ApiResponse response, const std::optional<std::string>& new_token) {
    if (new_token) response.data["token"] = *new_token;
    return response;
}

staff_context resolve_staff(const std::optional<std::string>& username, ApiResponse& response) {
    if (!username) {
        response.needs_auth = true;
        return staff_context{};
    }
    return staff::context_for_username(username);
}

int64_t resolve_account_id(const json& body) {
    if (body.contains("account_id") && body["account_id"].is_number_integer()) {
        return body["account_id"].get<int64_t>();
    }
    if (body.contains("username") && body["username"].is_string()) {
        const auto id = staff::account_id_for_username(body["username"].get<std::string>());
        return id.value_or(0);
    }
    return 0;
}

json ticket_to_json(const staff::support_ticket& ticket, bool include_account) {
    json out = {
        {"id", ticket.id},
        {"account_id", ticket.account_id},
        {"character_id", ticket.character_id},
        {"category", ticket.category},
        {"subject", ticket.subject},
        {"status", ticket.status},
        {"priority", ticket.priority},
        {"assigned_account_id", ticket.assigned_account_id},
        {"created_at", ticket.created_at},
        {"updated_at", ticket.updated_at},
        {"closed_at", ticket.closed_at},
        {"unread_for_player", ticket.unread_for_player},
        {"unread_for_staff", ticket.unread_for_staff},
        {"message_count", ticket.message_count},
        {"last_message_preview", ticket.last_message_preview},
        {"last_message_at", ticket.last_message_at},
    };
    if (include_account) {
        const auto username = staff::username_for_account(ticket.account_id);
        out["username"] = username.value_or("");
        if (ticket.assigned_account_id > 0) {
            out["assigned_username"] =
                staff::username_for_account(ticket.assigned_account_id).value_or("");
        }
        if (ticket.character_id > 0) {
            auto& db = Database::getInstance().gameDB();
            db << "SELECT safe_display_name FROM characters WHERE id = ?;" << ticket.character_id
               >> [&](std::string name) { out["character_name"] = std::move(name); };
        }
    }
    return out;
}

json message_to_json(const staff::support_ticket_message& message, bool staff_view) {
    json out = {
        {"id", message.id},
        {"ticket_id", message.ticket_id},
        {"author_kind", message.author_kind},
        {"author_account_id", message.author_account_id},
        {"author_character_id", message.author_character_id},
        {"body", message.body},
        {"is_internal", message.is_internal},
        {"created_at", message.created_at},
    };
    if (staff_view) {
        out["author_username"] = staff::username_for_account(message.author_account_id)
                                     .value_or(std::string());
        if (message.author_character_id > 0) {
            auto& db = Database::getInstance().gameDB();
            db << "SELECT safe_display_name FROM characters WHERE id = ?;"
               << message.author_character_id
               >> [&](std::string name) { out["author_character_name"] = std::move(name); };
        }
    }
    return out;
}

json notification_to_json(const staff::notification_record& record) {
    return {
        {"id", record.id},
        {"account_id", record.account_id},
        {"character_id", record.character_id},
        {"kind", record.kind},
        {"text_id", record.text_id},
        {"text_args", record.text_args},
        {"ticket_id", record.ticket_id},
        {"action_id", record.action_id},
        {"route", record.route},
        {"created_at", record.created_at},
        {"read_at", record.read_at},
        {"read", record.read()},
    };
}

json journal_to_json(const staff::staff_action_journal_entry& entry) {
    return {
        {"id", entry.id},
        {"operation", entry.operation},
        {"actor_account_id", entry.actor_account_id},
        {"actor_character_id", entry.actor_character_id},
        {"actor_type", entry.actor_type},
        {"authority", entry.authority},
        {"capability", entry.capability},
        {"action_type", entry.action_type},
        {"action_version", entry.action_version},
        {"target_type", entry.target_type},
        {"target_id", entry.target_id},
        {"scope", entry.scope},
        {"reason", entry.reason},
        {"before_state", entry.before_state},
        {"after_state", entry.after_state},
        {"undo_descriptor", entry.undo.parameters},
        {"reversible", entry.undo.reversible},
        {"status", entry.status},
        {"error", entry.error},
        {"parent_action_id", entry.parent_action_id},
        {"undo_of", entry.undo_of},
        {"created_at", entry.created_at},
        {"updated_at", entry.updated_at},
        {"applied_at", entry.applied_at},
        {"preflight", staff::StaffActionEngine::instance().preflight_undo(entry.id).status ==
                              staff::staff_preflight_status::available
                          ? "available"
                          : "blocked"},
    };
}

json character_row(sqlite::database& db, int64_t character_id, bool include_identity) {
    json out = json::object();
    db << "SELECT c.id, c.display_name, c.safe_display_name, c.level, c.user_id, "
          "COALESCE(c.sex, ''), COALESCE(c.avatar_safe_id, ''), "
          "COALESCE(c.avatar_custom_id, ''), COALESCE(c.avatar_storage, 'local'), "
          "c.avatar_changed_at, c.name_locked, c.avatar_locked "
          "FROM characters c WHERE c.id = ?;"
       << character_id
       >> [&](int64_t id, std::string display_name, std::string safe_name, int level,
              int64_t account_id, std::string sex, std::string avatar_safe, std::string avatar_custom,
              std::string storage, int64_t avatar_changed_at, int name_locked, int avatar_locked) {
              out = {{"id", id},
                     {"account_id", account_id},
                     {"display_name", display_name},
                     {"safe_display_name", safe_name},
                     {"level", level},
                     {"sex", sex},
                     {"name_locked", name_locked != 0},
                     {"avatar_locked", avatar_locked != 0},
                     {"avatar_changed_at", avatar_changed_at},
                     {"has_custom_avatar", !avatar_custom.empty()}};
              if (include_identity) {
                  out["avatar_safe_id"] = avatar_safe;
                  out["avatar_custom_id"] = avatar_custom;
                  out["avatar_storage"] = storage;
              }
          };
    return out;
}

json account_row(int64_t account_id, bool include_characters) {
    auto& db = Database::getInstance().gameDB();
    json out = json::object();
    bool found = false;
    db << "SELECT id, username, adult, social_restricted, support_locked, created_at "
          "FROM users WHERE id = ?;"
       << account_id
       >> [&](int64_t id, std::string username, int adult, int social, int support,
              int64_t created_at) {
              found = true;
              out = {{"account_id", id},
                     {"username", username},
                     {"adult", adult != 0},
                     {"social_restricted", social != 0},
                     {"support_locked", support != 0},
                     {"created_at", created_at}};
          };
    if (!found) return json::object();
    if (include_characters) {
        json characters = json::array();
        db << "SELECT id FROM characters WHERE user_id = ? ORDER BY id;" << account_id
           >> [&](int64_t character_id) {
                  characters.push_back(character_row(db, character_id, true));
              };
        out["characters"] = std::move(characters);
    }
    return out;
}

json optional_json_string(const std::string& value) {
    return value.empty() ? json(nullptr) : json(value);
}

void journal_support_action(const staff_context& context, const std::string& action_type,
                            const std::string& ticket_id, const json& after,
                            bool reversible = false) {
    staff::staff_action_journal_entry entry;
    entry.actor_account_id = context.account_id;
    entry.actor_type = "staff";
    entry.authority = "staff_capability";
    entry.capability = action_type.find("support.") == 0 ? "support.ticket.manage" : "";
    entry.action_type = action_type;
    entry.target_type = "support_ticket";
    entry.target_id = ticket_id;
    entry.after_state = after;
    entry.status = "applied";
    entry.applied_at = staff::now_seconds();
    entry.undo.reversible = reversible;
    std::string error;
    const std::string id = StaffStore::instance().append_action(entry, error);
    if (!id.empty()) {
        StaffStore::instance().append_event(id, "applied");
        StaffStore::instance().link_resource(id, {"support_ticket", ticket_id, "primary"});
        StaffStore::instance().link_ticket_action(ticket_id, id, context.account_id);
    }
}

ApiResponse staff_action_response(const staff::staff_action_result& result,
                                 const std::optional<std::string>& new_token) {
    ApiResponse response;
    if (!result.ok) {
        response.error = result.error;
        return response;
    }
    response.data = result.data;
    response.data["action_id"] = result.action_id;
    return with_token(response, new_token);
}

}  // namespace

// ── Player support + notifications ──────────────────────────────────────────

ApiResponse handleGetSupportContext(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)body;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const auto account_id = staff::account_id_for_username(*username);
    if (!account_id) {
        response.needs_auth = true;
        return response;
    }
    response.data["support_locked"] = staff::account_is_support_locked(*account_id);
    response.data["unread_tickets"] = StaffStore::instance().unread_tickets_for_player(*account_id);
    response.data["unread_notifications"] = StaffStore::instance().unread_notifications(*account_id);
    return with_token(response, new_token);
}

ApiResponse handleListSupportTickets(GameConfigCache& config_cache, const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const auto account_id = staff::account_id_for_username(*username);
    if (!account_id) {
        response.needs_auth = true;
        return response;
    }
    staff::support_ticket_filter filter;
    filter.limit = body.value("limit", 100);
    filter.offset = body.value("offset", 0);
    filter.status = body.value("status", std::string());
    filter.category = body.value("category", std::string());
    filter.query = body.value("query", std::string());
    json tickets = json::array();
    for (const auto& ticket : StaffStore::instance().list_tickets_for_account(*account_id, filter)) {
        tickets.push_back(ticket_to_json(ticket, false));
    }
    response.data["tickets"] = std::move(tickets);
    response.data["support_locked"] = staff::account_is_support_locked(*account_id);
    response.data["unread_notifications"] = StaffStore::instance().unread_notifications(*account_id);
    return with_token(response, new_token);
}

ApiResponse handleGetSupportTicket(GameConfigCache& config_cache, const json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const auto account_id = staff::account_id_for_username(*username);
    if (!account_id) {
        response.needs_auth = true;
        return response;
    }
    const std::string ticket_id = body.value("ticket_id", std::string());
    auto ticket = StaffStore::instance().get_ticket(ticket_id);
    if (!ticket || ticket->account_id != *account_id) {
        response.error = "ticket_not_found";
        return response;
    }
    StaffStore::instance().mark_ticket_player_read(ticket_id);
    ticket = StaffStore::instance().get_ticket(ticket_id);
    json messages = json::array();
    for (const auto& message : StaffStore::instance().list_ticket_messages(ticket_id, false)) {
        messages.push_back(message_to_json(message, false));
    }
    response.data["ticket"] = ticket_to_json(*ticket, false);
    response.data["messages"] = std::move(messages);
    response.data["support_locked"] = staff::account_is_support_locked(*account_id);
    return with_token(response, new_token);
}

ApiResponse handleCreateSupportTicket(GameConfigCache& config_cache, const json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const auto account_id = staff::account_id_for_username(*username);
    if (!account_id) {
        response.needs_auth = true;
        return response;
    }
    if (staff::account_is_support_locked(*account_id)) {
        response.error = "support_locked";
        return response;
    }
    staff::support_ticket ticket;
    ticket.account_id = *account_id;
    ticket.character_id = body.value("character_id", static_cast<int64_t>(0));
    if (ticket.character_id > 0) {
        int64_t owner = 0;
        if (!staff::character_account_id(ticket.character_id, owner) || owner != *account_id) {
            response.error = "character_not_found";
            return response;
        }
    }
    ticket.category = body.value("category", std::string("other"));
    ticket.subject = body.value("subject", std::string());
    ticket.status = "open";
    ticket.priority = body.value("priority", std::string("normal"));
    if (ticket.subject.empty()) {
        response.error = "subject_required";
        return response;
    }
    if (ticket.subject.size() > 160) {
        response.error = "subject_too_long";
        return response;
    }
    std::string error;
    const std::string ticket_id = StaffStore::instance().create_ticket(ticket, error);
    if (ticket_id.empty()) {
        response.error = error.empty() ? "ticket_create_failed" : error;
        return response;
    }
    const std::string body_text = body.value("body", std::string());
    if (body_text.size() > 10000) {
        response.error = "message_too_long";
        return response;
    }
    if (!body_text.empty()) {
        staff::support_ticket_message message;
        message.ticket_id = ticket_id;
        message.author_kind = "player";
        message.author_account_id = *account_id;
        message.author_character_id = ticket.character_id;
        message.body = body_text;
        std::string message_error;
        const std::string message_id = StaffStore::instance().append_ticket_message(message, message_error);
        if (message_id.empty()) {
            response.error = message_error.empty() ? "message_create_failed" : message_error;
            return response;
        }
    }
    response.data["ticket"] = ticket_to_json(*StaffStore::instance().get_ticket(ticket_id), false);
    return with_token(response, new_token);
}

ApiResponse handleReplySupportTicket(GameConfigCache& config_cache, const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const auto account_id = staff::account_id_for_username(*username);
    if (!account_id) {
        response.needs_auth = true;
        return response;
    }
    if (staff::account_is_support_locked(*account_id)) {
        response.error = "support_locked";
        return response;
    }
    const std::string ticket_id = body.value("ticket_id", std::string());
    auto ticket = StaffStore::instance().get_ticket(ticket_id);
    if (!ticket || ticket->account_id != *account_id) {
        response.error = "ticket_not_found";
        return response;
    }
    if (ticket->status == "closed") {
        response.error = "ticket_closed";
        return response;
    }
    const std::string text = body.value("body", std::string());
    if (text.empty()) {
        response.error = "body_required";
        return response;
    }
    if (text.size() > 10000) {
        response.error = "message_too_long";
        return response;
    }
    staff::support_ticket_message message;
    message.ticket_id = ticket_id;
    message.author_kind = "player";
    message.author_account_id = *account_id;
    message.author_character_id = ticket->character_id;
    message.body = text;
    std::string error;
    const std::string message_id = StaffStore::instance().append_ticket_message(message, error);
    if (message_id.empty()) {
        response.error = error.empty() ? "message_create_failed" : error;
        return response;
    }
    if (ticket->status == "resolved") {
        std::string status_error;
        StaffStore::instance().set_ticket_status(ticket_id, "awaiting_staff", status_error);
    }
    response.data["message_id"] = message_id;
    response.data["ticket"] = ticket_to_json(*StaffStore::instance().get_ticket(ticket_id), false);
    return with_token(response, new_token);
}

ApiResponse handleReopenSupportTicket(GameConfigCache& config_cache, const json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const auto account_id = staff::account_id_for_username(*username);
    if (!account_id) {
        response.needs_auth = true;
        return response;
    }
    const std::string ticket_id = body.value("ticket_id", std::string());
    auto ticket = StaffStore::instance().get_ticket(ticket_id);
    if (!ticket || ticket->account_id != *account_id) {
        response.error = "ticket_not_found";
        return response;
    }
    if (ticket->status == "closed") {
        response.error = "ticket_closed";
        return response;
    }
    if (staff::account_is_support_locked(*account_id)) {
        response.error = "support_locked";
        return response;
    }
    std::string error;
    StaffStore::instance().set_ticket_status(ticket_id, "awaiting_staff", error);
    response.data["ticket"] = ticket_to_json(*StaffStore::instance().get_ticket(ticket_id), false);
    return with_token(response, new_token);
}

ApiResponse handleGetNotifications(GameConfigCache& config_cache, const json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const auto account_id = staff::account_id_for_username(*username);
    if (!account_id) {
        response.needs_auth = true;
        return response;
    }
    json notifications = json::array();
    for (const auto& record : StaffStore::instance().list_notifications(
             *account_id, body.value("limit", 50), body.value("offset", 0))) {
        notifications.push_back(notification_to_json(record));
    }
    response.data["notifications"] = std::move(notifications);
    response.data["unread"] = StaffStore::instance().unread_notifications(*account_id);
    response.data["support_locked"] = staff::account_is_support_locked(*account_id);
    return with_token(response, new_token);
}

ApiResponse handleMarkNotificationRead(GameConfigCache& config_cache, const json& body,
                                       const std::optional<std::string>& username,
                                       const ClientInfo& client,
                                       const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const auto account_id = staff::account_id_for_username(*username);
    if (!account_id) {
        response.needs_auth = true;
        return response;
    }
    const std::string id = body.value("notification_id", std::string());
    if (id.empty()) {
        response.error = "notification_id_required";
        return response;
    }
    StaffStore::instance().mark_notification_read(*account_id, id);
    response.data["unread"] = StaffStore::instance().unread_notifications(*account_id);
    return with_token(response, new_token);
}

ApiResponse handleMarkAllNotificationsRead(GameConfigCache& config_cache, const json& body,
                                           const std::optional<std::string>& username,
                                           const ClientInfo& client,
                                           const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)body;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const auto account_id = staff::account_id_for_username(*username);
    if (!account_id) {
        response.needs_auth = true;
        return response;
    }
    StaffStore::instance().mark_all_notifications_read(*account_id);
    response.data["unread"] = 0;
    return with_token(response, new_token);
}

// ── Staff console ──────────────────────────────────────────────────────────

ApiResponse handleStaffGetContext(GameConfigCache& config_cache, const json& body,
                                  const std::optional<std::string>& username,
                                  const ClientInfo& client,
                                  const std::optional<std::string>& new_token) {
    (void)body;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!context.is_staff()) {
        response.error = "not_staff";
        return response;
    }
    response.data["is_staff"] = true;
    response.data["account_id"] = context.account_id;
    response.data["roles"] = context.roles;
    response.data["capabilities"] = json(context.capabilities);
    response.data["unread_notifications"] = StaffStore::instance().unread_notifications(context.account_id);
    response.data["support_unread"] = StaffStore::instance().unread_tickets_for_staff(context.account_id);
    response.data["chat_moderation_mode"] = StaffStore::instance().chat_moderation_mode(context.account_id);
    response.data["roles_catalog"] = json::array();
    for (const auto& role : StaffStore::instance().list_roles()) {
        response.data["roles_catalog"].push_back(
            {{"id", role.id}, {"name", role.name}, {"description", role.description},
             {"capabilities", role.capabilities}});
    }
    return with_token(response, new_token);
}

ApiResponse handleStaffListStaff(GameConfigCache& config_cache, const json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)body;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "staff.audit.read", response)) return response;
    json entries = json::array();
    for (const auto& account : StaffStore::instance().list_staff()) {
        entries.push_back({{"account_id", account.account_id},
                           {"username", account.username},
                           {"roles", account.roles},
                           {"created_at", account.created_at},
                           {"revoked_at", account.revoked_at}});
    }
    response.data["staff"] = std::move(entries);
    return with_token(response, new_token);
}

ApiResponse handleStaffSetChatModerationMode(GameConfigCache& config_cache, const json& body,
                                             const std::optional<std::string>& username,
                                             const ClientInfo& client,
                                             const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "chat.moderate_any", response)) return response;
    const bool enabled = body.value("enabled", false);
    StaffStore::instance().set_chat_moderation_mode(context.account_id, enabled);
    response.data["chat_moderation_mode"] = enabled;
    return with_token(response, new_token);
}

ApiResponse handleStaffSearchAccounts(GameConfigCache& config_cache, const json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "account.view", response)) return response;
    const std::string query = body.value("query", std::string());
    const std::string like = "%" + query + "%";
    auto& db = Database::getInstance().gameDB();
    json accounts = json::array();
    db << "SELECT DISTINCT u.id FROM users u LEFT JOIN characters c ON c.user_id = u.id "
          "WHERE (? = '' OR u.username LIKE ? OR c.display_name LIKE ? OR c.safe_display_name LIKE ?) "
          "ORDER BY u.username LIMIT 50;"
       << query << like << like << like
       >> [&](int64_t account_id) {
              json row = account_row(account_id, true);
              if (!row.empty()) accounts.push_back(std::move(row));
          };
    response.data["accounts"] = std::move(accounts);
    staff::record_read_event(context, "read.account", "account_search", query.substr(0, 64),
                             std::string(), {{"query", query}});
    return with_token(response, new_token);
}

ApiResponse handleStaffGetAccount(GameConfigCache& config_cache, const json& body,
                                  const std::optional<std::string>& username,
                                  const ClientInfo& client,
                                  const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "account.view", response)) return response;
    const int64_t account_id = resolve_account_id(body);
    json account = account_row(account_id, true);
    if (account.empty()) {
        response.error = "account_not_found";
        return response;
    }
    response.data = std::move(account);
    staff::record_read_event(context, "read.account", "account", std::to_string(account_id),
                             std::string(), response.data);
    return with_token(response, new_token);
}

ApiResponse handleStaffSetAdultStatus(GameConfigCache& config_cache, const json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "account.set_adult_status", response)) return response;
    const int64_t account_id = resolve_account_id(body);
    if (account_id <= 0) {
        response.error = "account_id_required";
        return response;
    }
    if (!body.contains("adult") || !body["adult"].is_boolean()) {
        response.error = "adult_boolean_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "staff.set_adult_status";
    request.target_type = "account";
    request.target_id = std::to_string(account_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"adult", body["adult"].get<bool>()},
                          {"method", body.value("method", std::string("customer_service"))}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffSetSocialRestriction(GameConfigCache& config_cache, const json& body,
                                            const std::optional<std::string>& username,
                                            const ClientInfo& client,
                                            const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "account.set_social_restriction", response)) return response;
    const int64_t account_id = resolve_account_id(body);
    if (account_id <= 0 || !body.contains("restricted") || !body["restricted"].is_boolean()) {
        response.error = "account_id_and_restricted_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "staff.set_social_restriction";
    request.target_type = "account";
    request.target_id = std::to_string(account_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"restricted", body["restricted"].get<bool>()}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffSetSupportLock(GameConfigCache& config_cache, const json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "account.set_support_lock", response)) return response;
    const int64_t account_id = resolve_account_id(body);
    if (account_id <= 0 || !body.contains("locked") || !body["locked"].is_boolean()) {
        response.error = "account_id_and_locked_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "staff.set_support_lock";
    request.target_type = "account";
    request.target_id = std::to_string(account_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"locked", body["locked"].get<bool>()}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffGetIdentityHistory(GameConfigCache& config_cache, const json& body,
                                          const std::optional<std::string>& username,
                                          const ClientInfo& client,
                                          const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "identity.view_history", response)) return response;
    const int64_t character_id = body.value("target_character_id",
                                             body.value("character_id", static_cast<int64_t>(0)));
    if (character_id <= 0) {
        response.error = "character_id_required";
        return response;
    }
    auto& db = Database::getInstance().gameDB();
    json history = json::array();
    db << "SELECT id, kind, value, changed_at FROM character_identity_history "
          "WHERE character_id = ? ORDER BY changed_at DESC, id DESC LIMIT 200;"
       << character_id
       >> [&](int64_t id, std::string kind, std::string value, int64_t changed_at) {
              history.push_back({{"id", id}, {"kind", kind}, {"value", value},
                                 {"changed_at", changed_at}});
          };
    response.data["character_id"] = character_id;
    response.data["history"] = std::move(history);
    staff::record_read_event(context, "read.identity_history", "character",
                             std::to_string(character_id), std::string(), response.data);
    return with_token(response, new_token);
}

ApiResponse handleStaffLockIdentity(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "identity.set_name_lock", response)) return response;
    const int64_t character_id = body.value("character_id", static_cast<int64_t>(0));
    if (character_id <= 0) {
        response.error = "character_id_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "staff.lock_identity";
    request.target_type = "character";
    request.target_id = std::to_string(character_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"revert_name", body.value("revert_name", false)},
                          {"revert_avatar", body.value("revert_avatar", false)},
                          {"revert_to", body.value("revert_to", std::string("safe"))}};
    // Only change the lock flags the caller explicitly supplied. This lets the
    // console toggle name and avatar locks independently without clearing the
    // other flag.
    if (body.contains("lock_name")) request.parameters["lock_name"] = body["lock_name"];
    if (body.contains("lock_avatar")) request.parameters["lock_avatar"] = body["lock_avatar"];
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffRevertIdentity(GameConfigCache& config_cache, const json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "identity.revert_name", response)) return response;
    const int64_t character_id = body.value("character_id", static_cast<int64_t>(0));
    const bool name = body.value("revert_name", true);
    const bool avatar = body.value("revert_avatar", false);
    if (character_id <= 0 || (!name && !avatar)) {
        response.error = "character_id_and_revert_target_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "staff.lock_identity";
    request.target_type = "character";
    request.target_id = std::to_string(character_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"revert_name", name}, {"revert_avatar", avatar},
                          {"revert_to", body.value("revert_to", std::string("safe"))}};
    if (body.contains("lock_name")) request.parameters["lock_name"] = body["lock_name"];
    if (body.contains("lock_avatar")) request.parameters["lock_avatar"] = body["lock_avatar"];
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffRemoveCustomAvatar(GameConfigCache& config_cache, const json& body,
                                          const std::optional<std::string>& username,
                                          const ClientInfo& client,
                                          const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "identity.remove_custom_avatar", response)) return response;
    const int64_t character_id = body.value("character_id", static_cast<int64_t>(0));
    if (character_id <= 0) {
        response.error = "character_id_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "staff.remove_custom_avatar";
    request.target_type = "character";
    request.target_id = std::to_string(character_id);
    request.reason = body.value("reason", std::string());
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffBanAvatarHash(GameConfigCache& config_cache, const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "avatar.ban_hash", response)) return response;
    const std::string sha = body.value("sha256", std::string());
    staff::staff_action_request request;
    request.action_type = "staff.ban_avatar_hash";
    request.target_type = "avatar_hash";
    request.target_id = sha;
    request.reason = body.value("reason", std::string());
    request.parameters = {{"sha256", sha}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffUnbanAvatarHash(GameConfigCache& config_cache, const json& body,
                                       const std::optional<std::string>& username,
                                       const ClientInfo& client,
                                       const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "avatar.ban_hash", response)) return response;
    const std::string sha = body.value("sha256", std::string());
    staff::staff_action_request request;
    request.action_type = "staff.unban_avatar_hash";
    request.target_type = "avatar_hash";
    request.target_id = sha;
    request.reason = body.value("reason", std::string());
    request.parameters = {{"sha256", sha}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffListAvatarHashBans(GameConfigCache& config_cache, const json& body,
                                          const std::optional<std::string>& username,
                                          const ClientInfo& client,
                                          const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "avatar.ban_hash", response)) return response;
    json bans = json::array();
    for (const auto& ban : StaffStore::instance().list_hash_bans(body.value("include_revoked", false))) {
        bans.push_back({{"sha256", ban.sha256},
                        {"reason", ban.reason},
                        {"banned_by_account_id", ban.banned_by_account_id},
                        {"created_at", ban.created_at},
                        {"revoked_at", ban.revoked_at},
                        {"action_id", ban.action_id}});
    }
    response.data["bans"] = std::move(bans);
    return with_token(response, new_token);
}

ApiResponse handleStaffSearchBaronies(GameConfigCache& config_cache, const json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "barony.view", response)) return response;
    const std::string query = body.value("query", std::string());
    const std::string like = "%" + query + "%";
    auto& db = Database::getInstance().gameDB();
    json baronies = json::array();
    db << "SELECT b.id, b.name, b.safe_name_id, b.safe_tag_ids, b.description, "
          "b.accepts_minors, b.owner_character_id, COALESCE(b.baron_character_id, 0), "
          "COALESCE(c.safe_display_name, ''), COALESCE(b.defunct_at, 0) "
          "FROM baronies b LEFT JOIN characters c ON c.id = b.owner_character_id "
          "WHERE (? = '' OR b.name LIKE ? OR c.display_name LIKE ? OR c.safe_display_name LIKE ?) "
          "ORDER BY b.id DESC LIMIT 100;"
       << query << like << like << like
       >> [&](int64_t id, std::string name, std::string safe_name, std::string safe_tags,
              std::string description, int accepts_minors, int64_t owner, int64_t baron,
              std::string owner_name, int64_t defunct_at) {
              int member_count = 0;
              db << "SELECT COUNT(*) FROM barony_members WHERE barony_id = ?;" << id
                 >> [&](int count) { member_count = count; };
              baronies.push_back({{"id", id},
                                  {"name", name},
                                  {"safe_name_id", safe_name},
                                  {"safe_tag_ids", json::parse(safe_tags, nullptr, false)},
                                  {"description", description},
                                  {"accepts_minors", accepts_minors != 0},
                                  {"owner_character_id", owner},
                                  {"baron_character_id", baron},
                                  {"owner_name", owner_name},
                                  {"member_count", member_count},
                                  {"defunct_at", defunct_at}});
          };
    response.data["baronies"] = std::move(baronies);
    staff::record_read_event(context, "read.barony", "barony_search", query.substr(0, 64),
                             std::string(), {{"query", query}});
    return with_token(response, new_token);
}

ApiResponse handleStaffSetBaronyIdentity(GameConfigCache& config_cache, const json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token) {
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "barony.set_safe_identity", response)) return response;
    const int64_t barony_id = body.value("barony_id", static_cast<int64_t>(0));
    const std::string safe_name_id = body.value("safe_name_id", std::string());
    if (barony_id <= 0 || safe_name_id.empty() ||
        !visibility::is_valid_safe_id(config_cache, visibility::safe_catalog_kind::barony_name,
                                      safe_name_id)) {
        response.error = "barony_id_and_valid_safe_name_id_required";
        return response;
    }
    std::vector<std::string> tags;
    if (body.contains("safe_tag_ids") && body["safe_tag_ids"].is_array()) {
        for (const auto& tag : body["safe_tag_ids"]) {
            if (!tag.is_string()) continue;
            const std::string id = tag.get<std::string>();
            if (!visibility::is_valid_safe_id(config_cache,
                                              visibility::safe_catalog_kind::barony_tag, id)) {
                response.error = "invalid_safe_barony_tag";
                return response;
            }
            tags.push_back(id);
        }
    }
    staff::staff_action_request request;
    request.action_type = "staff.set_barony_safe_identity";
    request.target_type = "barony";
    request.target_id = std::to_string(barony_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"safe_name_id", safe_name_id},
                          {"safe_tag_ids", tags},
                          {"safe_label", visibility::safe_label(
                                             config_cache,
                                             visibility::safe_catalog_kind::barony_name,
                                             safe_name_id)},
                          {"name", body.value("name", std::string())},
                          {"description", body.value("description", std::string())}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffSetBaronyAcceptsMinors(GameConfigCache& config_cache, const json& body,
                                              const std::optional<std::string>& username,
                                              const ClientInfo& client,
                                              const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "barony.set_accepts_minors", response)) return response;
    const int64_t barony_id = body.value("barony_id", static_cast<int64_t>(0));
    if (barony_id <= 0 || !body.contains("accepts_minors") ||
        !body["accepts_minors"].is_boolean()) {
        response.error = "barony_id_and_accepts_minors_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "staff.set_barony_accepts_minors";
    request.target_type = "barony";
    request.target_id = std::to_string(barony_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"accepts_minors", body["accepts_minors"].get<bool>()}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffReassignBaronyOwner(GameConfigCache& config_cache, const json& body,
                                           const std::optional<std::string>& username,
                                           const ClientInfo& client,
                                           const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "barony.reassign_owner", response)) return response;
    const int64_t barony_id = body.value("barony_id", static_cast<int64_t>(0));
    const int64_t owner = body.value("owner_character_id", static_cast<int64_t>(0));
    if (barony_id <= 0 || owner <= 0) {
        response.error = "barony_id_and_owner_character_id_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "staff.reassign_barony_owner";
    request.target_type = "barony";
    request.target_id = std::to_string(barony_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"owner_character_id", owner}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffListJournal(GameConfigCache& config_cache, const json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "staff.audit.read", response)) return response;
    staff::staff_journal_filter filter;
    filter.limit = body.value("limit", 100);
    filter.offset = body.value("offset", 0);
    filter.action_type = body.value("action_type", std::string());
    filter.actor_account_id = body.value("actor_account_id", std::string());
    filter.target_type = body.value("target_type", std::string());
    filter.target_id = body.value("target_id", std::string());
    filter.status = body.value("status", std::string());
    filter.operation = body.value("operation", std::string());
    filter.query = body.value("query", std::string());
    json entries = json::array();
    for (const auto& entry : StaffStore::instance().list_actions(filter)) {
        entries.push_back(journal_to_json(entry));
    }
    response.data["entries"] = std::move(entries);
    staff::record_read_event(context, "read.staff_journal", "journal", "list",
                             std::string(), {{"filters", body}});
    return with_token(response, new_token);
}

ApiResponse handleStaffGetJournalEntry(GameConfigCache& config_cache, const json& body,
                                       const std::optional<std::string>& username,
                                       const ClientInfo& client,
                                       const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "staff.audit.read", response)) return response;
    const std::string action_id = body.value("action_id", std::string());
    auto entry = StaffStore::instance().find_action(action_id);
    if (!entry) {
        response.error = "action_not_found";
        return response;
    }
    response.data["entry"] = journal_to_json(*entry);
    auto preflight = staff::StaffActionEngine::instance().preflight_undo(action_id);
    response.data["preflight"] = {{"status", staff::preflight_status_name(preflight.status)},
                                  {"reason", preflight.reason},
                                  {"resources", preflight.resource_results}};
    staff::record_read_event(context, "read.staff_journal", "journal", action_id,
                             std::string(), response.data["entry"]);
    return with_token(response, new_token);
}

ApiResponse handleStaffPreflightUndo(GameConfigCache& config_cache, const json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "staff.undo", response)) return response;
    const std::string action_id = body.value("action_id", std::string());
    auto preflight = staff::StaffActionEngine::instance().preflight_undo(action_id);
    response.data["status"] = staff::preflight_status_name(preflight.status);
    response.data["reason"] = preflight.reason;
    response.data["resources"] = preflight.resource_results;
    return with_token(response, new_token);
}

ApiResponse handleStaffUndo(GameConfigCache& config_cache, const json& body,
                            const std::optional<std::string>& username,
                            const ClientInfo& client,
                            const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "staff.undo", response)) return response;
    const std::string action_id = body.value("action_id", std::string());
    return staff_action_response(
        staff::StaffActionEngine::instance().undo(context, action_id,
                                                  body.value("reason", std::string())),
        new_token);
}

ApiResponse handleStaffRecoverPendingAction(GameConfigCache& config_cache, const json& body,
                                            const std::optional<std::string>& username,
                                            const ClientInfo& client,
                                            const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)body;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "staff.audit.read", response)) return response;
    staff::StaffActionEngine::instance().recover_interrupted_actions();
    response.data["recovered"] = true;
    return with_token(response, new_token);
}

ApiResponse handleStaffListSupportQueue(GameConfigCache& config_cache, const json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.ticket.view", response)) return response;
    staff::support_ticket_filter filter;
    filter.limit = body.value("limit", 100);
    filter.offset = body.value("offset", 0);
    filter.status = body.value("status", std::string());
    filter.category = body.value("category", std::string());
    filter.query = body.value("query", std::string());
    json tickets = json::array();
    for (const auto& ticket : StaffStore::instance().list_staff_tickets(filter)) {
        tickets.push_back(ticket_to_json(ticket, true));
    }
    response.data["tickets"] = std::move(tickets);
    response.data["unread"] = StaffStore::instance().unread_tickets_for_staff(context.account_id);
    staff::record_read_event(context, "read.support_ticket", "support_queue", "list",
                             std::string(), {{"filters", body}});
    return with_token(response, new_token);
}

ApiResponse handleStaffGetSupportTicket(GameConfigCache& config_cache, const json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.ticket.view", response)) return response;
    const std::string ticket_id = body.value("ticket_id", std::string());
    auto ticket = StaffStore::instance().get_ticket(ticket_id);
    if (!ticket) {
        response.error = "ticket_not_found";
        return response;
    }
    StaffStore::instance().mark_ticket_staff_read(ticket_id);
    ticket = StaffStore::instance().get_ticket(ticket_id);
    json messages = json::array();
    for (const auto& message : StaffStore::instance().list_ticket_messages(ticket_id, true)) {
        messages.push_back(message_to_json(message, true));
    }
    json actions = json::array();
    for (const auto& action_id : StaffStore::instance().action_ids_for_ticket(ticket_id)) {
        if (auto entry = StaffStore::instance().find_action(action_id)) {
            actions.push_back(journal_to_json(*entry));
        }
    }
    response.data["ticket"] = ticket_to_json(*ticket, true);
    response.data["messages"] = std::move(messages);
    response.data["actions"] = std::move(actions);
    staff::record_read_event(context, "read.support_ticket", "support_ticket", ticket_id,
                             std::string(), response.data["ticket"]);
    return with_token(response, new_token);
}

ApiResponse handleStaffReplySupportTicket(GameConfigCache& config_cache, const json& body,
                                          const std::optional<std::string>& username,
                                          const ClientInfo& client,
                                          const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.ticket.reply", response)) return response;
    const std::string ticket_id = body.value("ticket_id", std::string());
    auto ticket = StaffStore::instance().get_ticket(ticket_id);
    if (!ticket) {
        response.error = "ticket_not_found";
        return response;
    }
    const std::string text = body.value("body", std::string());
    const bool internal = body.value("internal", false);
    if (internal && !context.has("support.ticket.internal_notes")) {
        response.error = "missing_capability:support.ticket.internal_notes";
        return response;
    }
    if (text.empty()) {
        response.error = "body_required";
        return response;
    }
    if (text.size() > 10000) {
        response.error = "message_too_long";
        return response;
    }
    if (ticket->status == "closed") {
        response.error = "ticket_closed";
        return response;
    }
    staff::support_ticket_message message;
    message.ticket_id = ticket_id;
    message.author_kind = "staff";
    message.author_account_id = context.account_id;
    message.author_character_id = 0;
    message.body = text;
    message.is_internal = internal;
    std::string error;
    const std::string message_id = StaffStore::instance().append_ticket_message(message, error);
    if (message_id.empty()) {
        response.error = error.empty() ? "message_create_failed" : error;
        return response;
    }
    journal_support_action(context, internal ? "support.internal_note" : "support.reply_ticket",
                           ticket_id, {{"message_id", message_id}, {"internal", internal}});
    if (!internal) {
        staff::notify_support_reply(ticket_id, ticket->account_id, ticket->subject, false,
                                    std::string());
    }
    response.data["message_id"] = message_id;
    response.data["ticket"] = ticket_to_json(*StaffStore::instance().get_ticket(ticket_id), true);
    return with_token(response, new_token);
}

ApiResponse handleStaffCreateSupportTicket(GameConfigCache& config_cache, const json& body,
                                           const std::optional<std::string>& username,
                                           const ClientInfo& client,
                                           const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.ticket.manage", response)) return response;
    const int64_t account_id = resolve_account_id(body);
    if (account_id <= 0 || !staff::username_for_account(account_id)) {
        response.error = "account_not_found";
        return response;
    }
    staff::support_ticket ticket;
    ticket.account_id = account_id;
    ticket.character_id = body.value("character_id", static_cast<int64_t>(0));
    if (ticket.character_id > 0) {
        int64_t owner = 0;
        if (!staff::character_account_id(ticket.character_id, owner) || owner != account_id) {
            response.error = "character_not_found";
            return response;
        }
    }
    ticket.category = body.value("category", std::string("other"));
    ticket.subject = body.value("subject", std::string());
    ticket.status = "open";
    ticket.priority = body.value("priority", std::string("normal"));
    if (ticket.subject.empty()) {
        response.error = "subject_required";
        return response;
    }
    std::string error;
    const std::string ticket_id = StaffStore::instance().create_ticket(ticket, error);
    if (ticket_id.empty()) {
        response.error = error.empty() ? "ticket_create_failed" : error;
        return response;
    }
    const std::string text = body.value("body", std::string());
    if (text.size() > 10000) {
        response.error = "message_too_long";
        return response;
    }
    std::string message_id;
    if (!text.empty()) {
        staff::support_ticket_message message;
        message.ticket_id = ticket_id;
        message.author_kind = "staff";
        message.author_account_id = context.account_id;
        message.body = text;
        std::string message_error;
        message_id = StaffStore::instance().append_ticket_message(message, message_error);
        if (message_id.empty()) {
            response.error = message_error.empty() ? "message_create_failed" : message_error;
            return response;
        }
    }
    journal_support_action(context, "support.create_ticket", ticket_id,
                           {{"message_id", message_id}, {"account_id", account_id}});
    staff::notify_support_reply(ticket_id, account_id, ticket.subject, true, std::string());
    response.data["ticket"] = ticket_to_json(*StaffStore::instance().get_ticket(ticket_id), true);
    return with_token(response, new_token);
}

ApiResponse handleStaffSetSupportTicketStatus(GameConfigCache& config_cache, const json& body,
                                              const std::optional<std::string>& username,
                                              const ClientInfo& client,
                                              const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.ticket.manage", response)) return response;
    const std::string ticket_id = body.value("ticket_id", std::string());
    const std::string status = body.value("status", std::string());
    auto ticket = StaffStore::instance().get_ticket(ticket_id);
    if (!ticket) {
        response.error = "ticket_not_found";
        return response;
    }
    std::string error;
    if (!StaffStore::instance().set_ticket_status(ticket_id, status, error)) {
        response.error = error;
        return response;
    }
    journal_support_action(context, "support.set_ticket_status", ticket_id, {{"status", status}});
    response.data["ticket"] = ticket_to_json(*StaffStore::instance().get_ticket(ticket_id), true);
    return with_token(response, new_token);
}

ApiResponse handleStaffAssignSupportTicket(GameConfigCache& config_cache, const json& body,
                                           const std::optional<std::string>& username,
                                           const ClientInfo& client,
                                           const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.ticket.manage", response)) return response;
    const std::string ticket_id = body.value("ticket_id", std::string());
    const int64_t assigned = body.value("assigned_account_id", static_cast<int64_t>(0));
    auto ticket = StaffStore::instance().get_ticket(ticket_id);
    if (!ticket) {
        response.error = "ticket_not_found";
        return response;
    }
    std::string error;
    StaffStore::instance().assign_ticket(ticket_id, assigned, error);
    journal_support_action(context, "support.assign_ticket", ticket_id,
                           {{"assigned_account_id", assigned}});
    response.data["ticket"] = ticket_to_json(*StaffStore::instance().get_ticket(ticket_id), true);
    return with_token(response, new_token);
}

ApiResponse handleStaffLinkSupportAction(GameConfigCache& config_cache, const json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.ticket.manage", response)) return response;
    const std::string ticket_id = body.value("ticket_id", std::string());
    const std::string action_id = body.value("action_id", std::string());
    if (!StaffStore::instance().ticket_exists(ticket_id) ||
        !StaffStore::instance().find_action(action_id)) {
        response.error = "ticket_or_action_not_found";
        return response;
    }
    if (!StaffStore::instance().link_ticket_action(ticket_id, action_id, context.account_id)) {
        response.error = "link_failed";
        return response;
    }
    journal_support_action(context, "support.link_action", ticket_id, {{"action_id", action_id}});
    response.data["linked"] = true;
    return with_token(response, new_token);
}

ApiResponse handleStaffUnstickGameSession(GameConfigCache& config_cache, const json& body,
                                          const std::optional<std::string>& username,
                                          const ClientInfo& client,
                                          const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.unstick_session", response)) return response;
    const int64_t character_id = body.value("character_id", static_cast<int64_t>(0));
    if (character_id <= 0) {
        response.error = "character_id_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "support.unstick_session";
    request.target_type = "character";
    request.target_id = std::to_string(character_id);
    request.reason = body.value("reason", std::string());
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffSetGamePhase(GameConfigCache& config_cache, const json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.set_game_phase", response)) return response;
    const int64_t character_id = body.value("character_id", static_cast<int64_t>(0));
    const std::string phase = body.value("game_phase", std::string());
    if (character_id <= 0 || phase.empty()) {
        response.error = "character_id_and_game_phase_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "support.set_game_phase";
    request.target_type = "character";
    request.target_id = std::to_string(character_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"game_phase", phase}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffSetMarchProgress(GameConfigCache& config_cache, const json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.set_march_progress", response)) return response;
    const int64_t character_id = body.value("character_id", static_cast<int64_t>(0));
    const std::string mini_game = body.value("mini_game", std::string());
    const int next_level = body.value("next_level", 0);
    if (character_id <= 0 || mini_game.empty() || next_level < 1) {
        response.error = "character_id_mini_game_and_next_level_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "support.set_march_progress";
    request.target_type = "character";
    request.target_id = std::to_string(character_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"mini_game", mini_game}, {"next_level", next_level}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffCancelConstruction(GameConfigCache& config_cache, const json& body,
                                          const std::optional<std::string>& username,
                                          const ClientInfo& client,
                                          const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.cancel_construction", response)) return response;
    const int64_t building_id = body.value("building_id", static_cast<int64_t>(0));
    if (building_id <= 0) {
        response.error = "building_id_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "support.cancel_construction";
    request.target_type = "building";
    request.target_id = std::to_string(building_id);
    request.reason = body.value("reason", std::string());
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffAdjustFiefdomResource(GameConfigCache& config_cache, const json& body,
                                             const std::optional<std::string>& username,
                                             const ClientInfo& client,
                                             const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "support.adjust_fiefdom_resource", response)) return response;
    const int64_t fiefdom_id = body.value("fiefdom_id", static_cast<int64_t>(0));
    const std::string resource = body.value("resource", std::string());
    if (fiefdom_id <= 0 || resource.empty() || !body.contains("value") ||
        !body["value"].is_number()) {
        response.error = "fiefdom_id_resource_and_value_required";
        return response;
    }
    staff::staff_action_request request;
    request.action_type = "support.adjust_fiefdom_resource";
    request.target_type = "fiefdom";
    request.target_id = std::to_string(fiefdom_id);
    request.reason = body.value("reason", std::string());
    request.parameters = {{"resource", resource}, {"value", body["value"].get<double>()}};
    return staff_action_response(staff::StaffActionEngine::instance().execute(context, request),
                                 new_token);
}

ApiResponse handleStaffSearchChats(GameConfigCache& config_cache, const json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "chat.moderate_any", response)) return response;
    const std::string query = body.value("query", std::string());
    const std::string like = "%" + query + "%";
    auto& db = Database::getInstance().chatDB();
    json chats = json::array();
    db << "SELECT id, type, COALESCE(name, ''), COALESCE(owner_character_id, 0), "
          "COALESCE(direct_key, ''), created_at FROM chats "
          "WHERE (? = '' OR name LIKE ? OR id = ?) ORDER BY created_at DESC LIMIT 100;"
       << query << like << query
       >> [&](std::string id, std::string type, std::string name, int64_t owner,
              std::string direct_key, int64_t created_at) {
              chats.push_back({{"id", id},
                               {"type", type},
                               {"name", name},
                               {"owner_character_id", owner},
                               {"direct_key", direct_key},
                               {"created_at", created_at}});
          };
    response.data["chats"] = std::move(chats);
    staff::record_read_event(context, "read.chat_messages", "chat_search", query.substr(0, 64),
                             std::string(), {{"query", query}});
    return with_token(response, new_token);
}

ApiResponse handleStaffGetDeletedMessages(GameConfigCache& config_cache, const json& body,
                                          const std::optional<std::string>& username,
                                          const ClientInfo& client,
                                          const std::optional<std::string>& new_token) {
    ApiResponse response;
    staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "staff.read_sensitive", response)) return response;
    visibility::viewer_context viewer;
    viewer.account_id = context.account_id;
    response.data["messages"] =
        visibility::list_deleted_messages(config_cache, viewer, body.value("limit", 100));
    staff::record_read_event(context, "read.deleted_messages", "deleted_messages", "list",
                             std::string(), {{"limit", body.value("limit", 100)}});
    return with_token(response, new_token);
}

void register_staff_handlers(std::unordered_map<std::string, ApiHandler>& handlers) {
    auto add = [&](const std::string& name, ApiHandler handler) {
        handlers[name] = std::move(handler);
    };
    auto reg = [&](const std::string& name, auto fn) {
        add(name, [fn](GameConfigCache& c, const nlohmann::json& b,
                       const std::optional<std::string>& u, const ClientInfo& cl,
                       const std::optional<std::string>& t) { return fn(c, b, u, cl, t); });
    };

    // Player-facing support + notifications (available to every account).
    reg("getSupportContext", handleGetSupportContext);
    reg("listSupportTickets", handleListSupportTickets);
    reg("getSupportTicket", handleGetSupportTicket);
    reg("createSupportTicket", handleCreateSupportTicket);
    reg("replySupportTicket", handleReplySupportTicket);
    reg("reopenSupportTicket", handleReopenSupportTicket);
    reg("getNotifications", handleGetNotifications);
    reg("markNotificationRead", handleMarkNotificationRead);
    reg("markAllNotificationsRead", handleMarkAllNotificationsRead);

    // Staff console.
    reg("staffGetContext", handleStaffGetContext);
    reg("staffListStaff", handleStaffListStaff);
    reg("staffSetChatModerationMode", handleStaffSetChatModerationMode);
    reg("staffSearchAccounts", handleStaffSearchAccounts);
    reg("staffGetAccount", handleStaffGetAccount);
    reg("staffSetAdultStatus", handleStaffSetAdultStatus);
    reg("staffSetSocialRestriction", handleStaffSetSocialRestriction);
    reg("staffSetSupportLock", handleStaffSetSupportLock);
    reg("staffGetIdentityHistory", handleStaffGetIdentityHistory);
    reg("staffLockIdentity", handleStaffLockIdentity);
    reg("staffRevertIdentity", handleStaffRevertIdentity);
    reg("staffRemoveCustomAvatar", handleStaffRemoveCustomAvatar);
    reg("staffBanAvatarHash", handleStaffBanAvatarHash);
    reg("staffUnbanAvatarHash", handleStaffUnbanAvatarHash);
    reg("staffListAvatarHashBans", handleStaffListAvatarHashBans);
    reg("staffSearchBaronies", handleStaffSearchBaronies);
    reg("staffSetBaronyIdentity", handleStaffSetBaronyIdentity);
    reg("staffSetBaronyAcceptsMinors", handleStaffSetBaronyAcceptsMinors);
    reg("staffReassignBaronyOwner", handleStaffReassignBaronyOwner);
    reg("staffListJournal", handleStaffListJournal);
    reg("staffGetJournalEntry", handleStaffGetJournalEntry);
    reg("staffPreflightUndo", handleStaffPreflightUndo);
    reg("staffUndo", handleStaffUndo);
    reg("staffRecoverPendingAction", handleStaffRecoverPendingAction);
    reg("staffListSupportQueue", handleStaffListSupportQueue);
    reg("staffGetSupportTicket", handleStaffGetSupportTicket);
    reg("staffReplySupportTicket", handleStaffReplySupportTicket);
    reg("staffCreateSupportTicket", handleStaffCreateSupportTicket);
    reg("staffSetSupportTicketStatus", handleStaffSetSupportTicketStatus);
    reg("staffAssignSupportTicket", handleStaffAssignSupportTicket);
    reg("staffLinkSupportAction", handleStaffLinkSupportAction);
    reg("staffUnstickGameSession", handleStaffUnstickGameSession);
    reg("staffSetGamePhase", handleStaffSetGamePhase);
    reg("staffSetMarchProgress", handleStaffSetMarchProgress);
    reg("staffCancelConstruction", handleStaffCancelConstruction);
    reg("staffAdjustFiefdomResource", handleStaffAdjustFiefdomResource);
    reg("staffSearchChats", handleStaffSearchChats);
    reg("staffGetDeletedMessages", handleStaffGetDeletedMessages);
}
