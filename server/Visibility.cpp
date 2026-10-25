#include "Visibility.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
#include <vector>

#include "AvatarPool.hpp"
#include "AvatarUrl.hpp"
#include "Database.hpp"
#include "GameConfigCache.hpp"
#include "chat/ChatDB.hpp"

namespace visibility {
namespace {

using nlohmann::json;

json empty_array() { return json::array(); }

const json& catalog_root(GameConfigCache& cache, safe_catalog_kind kind) {
    static const json empty = json::object();
    switch (kind) {
        case safe_catalog_kind::message: return cache.getSafeMessages();
        case safe_catalog_kind::barony_name: return cache.getSafeBaronyNames();
        case safe_catalog_kind::barony_tag: return cache.getSafeBaronyTags();
        case safe_catalog_kind::reaction: return cache.getSafeReactions();
        case safe_catalog_kind::gif: return cache.getSafeGifs();
        case safe_catalog_kind::ban_reason: return cache.getSafeBanReasons();
    }
    return empty;
}

std::string catalog_key(safe_catalog_kind kind) {
    switch (kind) {
        case safe_catalog_kind::message: return "messages";
        case safe_catalog_kind::barony_name: return "names";
        case safe_catalog_kind::barony_tag: return "tags";
        case safe_catalog_kind::reaction: return "reactions";
        case safe_catalog_kind::gif: return "gifs";
        case safe_catalog_kind::ban_reason: return "ban_reasons";
    }
    return "";
}

std::string entry_text(const json& entry, std::initializer_list<const char*> keys) {
    if (!entry.is_object()) return "";
    for (const char* key : keys) {
        if (entry.contains(key) && entry[key].is_string()) {
            return entry[key].get<std::string>();
        }
    }
    return "";
}

std::string id_csv(const std::vector<int64_t>& ids) {
    std::ostringstream out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) out << ',';
        out << ids[i];
    }
    return out.str();
}

std::vector<int64_t> account_character_ids(int64_t account_id) {
    std::vector<int64_t> ids;
    Database::getInstance().gameDB()
        << "SELECT id FROM characters WHERE user_id = ? ORDER BY id;" << account_id
        >> [&](int64_t id) { ids.push_back(id); };
    return ids;
}

std::string safe_barony_name_for_id(GameConfigCache& cache, const std::string& id) {
    if (id.empty()) return "";
    return safe_label(cache, safe_catalog_kind::barony_name, id);
}

}  // namespace

// ── Viewer context ──────────────────────────────────────────────────────────

viewer_context load_viewer_context(const std::string& username) {
    viewer_context viewer;
    if (username.empty()) return viewer;
    Database::getInstance().gameDB()
        << "SELECT id, adult FROM users WHERE username = ?;" << username
        >> [&](int64_t id, bool adult) {
               viewer.account_id = id;
               viewer.is_adult = adult;
           };
    return viewer;
}

viewer_context load_viewer_context_for_character(int64_t character_id) {
    viewer_context viewer;
    viewer.character_id = character_id;
    if (character_id <= 0) return viewer;
    Database::getInstance().gameDB()
        << "SELECT u.id, u.adult FROM characters c JOIN users u ON u.id = c.user_id "
           "WHERE c.id = ?;"
        << character_id
        >> [&](int64_t id, bool adult) {
               viewer.account_id = id;
               viewer.is_adult = adult;
           };
    return viewer;
}

bool account_is_adult(int64_t account_id) {
    if (account_id <= 0) return false;
    bool adult = false;
    Database::getInstance().gameDB()
        << "SELECT adult FROM users WHERE id = ?;" << account_id
        >> [&](bool value) { adult = value; };
    return adult;
}

bool character_is_adult(int64_t character_id) {
    return load_viewer_context_for_character(character_id).is_adult;
}

// ── Safe catalogs ───────────────────────────────────────────────────────────

std::vector<json> safe_catalog(GameConfigCache& config_cache, safe_catalog_kind kind) {
    std::vector<json> out;
    const json& root = catalog_root(config_cache, kind);
    const std::string key = catalog_key(kind);
    if (!root.is_object() || !root.contains(key) || !root[key].is_array()) {
        return out;
    }
    for (const auto& entry : root[key]) {
        if (entry.is_object()) out.push_back(entry);
    }
    return out;
}

