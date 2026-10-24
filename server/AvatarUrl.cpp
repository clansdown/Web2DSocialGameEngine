#include "AvatarUrl.hpp"

#include <memory>
#include <utility>

#include "AvatarPool.hpp"
#include "Database.hpp"

namespace {
avatar_url_config g_avatar_url_config;
}

void configure_avatar_urls(const avatar_url_config& config) {
    g_avatar_url_config = config;
    while (!g_avatar_url_config.public_base_url.empty() &&
           g_avatar_url_config.public_base_url.back() == '/') {
        g_avatar_url_config.public_base_url.pop_back();
    }
    if (g_avatar_url_config.r2_prefix.empty()) {
        g_avatar_url_config.r2_prefix = "avatars/";
    }
    while (!g_avatar_url_config.r2_prefix.empty() &&
           g_avatar_url_config.r2_prefix.front() == '/') {
        g_avatar_url_config.r2_prefix.erase(g_avatar_url_config.r2_prefix.begin());
    }
    if (g_avatar_url_config.r2_prefix.empty()) {
        g_avatar_url_config.r2_prefix = "avatars/";
    }
    if (g_avatar_url_config.r2_prefix.back() != '/') {
        g_avatar_url_config.r2_prefix.push_back('/');
    }
}

std::string build_avatar_public_url(std::string_view storage, std::string_view key) {
    if (storage != "r2" || key.empty() || g_avatar_url_config.public_base_url.empty()) {
        return "";
    }
    return g_avatar_url_config.public_base_url + "/" + g_avatar_url_config.r2_prefix +
           std::string(key);
}

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
    std::string avatar_storage = "local";
    int owner_user_id = 0;
    db << "SELECT avatar_safe_id, avatar_custom_id, "
          "COALESCE(avatar_storage, 'local'), user_id FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::unique_ptr<std::string> s, std::unique_ptr<std::string> c,
              std::string storage, int uid) {
              safe_id = std::move(s);
              custom_id = std::move(c);
              avatar_storage = std::move(storage);
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
        const std::string r2_url = build_avatar_public_url(avatar_storage, *custom_id);
        if (!r2_url.empty()) return r2_url;
        return "/api/avatar/" + std::to_string(character_id);
    }
    if (safe_id && !safe_id->empty()) {
        return AvatarPool::safe_avatar_url(*safe_id);
    }
    return "";
}
