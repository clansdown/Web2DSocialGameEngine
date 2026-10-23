#include "AvatarUrl.hpp"

#include <memory>

#include "AvatarPool.hpp"
#include "Database.hpp"

bool account_is_adult(int64_t character_id) {
    if (character_id <= 0) return false;
    bool adult = false;
    Database::getInstance().gameDB()
        << "SELECT u.adult FROM characters c JOIN users u ON u.id = c.user_id "
           "WHERE c.id = ?;"
        << character_id >> [&](bool a) { adult = a; };
    return adult;
}

std::string resolve_avatar_url(int64_t character_id, bool viewer_is_adult) {
    if (character_id <= 0) return "";
    auto& db = Database::getInstance().gameDB();
    std::unique_ptr<std::string> safe_id;
    std::unique_ptr<std::string> custom_id;
    int owner_user_id = 0;
    db << "SELECT avatar_safe_id, avatar_custom_id, user_id FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::unique_ptr<std::string> s, std::unique_ptr<std::string> c, int uid) {
              safe_id = std::move(s);
              custom_id = std::move(c);
              owner_user_id = uid;
          };
    bool owner_adult = false;
    if (owner_user_id > 0) {
        db << "SELECT adult FROM users WHERE id = ?;" << owner_user_id
           >> [&](bool a) { owner_adult = a; };
    }
    // Custom is layered on top of the safe pick, but only for adult viewers of
    // adult owners. Minors (and the fallback) always get the safe portrait.
    if (viewer_is_adult && owner_adult && custom_id && !custom_id->empty()) {
        return "/api/avatar/" + std::to_string(character_id);
    }
    if (safe_id && !safe_id->empty()) {
        return AvatarPool::safe_avatar_url(*safe_id);
    }
    return "";
}