std::optional<json> find_safe_entry(GameConfigCache& config_cache,
                                    safe_catalog_kind kind, const std::string& id) {
    if (id.empty()) return std::nullopt;
    for (const auto& entry : safe_catalog(config_cache, kind)) {
        if (entry.value("id", std::string()) == id) return entry;
    }
    return std::nullopt;
}

bool is_valid_safe_id(GameConfigCache& config_cache, safe_catalog_kind kind,
                      const std::string& id) {
    return find_safe_entry(config_cache, kind, id).has_value();
}

bool is_minor_safe(GameConfigCache& config_cache, safe_catalog_kind kind,
                   const std::string& id) {
    auto entry = find_safe_entry(config_cache, kind, id);
    if (!entry) return false;
    switch (kind) {
        case safe_catalog_kind::reaction:
        case safe_catalog_kind::gif:
            return entry->value("minor_safe", false);
        default:
            return true;
    }
}

std::string safe_message_body(GameConfigCache& config_cache, const std::string& id) {
    auto entry = find_safe_entry(config_cache, safe_catalog_kind::message, id);
    if (!entry) return "";
    return entry_text(*entry, {"body", "label", "name"});
}

std::string safe_label(GameConfigCache& config_cache, safe_catalog_kind kind,
                       const std::string& id) {
    auto entry = find_safe_entry(config_cache, kind, id);
    if (!entry) return "";
    return entry_text(*entry, {"name", "label", "body"});
}

// ── Character identity ──────────────────────────────────────────────────────

std::optional<character_identity> load_character_identity(int64_t character_id) {
    if (character_id <= 0) return std::nullopt;
    character_identity out;
    bool found = false;
    Database::getInstance().gameDB()
        << "SELECT c.id, c.user_id, c.safe_display_name, c.display_name, "
           "COALESCE(c.avatar_safe_id, ''), COALESCE(c.avatar_custom_id, ''), "
           "COALESCE(c.avatar_storage, 'local'), c.avatar_changed_at, c.level, "
           "COALESCE(c.archetype, ''), COALESCE(c.sex, ''), u.adult "
           "FROM characters c JOIN users u ON u.id = c.user_id WHERE c.id = ?;"
        << character_id
        >> [&](int64_t id, int64_t account_id, std::string safe_name, std::string custom_name,
               std::string safe_avatar, std::string custom_avatar, std::string storage,
               int64_t changed_at, int level, std::string archetype, std::string sex,
               bool owner_adult) {
               out.character_id = id;
               out.account_id = account_id;
               out.safe_display_name = std::move(safe_name);
               out.custom_display_name = std::move(custom_name);
               out.safe_avatar_id = std::move(safe_avatar);
               out.custom_avatar_id = std::move(custom_avatar);
               out.avatar_storage = std::move(storage);
               out.avatar_changed_at = changed_at;
               out.level = level;
               out.archetype = std::move(archetype);
               out.sex = std::move(sex);
               out.owner_is_adult = owner_adult;
               found = true;
           };
    if (!found) return std::nullopt;
    return out;
}

namespace {

std::string avatar_url_for(const viewer_context& viewer, const character_identity& identity) {
    if (viewer.is_adult && identity.owner_is_adult && !identity.custom_avatar_id.empty()) {
        const std::string r2_url =
            build_avatar_public_url(identity.avatar_storage, identity.custom_avatar_id);
        if (!r2_url.empty()) return r2_url;
        return "/api/avatar/" + std::to_string(identity.character_id);
    }
    if (!identity.safe_avatar_id.empty()) {
        return AvatarPool::safe_avatar_url(identity.safe_avatar_id);
    }
    return "";
}

json base_identity_json(const viewer_context& viewer, const character_identity& identity) {
    json out;
    out["id"] = identity.character_id;
    const bool use_custom = viewer.is_adult && identity.owner_is_adult &&
                            !identity.custom_display_name.empty();
    out["display_name"] = use_custom ? identity.custom_display_name : identity.safe_display_name;
    out["safe_display_name"] = identity.safe_display_name;
    out["level"] = identity.level;
    out["archetype"] = identity.archetype.empty() ? json(nullptr) : json(identity.archetype);
    out["sex"] = identity.sex.empty() ? json(nullptr) : json(identity.sex);
    out["avatar_url"] = avatar_url_for(viewer, identity);
    return out;
}

}  // namespace

