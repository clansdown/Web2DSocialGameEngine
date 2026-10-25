#pragma once

// Viewer-relative content projection and minor-safety policy.
//
// The invariant implemented here is:
//   * a minor viewer receives only server-owned safe content;
//   * a minor author may submit only safe catalog ids;
//   * custom content never reaches a minor, and hidden custom messages never
//     affect unread counts, previews, mentions, or push.
//
// Handlers must call these helpers instead of serializing player-authored
// fields directly. Keep the policy here (not in HTTP/WS handlers).

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class GameConfigCache;

namespace visibility {

using json = nlohmann::json;

// ── Viewer context ──────────────────────────────────────────────────────────

struct viewer_context {
    int64_t account_id = 0;
    int64_t character_id = 0;
    bool is_adult = false;

    bool is_minor() const { return !is_adult; }
};

viewer_context load_viewer_context(const std::string& username);
viewer_context load_viewer_context_for_character(int64_t character_id);
bool account_is_adult(int64_t account_id);
bool character_is_adult(int64_t character_id);

// ── Safe catalogs ───────────────────────────────────────────────────────────

enum class safe_catalog_kind {
    message,
    barony_name,
    barony_tag,
    reaction,
    gif,
    ban_reason,
};

std::vector<json> safe_catalog(GameConfigCache& config_cache, safe_catalog_kind kind);
std::optional<json> find_safe_entry(GameConfigCache& config_cache,
                                    safe_catalog_kind kind, const std::string& id);
bool is_valid_safe_id(GameConfigCache& config_cache, safe_catalog_kind kind,
                      const std::string& id);
bool is_minor_safe(GameConfigCache& config_cache, safe_catalog_kind kind,
                   const std::string& id);

// A safe message's server-owned body. Empty when the id is not in the catalog.
std::string safe_message_body(GameConfigCache& config_cache, const std::string& id);
// A safe barony name/tag label. Empty when unknown.
std::string safe_label(GameConfigCache& config_cache, safe_catalog_kind kind,
                       const std::string& id);

// ── Character identity ──────────────────────────────────────────────────────

struct character_identity {
    int64_t character_id = 0;
    int64_t account_id = 0;
    std::string safe_display_name;
    std::string custom_display_name;  // legacy DB column `display_name`
    std::string safe_avatar_id;
    std::string custom_avatar_id;
    std::string avatar_storage = "local";
    int64_t avatar_changed_at = 0;
    int level = 1;
    std::string archetype;
    std::string sex;
    bool owner_is_adult = false;
};

std::optional<character_identity> load_character_identity(int64_t character_id);

// Cross-player projection: never contains a custom avatar key/storage.
json project_character_identity(const viewer_context& viewer,
                                const character_identity& identity);

// Self projection: includes safe-picker data and a boolean `has_custom_avatar`
// (never the key itself).
json project_character_identity_for_self(const viewer_context& viewer,
                                         const character_identity& identity);

// ── Baronies ────────────────────────────────────────────────────────────────

struct barony_identity {
    int64_t id = 0;
    std::string safe_name_id;
    std::vector<std::string> safe_tag_ids;
    std::string custom_name;
    std::string custom_description;
    bool accepts_minors = false;
    int member_count = 0;
};

std::string barony_display_name(GameConfigCache& config_cache,
                                const viewer_context& viewer,
                                const barony_identity& barony);
std::string barony_display_description(GameConfigCache& config_cache,
                                       const viewer_context& viewer,
                                       const barony_identity& barony);
json project_barony(GameConfigCache& config_cache, const viewer_context& viewer,
                    const barony_identity& barony);
std::vector<json> barony_tag_labels(GameConfigCache& config_cache,
                                    const viewer_context& viewer,
                                    const barony_identity& barony);

// ── Authoring / access policy ───────────────────────────────────────────────

bool can_author_custom_content(int64_t character_id);
bool can_use_voice(int64_t character_id);
bool can_create_group(int64_t character_id);
bool can_join_group(int64_t character_id);

// ── Adult status / demotion ─────────────────────────────────────────────────

// Admin/CS entry point. Setting adult=false scrubs the account and deletes all
// messages authored by every character on the account (forensic copy retained).
void admin_set_account_adult_status(GameConfigCache& config_cache, int64_t account_id,
                                    bool adult, int64_t acting_admin_account_id,
                                    const std::string& method,
                                    const std::string& reason);
void demote_account_to_minor(GameConfigCache& config_cache, int64_t account_id,
                             int64_t acting_admin_account_id,
                             const std::string& reason);

json list_deleted_messages(GameConfigCache& config_cache,
                           const viewer_context& admin_viewer,
                           int64_t limit);

}  // namespace visibility
