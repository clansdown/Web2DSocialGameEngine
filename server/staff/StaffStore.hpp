#pragma once
// staff.db persistence: staff roles/accounts, the append-only action journal,
// support tickets, notifications, and banned avatar hashes.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "staff/StaffTypes.hpp"

namespace staff {

std::string new_id(const std::string& prefix);
int64_t now_seconds();

class StaffStore {
public:
    static StaffStore& instance();

    // Identity / roles
    staff_context load_context(int64_t account_id);
    bool is_staff(int64_t account_id);
    bool has_capability(int64_t account_id, const std::string& capability);
    bool role_exists(const std::string& role_id);
    std::vector<staff_role> list_roles();
    std::vector<staff_account_summary> list_staff();
    bool grant_staff(int64_t account_id, const std::string& role_id,
                     const std::string& granted_by_label, std::string& error);
    bool revoke_staff(int64_t account_id, const std::string& revoked_by_label,
                      std::string& error);
    bool set_chat_moderation_mode(int64_t account_id, bool enabled);
    bool chat_moderation_mode(int64_t account_id);

    // Journal
    std::string append_action(staff_action_journal_entry& entry, std::string& error);
    void append_event(const std::string& action_id, const std::string& event_type,
                      const json& detail = json::object());
    bool update_action_status(const std::string& action_id, const std::string& status,
                              const std::string& error = std::string());
    std::optional<staff_action_journal_entry> find_action(const std::string& action_id);
    std::vector<staff_action_journal_entry> list_actions(const staff_journal_filter& filter);
    void link_resource(const std::string& action_id, const staff_resource_ref& resource);
    std::vector<std::string> action_ids_for_ticket(const std::string& ticket_id);
    void recover_interrupted_actions();

    // Notifications
    std::string create_notification(staff_notification_spec spec);
    std::vector<notification_record> list_notifications(int64_t account_id, int limit,
                                                        int offset);
    int64_t unread_notifications(int64_t account_id);
    bool mark_notification_read(int64_t account_id, const std::string& notification_id);
    void mark_all_notifications_read(int64_t account_id);

    // Support tickets
    std::string create_ticket(support_ticket& ticket, std::string& error);
    bool ticket_exists(const std::string& ticket_id);
    std::optional<support_ticket> get_ticket(const std::string& ticket_id);
    std::vector<support_ticket> list_tickets_for_account(int64_t account_id,
                                                         const support_ticket_filter& filter);
    std::vector<support_ticket> list_staff_tickets(const support_ticket_filter& filter);
    std::string append_ticket_message(support_ticket_message& message, std::string& error);
    std::vector<support_ticket_message> list_ticket_messages(const std::string& ticket_id,
                                                             bool include_internal);
    bool set_ticket_status(const std::string& ticket_id, const std::string& status,
                           std::string& error);
    bool assign_ticket(const std::string& ticket_id, int64_t staff_account_id,
                       std::string& error);
    bool link_ticket_action(const std::string& ticket_id, const std::string& action_id,
                            int64_t staff_account_id);
    void mark_ticket_player_read(const std::string& ticket_id);
    void mark_ticket_staff_read(const std::string& ticket_id);
    int unread_tickets_for_player(int64_t account_id);
    int unread_tickets_for_staff(int64_t account_id);

    // Avatar hash bans
    bool is_hash_banned(const std::string& sha256);
    bool ban_hash(const std::string& sha256, const std::string& reason,
                  int64_t staff_account_id, const std::string& action_id, std::string& error);
    bool unban_hash(const std::string& sha256, int64_t staff_account_id,
                    const std::string& action_id, std::string& error);
    std::vector<banned_avatar_hash> list_hash_bans(bool include_revoked);

private:
    StaffStore() = default;
};

}  // namespace staff