json project_character_identity(const viewer_context& viewer,
                                const character_identity& identity) {
    return base_identity_json(viewer, identity);
}

json project_character_identity_for_self(const viewer_context& viewer,
                                         const character_identity& identity) {
    json out = base_identity_json(viewer, identity);
    out["account_id"] = identity.account_id;
    out["avatar_safe_id"] =
        identity.safe_avatar_id.empty() ? json(nullptr) : json(identity.safe_avatar_id);
    out["has_custom_avatar"] = !identity.custom_avatar_id.empty();
    out["avatar_changed_at"] = identity.avatar_changed_at;
    out["is_adult"] = viewer.is_adult;
    out["can_set_custom_display_name"] = viewer.is_adult;
    out["can_upload_custom_avatar"] = viewer.is_adult;
    out["can_use_voice"] = viewer.is_adult;
    return out;
}

// ── Baronies ────────────────────────────────────────────────────────────────

std::string barony_display_name(GameConfigCache& config_cache,
                                const viewer_context& viewer,
                                const barony_identity& barony) {
    const std::string safe_name = safe_barony_name_for_id(config_cache, barony.safe_name_id);
    if (viewer.is_minor()) {
        // Never fall back to custom text for a minor. Legacy rows without a
        // safe id render an empty name until an admin assigns one.
        return safe_name;
    }
    if (!barony.custom_name.empty()) return barony.custom_name;
    return safe_name;
}

std::string barony_display_description(GameConfigCache& config_cache,
                                       const viewer_context& viewer,
                                       const barony_identity& barony) {
    (void)config_cache;
    if (viewer.is_minor()) return "";
    return barony.custom_description;
}

std::vector<json> barony_tag_labels(GameConfigCache& config_cache,
                                    const viewer_context& viewer,
                                    const barony_identity& barony) {
    (void)viewer;
    std::vector<json> out;
    for (const auto& tag_id : barony.safe_tag_ids) {
        const std::string label = safe_label(config_cache, safe_catalog_kind::barony_tag, tag_id);
        if (label.empty()) continue;
        out.push_back({{"id", tag_id}, {"name", label}});
    }
    return out;
}

json project_barony(GameConfigCache& config_cache, const viewer_context& viewer,
                    const barony_identity& barony) {
    json out;
    out["id"] = barony.id;
    out["name"] = barony_display_name(config_cache, viewer, barony);
    out["description"] = barony_display_description(config_cache, viewer, barony);
    out["safe_name_id"] = barony.safe_name_id;
    out["safe_tag_ids"] = barony.safe_tag_ids;
    out["tags"] = barony_tag_labels(config_cache, viewer, barony);
    out["accepts_minors"] = barony.accepts_minors;
    out["member_count"] = barony.member_count;
    return out;
}

// ── Authoring / access policy ───────────────────────────────────────────────

bool can_author_custom_content(int64_t character_id) {
    return character_is_adult(character_id);
}

bool can_use_voice(int64_t character_id) {
    return character_is_adult(character_id);
}

bool can_create_group(int64_t character_id) {
    return character_is_adult(character_id);
}

bool can_join_group(int64_t character_id) {
    return character_is_adult(character_id);
}

// ── Adult status / demotion ─────────────────────────────────────────────────

