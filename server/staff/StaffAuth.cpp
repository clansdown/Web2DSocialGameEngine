#include "staff/StaffAuth.hpp"

#include "Database.hpp"
#include "staff/StaffStore.hpp"

namespace staff {

std::optional<int64_t> account_id_for_username(const std::string& username) {
    if (username.empty()) return std::nullopt;
    auto& db = Database::getInstance().gameDB();
    std::optional<int64_t> id;
    db << "SELECT id FROM users WHERE username = ?;" << username
       >> [&](int64_t value) { id = value; };
    return id;
}

std::optional<std::string> username_for_account(int64_t account_id) {
    if (account_id <= 0) return std::nullopt;
    auto& db = Database::getInstance().gameDB();
    std::optional<std::string> username;
    db << "SELECT username FROM users WHERE id = ?;" << account_id
       >> [&](std::string value) { username = std::move(value); };
    return username;
}

bool character_account_id(int64_t character_id, int64_t& account_id) {
    if (character_id <= 0) return false;
    auto& db = Database::getInstance().gameDB();
    db << "SELECT user_id FROM characters WHERE id = ?;" << character_id
       >> [&](int64_t value) { account_id = value; };
    return account_id > 0;
}

staff_context context_for_username(const std::optional<std::string>& username) {
    if (!username) return staff_context{};
    const auto id = account_id_for_username(*username);
    if (!id) return staff_context{};
    return StaffStore::instance().load_context(*id);
}

bool require_capability(const staff_context& context, const std::string& capability,
                        ApiResponse& response) {
    if (!context.is_staff()) {
        response.error = "not_staff";
        return false;
    }
    if (!context.has(capability)) {
        response.error = "missing_capability:" + capability;
        return false;
    }
    return true;
}

bool staff_can_view_chat(const staff_context& context) {
    return context.has("chat.moderate_any");
}

bool staff_can_moderate_chat(const staff_context& context) {
    return context.has("chat.moderate_any");
}

bool account_is_social_restricted(int64_t account_id) {
    if (account_id <= 0) return false;
    auto& db = Database::getInstance().gameDB();
    bool restricted = false;
    db << "SELECT social_restricted FROM users WHERE id = ?;" << account_id
       >> [&](int value) { restricted = value != 0; };
    return restricted;
}

bool account_is_support_locked(int64_t account_id) {
    if (account_id <= 0) return false;
    auto& db = Database::getInstance().gameDB();
    bool locked = false;
    db << "SELECT support_locked FROM users WHERE id = ?;" << account_id
       >> [&](int value) { locked = value != 0; };
    return locked;
}

bool character_is_social_restricted(int64_t character_id) {
    int64_t account_id = 0;
    return character_account_id(character_id, account_id) &&
           account_is_social_restricted(account_id);
}

}  // namespace staff