void admin_set_account_adult_status(GameConfigCache& config_cache, int64_t account_id,
                                    bool adult, int64_t acting_admin_account_id,
                                    const std::string& method,
                                    const std::string& reason) {
    if (account_id <= 0) return;
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    auto& gdb = Database::getInstance().gameDB();
    gdb << "UPDATE users SET adult = ?, adult_verified_at = ?, adult_verified_by = ?, "
           "adult_verification_method = ? WHERE id = ?;"
        << (adult ? 1 : 0) << now
        << (acting_admin_account_id > 0 ? std::optional<int64_t>(acting_admin_account_id)
                                        : std::optional<int64_t>())
        << (method.empty() ? std::optional<std::string>() : std::optional<std::string>(method))
        << account_id;

    chat::ChatDB::instance().db()
        << "INSERT INTO moderation_log (created_at, actor_character_id, action, chat_id, "
           "target_character_id, payload) VALUES (?, NULL, 'adult_status', NULL, NULL, ?);"
        << now
        << json{{"account_id", account_id},
                {"adult", adult},
                {"method", method},
                {"reason", reason},
                {"admin_account_id", acting_admin_account_id}}
               .dump();

    if (!adult) {
        demote_account_to_minor(config_cache, account_id, acting_admin_account_id, reason);
    }
}

void demote_account_to_minor(GameConfigCache& config_cache, int64_t account_id,
                             int64_t acting_admin_account_id,
                             const std::string& reason) {
    if (account_id <= 0) return;
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    auto& gdb = Database::getInstance().gameDB();
    auto& cdb = chat::ChatDB::instance().db();

    gdb << "UPDATE users SET adult = 0 WHERE id = ?;" << account_id;

    const std::vector<int64_t> character_ids = account_character_ids(account_id);
    if (character_ids.empty()) return;
    const std::string chars = id_csv(character_ids);

    // ── Identity + honor titles ────────────────────────────────────────────
    for (int64_t character_id : character_ids) {
        gdb << "UPDATE characters SET display_name = safe_display_name, "
                "avatar_custom_id = NULL, avatar_storage = 'local' WHERE id = ?;"
            << character_id;
        // The knight row is a snapshot of the character's name.
        gdb << "UPDATE retinue_members SET display_name = "
                "(SELECT safe_display_name FROM characters WHERE id = ?) "
                "WHERE character_id = ? AND is_knight = 1;"
            << character_id << character_id;

        std::string safe_name_id;
        gdb << "SELECT COALESCE(safe_barony_name_id, '') FROM player_game_state "
                "WHERE character_id = ?;"
            << character_id >> safe_name_id;
        const std::string honor =
            safe_barony_name_for_id(config_cache, safe_name_id);
        if (!honor.empty()) {
            gdb << "UPDATE player_game_state SET honor_name = ? WHERE character_id = ?;"
                << honor << character_id;
        } else {
            gdb << "UPDATE player_game_state SET honor_name = NULL WHERE character_id = ?;"
                << character_id;
        }
    }

    // Barony identity/title scrub: custom names and descriptions are removed,
    // the safe pick is retained, and minor membership is always allowed after a
    // demotion (a minor can own or join the barony safely).
    {
        struct barony_row {
            int64_t id = 0;
            std::string safe_name_id;
            std::string current_name;
        };
        std::vector<barony_row> baronies;
        gdb << "SELECT id, COALESCE(safe_name_id, ''), COALESCE(name, '') FROM baronies "
               "WHERE owner_character_id IN (" + chars + ") OR id IN "
               "(SELECT barony_id FROM barony_members WHERE character_id IN (" + chars + "));"
            >> [&](int64_t id, std::string safe_id, std::string current_name) {
                   baronies.push_back({id, std::move(safe_id), std::move(current_name)});
               };
        for (const auto& barony : baronies) {
            std::string safe_name =
                safe_barony_name_for_id(config_cache, barony.safe_name_id);
            if (safe_name.empty()) safe_name = barony.current_name;
            gdb << "UPDATE baronies SET name = ?, description = '', accepts_minors = 1 "
                   "WHERE id = ?;"
                << safe_name << barony.id;
        }
        // Any barony with a demoted member must accept minors even if it was not
        // owned by the demoted account.
        gdb << "UPDATE baronies SET accepts_minors = 1 WHERE id IN "
                "(SELECT barony_id FROM barony_members WHERE character_id IN (" + chars + "));";
    }

    // ── Groups: remove memberships, transfer ownership to oldest adult ─────
    struct group_owner {
        std::string chat_id;
        int64_t owner_character_id = 0;
    };
    std::vector<group_owner> owned_groups;
    cdb << "SELECT id, COALESCE(owner_character_id, 0) FROM chats WHERE type = 'group' "
           "AND owner_character_id IN (" + chars + ");"
        >> [&](std::string chat_id, int64_t owner) {
               owned_groups.push_back({std::move(chat_id), owner});
           };

    for (const auto& group : owned_groups) {
        std::vector<int64_t> members;
        cdb << "SELECT character_id FROM chat_members WHERE chat_id = ? AND left_at IS NULL "
               "ORDER BY joined_at ASC;"
            << group.chat_id >> [&](int64_t cid) { members.push_back(cid); };

        int64_t successor = 0;
        for (int64_t candidate : members) {
            if (std::find(character_ids.begin(), character_ids.end(), candidate) !=
                character_ids.end()) {
                continue;
            }
            if (character_is_adult(candidate)) {
                successor = candidate;
                break;
            }
        }

        const int64_t change_id = chat::ChatDB::instance().allocate_change_id();
        if (successor > 0) {
            cdb << "UPDATE chat_members SET role = 'owner', change_id = ? "
                   "WHERE chat_id = ? AND character_id = ?;"
                << change_id << group.chat_id << successor;
            cdb << "UPDATE chats SET owner_character_id = ?, change_id = ? WHERE id = ?;"
                << successor << change_id << group.chat_id;
        } else {
            cdb << "UPDATE chats SET archived_at = ?, change_id = ? WHERE id = ?;"
                << now << change_id << group.chat_id;
        }
    }

    {
        const int64_t change_id = chat::ChatDB::instance().allocate_change_id();
        cdb << "UPDATE chat_members SET left_at = ?, change_id = ? "
               "WHERE character_id IN (" + chars + ") AND left_at IS NULL "
               "AND chat_id IN (SELECT id FROM chats WHERE type = 'group');"
            << now << change_id;
        cdb << "DELETE FROM chat_invites WHERE invited_character_id IN (" + chars + ") "
               "OR invited_by_character_id IN (" + chars + ");";
    }

    // ── Message deletion (forensic copy + tombstone) ──────────────────────
    struct deleted_message {
        std::string id;
        std::string chat_id;
        int64_t author = 0;
        std::string body;
        std::string safe_message_id;
        std::string gif_id;
        std::string source_language;
        std::string reply_to;
        std::string reply_excerpt;
        std::string reply_safe_excerpt;
        int64_t created_at = 0;
    };
    std::vector<deleted_message> messages;
    cdb << "SELECT id, chat_id, COALESCE(author_character_id, 0), COALESCE(body, ''), "
           "COALESCE(safe_message_id, ''), COALESCE(gif_id, ''), "
           "COALESCE(source_language, ''), COALESCE(reply_to_message_id, ''), "
           "COALESCE(reply_excerpt, ''), COALESCE(reply_safe_excerpt, ''), created_at "
           "FROM messages WHERE author_character_id IN (" + chars + ");"
        >> [&](std::string id, std::string chat_id, int64_t author, std::string body,
               std::string safe_id, std::string gif_id, std::string source_language,
               std::string reply_to, std::string reply_excerpt, std::string reply_safe,
               int64_t created_at) {
               messages.push_back({std::move(id), std::move(chat_id), author, std::move(body),
                                   std::move(safe_id), std::move(gif_id),
                                   std::move(source_language), std::move(reply_to),
                                   std::move(reply_excerpt), std::move(reply_safe), created_at});
           };

    for (const auto& message : messages) {
        cdb << "INSERT INTO message_deletions (message_id, chat_id, author_character_id, "
               "author_account_id, custom_body, safe_message_id, gif_id, source_language, "
               "reply_to_message_id, reply_custom_excerpt, reply_safe_excerpt, created_at, "
               "deleted_at, deletion_reason, deleted_by_account_id) "
               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"
            << message.id << message.chat_id
            << (message.author > 0 ? std::optional<int64_t>(message.author)
                                   : std::optional<int64_t>())
            << account_id
            << (message.body.empty() ? std::optional<std::string>()
                                     : std::optional<std::string>(message.body))
            << (message.safe_message_id.empty() ? std::optional<std::string>()
                                                : std::optional<std::string>(message.safe_message_id))
            << (message.gif_id.empty() ? std::optional<std::string>()
                                       : std::optional<std::string>(message.gif_id))
            << (message.source_language.empty() ? std::optional<std::string>()
                                                : std::optional<std::string>(message.source_language))
            << (message.reply_to.empty() ? std::optional<std::string>()
                                         : std::optional<std::string>(message.reply_to))
            << (message.reply_excerpt.empty() ? std::optional<std::string>()
                                              : std::optional<std::string>(message.reply_excerpt))
            << (message.reply_safe_excerpt.empty()
                    ? std::optional<std::string>()
                    : std::optional<std::string>(message.reply_safe_excerpt))
            << message.created_at << now << reason << acting_admin_account_id;
    }

    if (!messages.empty()) {
        cdb << "DELETE FROM message_translations WHERE message_id IN "
               "(SELECT id FROM messages WHERE author_character_id IN (" + chars + "));";
        cdb << "DELETE FROM message_reactions WHERE message_id IN "
               "(SELECT id FROM messages WHERE author_character_id IN (" + chars + ")) "
               "OR character_id IN (" + chars + ");";
        cdb << "UPDATE messages SET body = NULL, safe_message_id = NULL, gif_id = NULL, "
               "reply_excerpt = NULL, reply_safe_excerpt = NULL, deleted_at = ?, "
               "deletion_reason = ?, deleted_by_account_id = ? "
               "WHERE author_character_id IN (" + chars + ");"
            << now << reason << acting_admin_account_id;
    }

    cdb << "INSERT INTO moderation_log (created_at, actor_character_id, action, chat_id, "
           "target_character_id, payload) VALUES (?, NULL, 'account_demoted', NULL, NULL, ?);"
        << now
        << json{{"account_id", account_id},
                {"character_ids", character_ids},
                {"reason", reason},
                {"admin_account_id", acting_admin_account_id},
                {"deleted_messages", messages.size()}}
               .dump();
}

json list_deleted_messages(GameConfigCache& config_cache, const viewer_context& admin_viewer,
                           int64_t limit) {
    (void)config_cache;
    (void)admin_viewer;
    if (limit <= 0 || limit > 500) limit = 100;
    json out = json::array();
    chat::ChatDB::instance().db()
        << "SELECT id, message_id, chat_id, author_character_id, author_account_id, "
           "custom_body, safe_message_id, gif_id, source_language, reply_to_message_id, "
           "reply_custom_excerpt, reply_safe_excerpt, created_at, deleted_at, "
           "deletion_reason, deleted_by_account_id FROM message_deletions "
           "ORDER BY id DESC LIMIT ?;"
        << limit
        >> [&](int64_t id, std::string message_id, std::string chat_id,
               std::optional<int64_t> author, int64_t author_account_id,
               std::optional<std::string> body, std::optional<std::string> safe_id,
               std::optional<std::string> gif_id, std::optional<std::string> source_language,
               std::optional<std::string> reply_to, std::optional<std::string> reply_excerpt,
               std::optional<std::string> reply_safe, int64_t created_at, int64_t deleted_at,
               std::string deletion_reason, int64_t deleted_by) {
               out.push_back({{"id", id},
                              {"message_id", message_id},
                              {"chat_id", chat_id},
                              {"author_character_id", author.value_or(0)},
                              {"author_account_id", author_account_id},
                              {"body", body.value_or("")},
                              {"safe_message_id", safe_id.value_or("")},
                              {"gif_id", gif_id.value_or("")},
                              {"source_language", source_language.value_or("")},
                              {"reply_to_message_id", reply_to.value_or("")},
                              {"reply_excerpt", reply_excerpt.value_or("")},
                              {"reply_safe_excerpt", reply_safe.value_or("")},
                              {"created_at", created_at},
                              {"deleted_at", deleted_at},
                              {"deletion_reason", deletion_reason},
                              {"deleted_by_account_id", deleted_by}});
           };
    return out;
}

}  // namespace visibility
