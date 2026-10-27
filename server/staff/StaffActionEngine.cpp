#include "staff/StaffActionEngine.hpp"

#include "registration/InvitationStore.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "Database.hpp"
#include "MonthlyPass.hpp"
#include "Visibility.hpp"
#include "chat/ChatDB.hpp"
#include "staff/StaffAuth.hpp"
#include "staff/StaffStore.hpp"

namespace staff {

namespace {

GameConfigCache* g_config = nullptr;

json read_account_state(int64_t account_id, bool& found) {
    auto& db = Database::getInstance().gameDB();
    json state = json::object();
    found = false;
    db << "SELECT adult, adult_verified_at, COALESCE(adult_verified_by, 0), "
          "COALESCE(adult_verification_method, ''), social_restricted, support_locked, "
          "restriction_updated_at FROM users WHERE id = ?;"
       << account_id
       >> [&](int adult, int64_t verified_at, int64_t verified_by, std::string method,
              int social, int support, int64_t restriction_updated_at) {
              found = true;
              state = {{"adult", adult != 0},
                       {"adult_verified_at", verified_at},
                       {"adult_verified_by", verified_by},
                       {"adult_verification_method", method},
                       {"social_restricted", social != 0},
                       {"support_locked", support != 0},
                       {"restriction_updated_at", restriction_updated_at}};
          };
    return state;
}

bool character_exists(int64_t character_id) {
    auto& db = Database::getInstance().gameDB();
    bool found = false;
    db << "SELECT 1 FROM characters WHERE id = ? LIMIT 1;" << character_id
       >> [&](int) { found = true; };
    return found;
}

json read_monthly_pass_state(int64_t character_id) {
    auto& db = Database::getInstance().gameDB();
    const monthly_pass::status current =
        monthly_pass::get_status(db, character_id, now_seconds());
    return {
        {"character_id", character_id},
        {"active", current.active},
        {"permanent", current.permanent},
        {"expires_at", current.expires_at},
    };
}

monthly_pass::status monthly_pass_status_from_json(const json& state) {
    monthly_pass::status value;
    value.active = state.value("active", false);
    value.permanent = state.value("permanent", false);
    value.expires_at = state.value("expires_at", static_cast<int64_t>(0));
    return value;
}

json read_identity_state(int64_t character_id, bool& found) {
    auto& db = Database::getInstance().gameDB();
    json state = json::object();
    found = false;
    db << "SELECT display_name, safe_display_name, COALESCE(avatar_safe_id, ''), "
          "COALESCE(avatar_custom_id, ''), COALESCE(avatar_storage, 'local'), "
          "avatar_changed_at, name_locked, avatar_locked, "
          "COALESCE(identity_locked_by, 0), COALESCE(identity_locked_at, 0) "
          "FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::string display_name, std::string safe_display_name, std::string safe_avatar,
              std::string custom_avatar, std::string storage, int64_t changed_at, int name_locked,
              int avatar_locked, int64_t locked_by, int64_t locked_at) {
              found = true;
              state = {{"character_id", character_id},
                       {"display_name", display_name},
                       {"safe_display_name", safe_display_name},
                       {"avatar_safe_id", safe_avatar},
                       {"avatar_custom_id", custom_avatar},
                       {"avatar_storage", storage},
                       {"avatar_changed_at", changed_at},
                       {"name_locked", name_locked != 0},
                       {"avatar_locked", avatar_locked != 0},
                       {"identity_locked_by", locked_by},
                       {"identity_locked_at", locked_at}};
          };
    return state;
}

json read_barony_state(int64_t barony_id, bool& found) {
    auto& db = Database::getInstance().gameDB();
    json state = json::object();
    found = false;
    db << "SELECT name, COALESCE(safe_name_id, ''), COALESCE(safe_tag_ids, '[]'), "
          "COALESCE(description, ''), accepts_minors, owner_character_id, "
          "COALESCE(baron_character_id, 0) FROM baronies WHERE id = ?;"
       << barony_id
       >> [&](std::string name, std::string safe_name_id, std::string safe_tag_ids,
              std::string description, int accepts_minors, int64_t owner, int64_t baron) {
              found = true;
              state = {{"barony_id", barony_id},
                       {"name", name},
                       {"safe_name_id", safe_name_id},
                       {"safe_tag_ids", safe_tag_ids},
                       {"description", description},
                       {"accepts_minors", accepts_minors != 0},
                       {"owner_character_id", owner},
                       {"baron_character_id", baron}};
          };
    return state;
}

json read_session_rows(int64_t character_id, const std::string& table) {
    auto& db = Database::getInstance().gameDB();
    json rows = json::array();
    if (table == "game_sessions") {
        db << "SELECT id, mini_game, level_id, started_at, last_activity, total_rounds, "
              "current_round, state, COALESCE(current_spawn_schedule, ''), placements "
              "FROM game_sessions WHERE character_id = ? ORDER BY id;"
           << character_id
           >> [&](int64_t id, std::string mini_game, int level_id, int64_t started_at,
                  int64_t last_activity, int total_rounds, int current_round, std::string state,
                  std::string schedule, std::string placements) {
                  rows.push_back({{"id", id},
                                  {"mini_game", mini_game},
                                  {"level_id", level_id},
                                  {"started_at", started_at},
                                  {"last_activity", last_activity},
                                  {"total_rounds", total_rounds},
                                  {"current_round", current_round},
                                  {"state", state},
                                  {"current_spawn_schedule", schedule},
                                  {"placements", placements}});
              };
    } else if (table == "weeding_sessions") {
        db << "SELECT id, level_id, started_at, last_activity, state, session_json "
              "FROM weeding_sessions WHERE character_id = ? ORDER BY id;"
           << character_id
           >> [&](int64_t id, int level_id, int64_t started_at, int64_t last_activity,
                  std::string state, std::string session_json) {
                  rows.push_back({{"id", id},
                                  {"level_id", level_id},
                                  {"started_at", started_at},
                                  {"last_activity", last_activity},
                                  {"state", state},
                                  {"session_json", session_json}});
              };
    }
    return rows;
}

json read_progress_rows(int64_t character_id, const std::string& mini_game) {
    auto& db = Database::getInstance().gameDB();
    json rows = json::array();
    if (mini_game.empty()) {
        db << "SELECT mini_game, level_id, completed, COALESCE(best_score, 0), "
              "COALESCE(times_played, 0), COALESCE(last_played, 0) FROM mini_game_progress "
              "WHERE character_id = ? ORDER BY mini_game, level_id;"
           << character_id
           >> [&](std::string game, int level_id, int completed, int best_score,
                  int times_played, int64_t last_played) {
                  rows.push_back({{"mini_game", game},
                                  {"level_id", level_id},
                                  {"completed", completed != 0},
                                  {"best_score", best_score},
                                  {"times_played", times_played},
                                  {"last_played", last_played}});
              };
    } else {
        db << "SELECT level_id, completed, COALESCE(best_score, 0), COALESCE(times_played, 0), "
              "COALESCE(last_played, 0) FROM mini_game_progress "
              "WHERE character_id = ? AND mini_game = ? ORDER BY level_id;"
           << character_id << mini_game
           >> [&](int level_id, int completed, int best_score, int times_played,
                  int64_t last_played) {
                  rows.push_back({{"mini_game", mini_game},
                                  {"level_id", level_id},
                                  {"completed", completed != 0},
                                  {"best_score", best_score},
                                  {"times_played", times_played},
                                  {"last_played", last_played}});
              };
    }
    return rows;
}

json read_phase_state(int64_t character_id, bool& found) {
    auto& db = Database::getInstance().gameDB();
    json state = json::object();
    found = false;
    db << "SELECT game_phase, COALESCE(current_mini_game, ''), COALESCE(current_level_id, 0), "
          "base_unlocked, entered_at, last_updated, COALESCE(honor_name, ''), "
          "COALESCE(land_patent_acknowledged, 0) FROM player_game_state WHERE character_id = ?;"
       << character_id
       >> [&](std::string phase, std::string current_game, int current_level, int base_unlocked,
              int64_t entered_at, int64_t last_updated, std::string honor_name, int ack) {
              found = true;
              state = {{"character_id", character_id},
                       {"game_phase", phase},
                       {"current_mini_game", current_game},
                       {"current_level_id", current_level},
                       {"base_unlocked", base_unlocked != 0},
                       {"entered_at", entered_at},
                       {"last_updated", last_updated},
                       {"honor_name", honor_name},
                       {"land_patent_acknowledged", ack != 0}};
          };
    return state;
}

json read_building_row(int64_t building_id, bool& found) {
    auto& db = Database::getInstance().gameDB();
    json row = json::object();
    found = false;
    db << "SELECT id, fiefdom_id, name, level, x, y, construction_start_ts, last_updated, "
          "action_start_ts, action_tag, output_rates, tech_xp, tech_nodes, forge_order, "
          "training, pond_type FROM fiefdom_buildings WHERE id = ?;"
       << building_id
       >> [&](int64_t id, int64_t fiefdom_id, std::string name, int level, int x, int y,
              int64_t construction_start_ts, int64_t last_updated, int64_t action_start_ts,
              std::string action_tag, std::string output_rates, double tech_xp,
              std::string tech_nodes, std::string forge_order, std::string training,
              std::string pond_type) {
              found = true;
              row = {{"id", id},
                     {"fiefdom_id", fiefdom_id},
                     {"name", name},
                     {"level", level},
                     {"x", x},
                     {"y", y},
                     {"construction_start_ts", construction_start_ts},
                     {"last_updated", last_updated},
                     {"action_start_ts", action_start_ts},
                     {"action_tag", action_tag},
                     {"output_rates", output_rates},
                     {"tech_xp", tech_xp},
                     {"tech_nodes", tech_nodes},
                     {"forge_order", forge_order},
                     {"training", training},
                     {"pond_type", pond_type}};
          };
    return row;
}

bool restore_building_row(const json& row) {
    auto& db = Database::getInstance().gameDB();
    db << "INSERT OR REPLACE INTO fiefdom_buildings (id, fiefdom_id, name, level, x, y, "
          "construction_start_ts, last_updated, action_start_ts, action_tag, output_rates, "
          "tech_xp, tech_nodes, forge_order, training, pond_type) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"
       << row.value("id", static_cast<int64_t>(0))
       << row.value("fiefdom_id", static_cast<int64_t>(0))
       << row.value("name", std::string()) << row.value("level", 0)
       << row.value("x", 0) << row.value("y", 0)
       << row.value("construction_start_ts", static_cast<int64_t>(0))
       << row.value("last_updated", static_cast<int64_t>(0))
       << row.value("action_start_ts", static_cast<int64_t>(0))
       << row.value("action_tag", std::string())
       << row.value("output_rates", std::string("{}"))
       << row.value("tech_xp", 0.0) << row.value("tech_nodes", std::string("[]"))
       << row.value("forge_order", std::string()) << row.value("training", std::string())
       << row.value("pond_type", std::string());
    return true;
}

bool restore_progress_rows(int64_t character_id, const json& rows) {
    auto& db = Database::getInstance().gameDB();
    std::set<std::string> games;
    for (const auto& row : rows) {
        if (row.contains("mini_game")) games.insert(row.value("mini_game", std::string()));
    }
    for (const auto& game : games) {
        db << "DELETE FROM mini_game_progress WHERE character_id = ? AND mini_game = ?;"
           << character_id << game;
    }
    for (const auto& row : rows) {
        const std::string game = row.value("mini_game", std::string());
        if (game.empty()) continue;
        db << "INSERT INTO mini_game_progress (character_id, mini_game, level_id, completed, "
              "best_score, times_played, last_played) VALUES (?, ?, ?, ?, ?, ?, ?);"
           << character_id << game << row.value("level_id", 0)
           << (row.value("completed", false) ? 1 : 0)
           << row.value("best_score", 0) << row.value("times_played", 0)
           << row.value("last_played", static_cast<int64_t>(0));
    }
    return true;
}

std::string capability_for_action(const std::string& action_type) {
    if (action_type == "staff.set_adult_status") return "account.set_adult_status";
    if (action_type == "staff.set_social_restriction") return "account.set_social_restriction";
    if (action_type == "staff.set_support_lock") return "account.set_support_lock";
    if (action_type == "staff.grant_monthly_pass" ||
        action_type == "staff.revoke_monthly_pass") {
        return "character.manage_monthly_pass";
    }
    if (action_type == "staff.set_name_lock" || action_type == "staff.lock_identity") {
        return "identity.set_name_lock";
    }
    if (action_type == "staff.set_avatar_lock") return "identity.set_avatar_lock";
    if (action_type == "staff.revert_name") return "identity.revert_name";
    if (action_type == "staff.revert_avatar") return "identity.revert_avatar";
    if (action_type == "staff.remove_custom_avatar") return "identity.remove_custom_avatar";
    if (action_type == "staff.ban_avatar_hash" || action_type == "staff.unban_avatar_hash") {
        return "avatar.ban_hash";
    }
    if (action_type == "staff.set_barony_safe_identity") return "barony.set_safe_identity";
    if (action_type == "staff.set_barony_accepts_minors") return "barony.set_accepts_minors";
    if (action_type == "staff.reassign_barony_owner") return "barony.reassign_owner";
    if (action_type == "support.unstick_session") return "support.unstick_session";
    if (action_type == "support.set_game_phase") return "support.set_game_phase";
    if (action_type == "support.set_march_progress") return "support.set_march_progress";
    if (action_type == "support.cancel_construction") return "support.cancel_construction";
    if (action_type == "support.adjust_fiefdom_resource") {
        return "support.adjust_fiefdom_resource";
    }
    if (action_type == "registration.invite_generate") {
        return "registration.invites.generate";
    }
    if (action_type == "registration.invite_revoke") {
        return "registration.invites.revoke";
    }
    return std::string();
}

std::string character_safe_name(int64_t character_id) {
    auto& db = Database::getInstance().gameDB();
    std::string name;
    db << "SELECT safe_display_name FROM characters WHERE id = ?;" << character_id
       >> [&](std::string value) { name = std::move(value); };
    return name;
}

json notification_args(int64_t character_id, const std::string& reason,
                       const std::string& extra = std::string()) {
    json args = {{"character_name", character_safe_name(character_id)},
                 {"reason", reason}};
    if (!extra.empty()) args["detail"] = extra;
    return args;
}

void emit_action_notification(const staff_action_journal_entry& entry) {
    const std::string reason = entry.reason;
    if (entry.action_type == "staff.set_adult_status") {
        notify_account(entry.target_type == "account" ? std::stoll(entry.target_id) : 0,
                       "moderation_action", "ui_notification_adult_status",
                       {{"adult", entry.after_state.value("adult", false)},
                        {"reason", reason}},
                       std::string(), entry.id);
    } else if (entry.action_type == "staff.set_social_restriction") {
        notify_account(std::stoll(entry.target_id), "social_restriction",
                       "ui_notification_social_restriction",
                       {{"restricted", entry.after_state.value("social_restricted", false)},
                        {"reason", reason}},
                       "#/activity/support", entry.id);
    } else if (entry.action_type == "staff.set_support_lock") {
        notify_account(std::stoll(entry.target_id), "support_lock",
                       "ui_notification_support_lock",
                       {{"locked", entry.after_state.value("support_locked", false)},
                        {"reason", reason}},
                       "#/activity/support", entry.id);
    } else if (entry.action_type == "staff.set_name_lock" ||
               entry.action_type == "staff.revert_name") {
        notify_character(std::stoll(entry.target_id), "moderation_action",
                         "ui_notification_name_changed",
                         notification_args(std::stoll(entry.target_id), reason),
                         "#/activity/support", entry.id);
    } else if (entry.action_type == "staff.set_avatar_lock" ||
               entry.action_type == "staff.revert_avatar" ||
               entry.action_type == "staff.remove_custom_avatar") {
        notify_character(std::stoll(entry.target_id), "moderation_action",
                         "ui_notification_avatar_changed",
                         notification_args(std::stoll(entry.target_id), reason),
                         "#/activity/support", entry.id);
    } else if (entry.action_type == "staff.set_barony_safe_identity" ||
               entry.action_type == "staff.set_barony_accepts_minors" ||
               entry.action_type == "staff.reassign_barony_owner") {
        const int64_t owner = entry.after_state.value(
            "owner_character_id", static_cast<int64_t>(0));
        if (owner > 0) {
            notify_character(owner, "moderation_action", "ui_notification_barony_changed",
                             notification_args(owner, reason), "#/activity/barony", entry.id);
        }
    } else if (entry.action_type == "support.set_game_phase" ||
               entry.action_type == "support.unstick_session" ||
               entry.action_type == "support.set_march_progress" ||
               entry.action_type == "support.cancel_construction" ||
               entry.action_type == "support.adjust_fiefdom_resource") {
        notify_character(std::stoll(entry.target_id), "staff_notice",
                         "ui_notification_staff_notice",
                         notification_args(std::stoll(entry.target_id), reason,
                                           entry.action_type),
                         std::string(), entry.id);
    }
}

bool current_matches_after(const std::string& action_type, const json& after_state,
                           staff_preflight_result& result) {
    if (action_type.rfind("staff.set_", 0) == 0 ||
        action_type == "staff.revert_name" || action_type == "staff.revert_avatar" ||
        action_type == "staff.remove_custom_avatar") {
        if (action_type.find("account") != std::string::npos ||
            action_type == "staff.set_adult_status" ||
            action_type == "staff.set_social_restriction" ||
            action_type == "staff.set_support_lock") {
            return true;  // handled by caller with the right key
        }
    }
    (void)after_state;
    (void)result;
    return true;
}

}  // namespace

void StaffActionEngine::initialize(GameConfigCache* config_cache) {
    g_config = config_cache;
}

StaffActionEngine& StaffActionEngine::instance() {
    static StaffActionEngine engine;
    return engine;
}

void StaffActionEngine::recover_interrupted_actions() {
    StaffStore::instance().recover_interrupted_actions();
    if (g_config) {
        // No-op; kept so initialization has one entry point for future
        // recovery hooks that need config.
    }
}

staff_action_result StaffActionEngine::execute(const staff_context& context,
                                               const staff_action_request& request) {
    staff_action_result result;
    const std::string capability = capability_for_action(request.action_type);
    if (capability.empty()) {
        result.error = "unknown_action";
        return result;
    }
    if (!context.is_staff()) {
        result.error = "not_staff";
        return result;
    }
    if (!context.has(capability)) {
        result.error = "missing_capability:" + capability;
        return result;
    }

    auto& store = StaffStore::instance();
    staff_action_journal_entry entry;
    entry.actor_account_id = context.account_id;
    entry.actor_type = "staff";
    entry.authority = "staff_capability";
    entry.capability = capability;
    entry.action_type = request.action_type;
    entry.action_version = request.version;
    entry.target_type = request.target_type;
    entry.target_id = request.target_id;
    entry.scope = request.scope;
    entry.reason = request.reason;
    entry.status = "pending";

    auto fail = [&](const std::string& error) {
        result.ok = false;
        result.error = error;
        return result;
    };

    // Capture before-state.
    auto& game_db = Database::getInstance().gameDB();
    if (request.action_type == "staff.set_adult_status" ||
        request.action_type == "staff.set_social_restriction" ||
        request.action_type == "staff.set_support_lock") {
        bool found = false;
        entry.before_state = read_account_state(std::stoll(request.target_id), found);
        if (!found) return fail("account_not_found");
    } else if (request.action_type == "staff.grant_monthly_pass" ||
               request.action_type == "staff.revoke_monthly_pass") {
        const int64_t character_id = std::stoll(request.target_id);
        if (!character_exists(character_id)) return fail("character_not_found");
        entry.before_state = read_monthly_pass_state(character_id);
    } else if (request.action_type.rfind("staff.set_name_lock", 0) == 0 ||
               request.action_type.rfind("staff.set_avatar_lock", 0) == 0 ||
               request.action_type == "staff.lock_identity" ||
               request.action_type == "staff.revert_name" ||
               request.action_type == "staff.revert_avatar" ||
               request.action_type == "staff.remove_custom_avatar") {
        bool found = false;
        entry.before_state = read_identity_state(std::stoll(request.target_id), found);
        if (!found) return fail("character_not_found");
    } else if (request.action_type == "staff.ban_avatar_hash" ||
               request.action_type == "staff.unban_avatar_hash") {
        const std::string sha = request.parameters.value("sha256", std::string());
        if (sha.size() != 64) return fail("invalid_sha256");
        bool active = false;
        auto& sdb = Database::getInstance().staffDB();
        sdb << "SELECT 1 FROM banned_avatar_hashes WHERE sha256 = ? AND revoked_at IS NULL "
               "LIMIT 1;" << sha >> [&](int) { active = true; };
        entry.before_state = {{"sha256", sha}, {"active", active}};
    } else if (request.action_type.rfind("staff.set_barony_", 0) == 0 ||
               request.action_type == "staff.reassign_barony_owner") {
        bool found = false;
        entry.before_state = read_barony_state(std::stoll(request.target_id), found);
        if (!found) return fail("barony_not_found");
    } else if (request.action_type == "support.unstick_session") {
        const int64_t character_id = std::stoll(request.target_id);
        entry.before_state = {{"character_id", character_id},
                              {"game_sessions", read_session_rows(character_id, "game_sessions")},
                              {"weeding_sessions", read_session_rows(character_id, "weeding_sessions")}};
    } else if (request.action_type == "support.set_game_phase") {
        bool found = false;
        entry.before_state = read_phase_state(std::stoll(request.target_id), found);
        if (!found) return fail("character_not_found");
    } else if (request.action_type == "support.set_march_progress") {
        const int64_t character_id = std::stoll(request.target_id);
        const std::string mini_game = request.parameters.value("mini_game", std::string());
        entry.before_state = {{"character_id", character_id},
                              {"mini_game", mini_game},
                              {"rows", read_progress_rows(character_id, mini_game)}};
    } else if (request.action_type == "support.cancel_construction") {
        bool found = false;
        entry.before_state = read_building_row(std::stoll(request.target_id), found);
        if (!found) return fail("building_not_found");
    } else if (request.action_type == "support.adjust_fiefdom_resource") {
        const std::string resource = request.parameters.value("resource", std::string());
        const std::set<std::string> resources = {
            "gold", "silver_pence", "grain", "wood", "steel", "bronze", "leather", "mana",
            "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"};
        if (!resources.count(resource)) return fail("invalid_resource");
        bool found = false;
        double value = 0.0;
        game_db << "SELECT " + resource + " FROM fiefdoms WHERE id = ?;"
                << std::stoll(request.target_id)
                >> [&](double v) { found = true; value = v; };
        if (!found) return fail("fiefdom_not_found");
        entry.before_state = {{"fiefdom_id", std::stoll(request.target_id)},
                              {"resource", resource}, {"value", value}};
    } else if (request.action_type == "registration.invite_generate") {
        const std::string code = request.parameters.value("code", std::string());
        const std::string canonical = canonicalize_invitation_code(code);
        if (canonical.empty() || canonical != code) {
            return fail("invalid_invitation_code");
        }
        entry.before_state = {{"code_id", request.target_id}, {"exists", false}};
    } else if (request.action_type == "registration.invite_revoke") {
        const std::string code_id = request.parameters.value("code_id", std::string());
        if (code_id.empty() || code_id != request.target_id) {
            return fail("invalid_invitation_code");
        }
        const auto existing = InvitationStore::instance().find_by_id(code_id);
        if (!existing) return fail("invitation_code_not_found");
        if (existing->used_at > 0) return fail("invitation_code_used");
        if (existing->revoked_at > 0) return fail("invitation_code_already_revoked");
        entry.before_state = {{"code_id", code_id}, {"revoked_at", 0}};
    }

    std::string append_error;
    const std::string action_id = store.append_action(entry, append_error);
    if (action_id.empty()) return fail(append_error.empty() ? "journal_write_failed" : append_error);
    result.action_id = action_id;

    json after_state = json::object();
    std::string apply_error;

    try {
    if (request.action_type == "staff.set_adult_status") {
        if (!g_config) {
            store.update_action_status(action_id, "failed", "config_unavailable");
            return fail("config_unavailable");
        }
        const int64_t account_id = std::stoll(request.target_id);
        const bool adult = request.parameters.value("adult", false);
        const std::string method = request.parameters.value("method", "customer_service");
        try {
            visibility::admin_set_account_adult_status(*g_config, account_id, adult,
                                                       context.account_id, method, request.reason);
        } catch (const std::exception& e) {
            apply_error = e.what();
        }
        if (apply_error.empty()) {
            bool found = false;
            after_state = read_account_state(account_id, found);
            entry.undo.reversible = false;
        }
    } else if (request.action_type == "staff.set_social_restriction") {
        const int64_t account_id = std::stoll(request.target_id);
        const bool restricted = request.parameters.value("restricted", false);
        game_db << "UPDATE users SET social_restricted = ?, restriction_updated_at = ? WHERE id = ?;"
                << (restricted ? 1 : 0) << now_seconds() << account_id;
        bool found = false;
        after_state = read_account_state(account_id, found);
        entry.undo.reversible = true;
    } else if (request.action_type == "staff.set_support_lock") {
        const int64_t account_id = std::stoll(request.target_id);
        const bool locked = request.parameters.value("locked", false);
        game_db << "UPDATE users SET support_locked = ?, restriction_updated_at = ? WHERE id = ?;"
                << (locked ? 1 : 0) << now_seconds() << account_id;
        bool found = false;
        after_state = read_account_state(account_id, found);
        entry.undo.reversible = true;
    } else if (request.action_type == "staff.grant_monthly_pass") {
        const int64_t character_id = std::stoll(request.target_id);
        const bool permanent = request.parameters.value("permanent", false);
        const int months = request.parameters.value("months", 0);
        const int64_t now = now_seconds();

        monthly_pass::grant_origin origin;
        origin.source = "staff_grant";
        origin.reason = request.reason;
        origin.staff_account_id = context.account_id;
        origin.reference_id = action_id;

        if (entry.before_state.value("permanent", false)) {
            apply_error = "already_permanent";
        } else if (permanent) {
            monthly_pass::grant_permanent(game_db, character_id, origin, now);
        } else if (months < 1 || months > monthly_pass::k_max_months) {
            apply_error = "invalid_month_count";
        } else {
            monthly_pass::grant_months(game_db, character_id, months, origin, now);
        }

        if (apply_error.empty()) {
            after_state = read_monthly_pass_state(character_id);
        }
        entry.undo.reversible = true;
    } else if (request.action_type == "staff.revoke_monthly_pass") {
        const int64_t character_id = std::stoll(request.target_id);
        if (!entry.before_state.value("active", false)) {
            apply_error = "pass_not_active";
        } else {
            monthly_pass::grant_origin origin;
            origin.source = "staff_revoke";
            origin.reason = request.reason;
            origin.staff_account_id = context.account_id;
            origin.reference_id = action_id;
            monthly_pass::revoke(game_db, character_id, origin, now_seconds());
            after_state = read_monthly_pass_state(character_id);
        }
        entry.undo.reversible = true;
    } else if (request.action_type == "staff.lock_identity") {
        const int64_t character_id = std::stoll(request.target_id);
        const bool revert_name = request.parameters.value("revert_name", false);
        const bool revert_avatar = request.parameters.value("revert_avatar", false);
        const bool update_locks = request.parameters.contains("lock_name") ||
                                  request.parameters.contains("lock_avatar");
        const bool lock_name = request.parameters.contains("lock_name")
                                   ? request.parameters.value("lock_name", false)
                                   : entry.before_state.value("name_locked", false);
        const bool lock_avatar = request.parameters.contains("lock_avatar")
                                     ? request.parameters.value("lock_avatar", false)
                                     : entry.before_state.value("avatar_locked", false);
        const std::string revert_to = request.parameters.value("revert_to", std::string("safe"));
        if (revert_name) {
            std::string current_name = entry.before_state.value("display_name", std::string());
            std::string safe_name = entry.before_state.value("safe_display_name", std::string());
            std::string prior;
            game_db << "SELECT value FROM character_identity_history WHERE character_id = ? "
                       "AND kind = 'name' ORDER BY changed_at DESC, id DESC LIMIT 1;"
                    << character_id >> prior;
            const std::string new_name =
                (revert_to == "prior" && !prior.empty()) ? prior : safe_name;
            game_db << "INSERT INTO character_identity_history (character_id, kind, value, changed_at) "
                       "VALUES (?, 'name', ?, ?);"
                    << character_id << current_name << now_seconds();
            game_db << "UPDATE characters SET display_name = ? WHERE id = ?;"
                    << (new_name.empty() ? current_name : new_name) << character_id;
        }
        if (revert_avatar) {
            std::string safe_avatar = entry.before_state.value("avatar_safe_id", std::string());
            std::string prior;
            game_db << "SELECT value FROM character_identity_history WHERE character_id = ? "
                       "AND kind = 'avatar' ORDER BY changed_at DESC, id DESC LIMIT 1;"
                    << character_id >> prior;
            const std::string new_avatar =
                (revert_to == "prior" && !prior.empty() && prior != "custom") ? prior
                                                                             : safe_avatar;
            const std::string current_avatar =
                entry.before_state.value("avatar_custom_id", std::string()).empty()
                    ? entry.before_state.value("avatar_safe_id", std::string())
                    : std::string("custom");
            game_db << "INSERT INTO character_identity_history (character_id, kind, value, changed_at) "
                       "VALUES (?, 'avatar', ?, ?);"
                    << character_id << current_avatar << now_seconds();
            game_db << "UPDATE characters SET avatar_safe_id = ?, avatar_custom_id = NULL, "
                       "avatar_storage = 'local', avatar_changed_at = ? WHERE id = ?;"
                    << new_avatar << now_seconds() << character_id;
        }
        if (update_locks) {
            game_db << "UPDATE characters SET name_locked = ?, avatar_locked = ?, "
                       "identity_locked_by = ?, identity_locked_at = ? WHERE id = ?;"
                    << (lock_name ? 1 : 0) << (lock_avatar ? 1 : 0) << context.account_id
                    << now_seconds() << character_id;
        }
        bool found = false;
        after_state = read_identity_state(character_id, found);
        entry.undo.reversible = true;
    } else if (request.action_type == "staff.set_name_lock" ||
               request.action_type == "staff.set_avatar_lock") {
        const int64_t character_id = std::stoll(request.target_id);
        const bool lock_name = request.action_type == "staff.set_name_lock";
        const bool locked = request.parameters.value("locked", false);
        if (lock_name) {
            game_db << "UPDATE characters SET name_locked = ?, identity_locked_by = ?, "
                       "identity_locked_at = ? WHERE id = ?;"
                    << (locked ? 1 : 0) << context.account_id << now_seconds() << character_id;
        } else {
            game_db << "UPDATE characters SET avatar_locked = ?, identity_locked_by = ?, "
                       "identity_locked_at = ? WHERE id = ?;"
                    << (locked ? 1 : 0) << context.account_id << now_seconds() << character_id;
        }
        bool found = false;
        after_state = read_identity_state(character_id, found);
        entry.undo.reversible = true;
    } else if (request.action_type == "staff.revert_name" ||
               request.action_type == "staff.revert_avatar" ||
               request.action_type == "staff.remove_custom_avatar") {
        const int64_t character_id = std::stoll(request.target_id);
        if (request.action_type == "staff.revert_name") {
            std::string prior;
            game_db << "SELECT value FROM character_identity_history WHERE character_id = ? "
                       "AND kind = 'name' ORDER BY changed_at DESC, id DESC LIMIT 1;"
                    << character_id >> prior;
            const std::string current = entry.before_state.value("display_name", std::string());
            const std::string safe_name = entry.before_state.value("safe_display_name", std::string());
            const bool to_prior = request.parameters.value("revert_to", std::string("safe")) == "prior";
            const std::string new_name = (to_prior && !prior.empty()) ? prior : safe_name;
            game_db << "INSERT INTO character_identity_history (character_id, kind, value, changed_at) "
                       "VALUES (?, 'name', ?, ?);"
                    << character_id << current << now_seconds();
            game_db << "UPDATE characters SET display_name = ? WHERE id = ?;"
                    << (new_name.empty() ? current : new_name) << character_id;
        } else if (request.action_type == "staff.revert_avatar") {
            const std::string safe_avatar = entry.before_state.value("avatar_safe_id", std::string());
            const bool to_prior = request.parameters.value("revert_to", std::string("safe")) == "prior";
            std::string prior;
            game_db << "SELECT value FROM character_identity_history WHERE character_id = ? "
                       "AND kind = 'avatar' ORDER BY changed_at DESC, id DESC LIMIT 1;"
                    << character_id >> prior;
            const std::string new_avatar =
                (to_prior && !prior.empty() && prior != "custom") ? prior : safe_avatar;
            game_db << "INSERT INTO character_identity_history (character_id, kind, value, changed_at) "
                       "VALUES (?, 'avatar', ?, ?);"
                    << character_id
                    << entry.before_state.value("avatar_safe_id", std::string()) << now_seconds();
            game_db << "UPDATE characters SET avatar_safe_id = ?, avatar_custom_id = NULL, "
                       "avatar_storage = 'local', avatar_changed_at = ? WHERE id = ?;"
                    << new_avatar << now_seconds() << character_id;
        } else {
            game_db << "UPDATE characters SET avatar_custom_id = NULL, "
                       "avatar_storage = 'local', avatar_changed_at = ? WHERE id = ?;"
                    << now_seconds() << character_id;
        }
        bool found = false;
        after_state = read_identity_state(character_id, found);
        entry.undo.reversible = true;
    } else if (request.action_type == "staff.ban_avatar_hash") {
        const std::string sha = request.parameters.value("sha256", std::string());
        std::string err;
        if (!store.ban_hash(sha, request.reason, context.account_id, action_id, err)) {
            apply_error = err;
        } else {
            auto& sdb = Database::getInstance().staffDB();
            sdb << "UPDATE banned_avatar_hashes SET action_id = ? WHERE sha256 = ?;"
                << action_id << sha;
            after_state = {{"sha256", sha}, {"active", true}};
            entry.undo.reversible = true;
        }
    } else if (request.action_type == "staff.unban_avatar_hash") {
        const std::string sha = request.parameters.value("sha256", std::string());
        std::string err;
        if (!store.unban_hash(sha, context.account_id, action_id, err)) {
            apply_error = err;
        } else {
            after_state = {{"sha256", sha}, {"active", false}};
            entry.undo.reversible = true;
        }
    } else if (request.action_type == "staff.set_barony_safe_identity") {
        const int64_t barony_id = std::stoll(request.target_id);
        const std::string safe_name_id = request.parameters.value("safe_name_id", std::string());
        const json tags = request.parameters.value("safe_tag_ids", json::array());
        const std::string custom_name = request.parameters.value("name", std::string());
        const std::string safe_label = request.parameters.value("safe_label", std::string());
        const std::string name = custom_name.empty() ? safe_label : custom_name;
        if (name.empty()) {
            apply_error = "name_required";
        } else {
            game_db << "UPDATE baronies SET safe_name_id = ?, safe_tag_ids = ?, name = ?, "
                       "description = ? WHERE id = ?;"
                    << safe_name_id << tags.dump() << name
                    << request.parameters.value("description", std::string()) << barony_id;
            bool found = false;
            after_state = read_barony_state(barony_id, found);
            entry.undo.reversible = true;
        }
    } else if (request.action_type == "staff.set_barony_accepts_minors") {
        const int64_t barony_id = std::stoll(request.target_id);
        const bool accepts = request.parameters.value("accepts_minors", false);
        if (!accepts) {
            std::vector<int64_t> members;
            game_db << "SELECT character_id FROM barony_members WHERE barony_id = ?;"
                    << barony_id >> [&](int64_t id) { members.push_back(id); };
            for (int64_t member : members) {
                if (!visibility::character_is_adult(member)) {
                    apply_error = "barony_has_minor_members";
                    break;
                }
            }
        }
        if (apply_error.empty()) {
            game_db << "UPDATE baronies SET accepts_minors = ? WHERE id = ?;"
                    << (accepts ? 1 : 0) << barony_id;
            bool found = false;
            after_state = read_barony_state(barony_id, found);
            entry.undo.reversible = true;
        }
    } else if (request.action_type == "staff.reassign_barony_owner") {
        const int64_t barony_id = std::stoll(request.target_id);
        const int64_t new_owner = request.parameters.value("owner_character_id", static_cast<int64_t>(0));
        bool member = false;
        game_db << "SELECT 1 FROM barony_members WHERE barony_id = ? AND character_id = ? LIMIT 1;"
                << barony_id << new_owner >> [&](int) { member = true; };
        if (new_owner <= 0 || !member) {
            apply_error = "owner_must_be_member";
        } else {
            game_db << "UPDATE baronies SET owner_character_id = ?, baron_character_id = ? WHERE id = ?;"
                    << new_owner << new_owner << barony_id;
            game_db << "UPDATE barony_members SET role = 'member' WHERE barony_id = ?;"
                    << barony_id;
            game_db << "UPDATE barony_members SET role = 'owner' WHERE barony_id = ? AND character_id = ?;"
                    << barony_id << new_owner;
            bool found = false;
            after_state = read_barony_state(barony_id, found);
            // Membership roles are not part of the barony snapshot; do not offer
            // a partial undo that would restore the owner but not the roles.
            entry.undo.reversible = false;
        }
    } else if (request.action_type == "support.unstick_session") {
        const int64_t character_id = std::stoll(request.target_id);
        game_db << "UPDATE game_sessions SET state = 'abandoned', last_activity = ? "
                   "WHERE character_id = ? AND state = 'active';"
                << now_seconds() << character_id;
        game_db << "UPDATE weeding_sessions SET state = 'abandoned', last_activity = ? "
                   "WHERE character_id = ? AND state = 'active';"
                << now_seconds() << character_id;
        after_state = {{"character_id", character_id},
                       {"game_sessions", read_session_rows(character_id, "game_sessions")},
                       {"weeding_sessions", read_session_rows(character_id, "weeding_sessions")}};
        entry.undo.reversible = true;
    } else if (request.action_type == "support.set_game_phase") {
        const int64_t character_id = std::stoll(request.target_id);
        const std::string phase = request.parameters.value("game_phase", std::string());
        if (phase.empty()) {
            apply_error = "game_phase_required";
        } else {
            game_db << "INSERT INTO player_game_state (character_id, game_phase, last_updated) "
                       "VALUES (?, ?, ?) ON CONFLICT(character_id) DO UPDATE SET "
                       "game_phase = excluded.game_phase, last_updated = excluded.last_updated;"
                    << character_id << phase << now_seconds();
            bool found = false;
            after_state = read_phase_state(character_id, found);
            entry.undo.reversible = true;
        }
    } else if (request.action_type == "support.set_march_progress") {
        const int64_t character_id = std::stoll(request.target_id);
        const std::string mini_game = request.parameters.value("mini_game", std::string());
        const int next_level = request.parameters.value("next_level", 0);
        if (mini_game.empty() || next_level < 1) {
            apply_error = "mini_game_and_next_level_required";
        } else {
            for (int level = 1; level < next_level; ++level) {
                game_db << "INSERT INTO mini_game_progress (character_id, mini_game, level_id, "
                           "completed, times_played, last_played) VALUES (?, ?, ?, 1, 0, ?) "
                           "ON CONFLICT(character_id, mini_game, level_id) DO UPDATE SET completed = 1;"
                        << character_id << mini_game << level << now_seconds();
            }
            game_db << "UPDATE mini_game_progress SET completed = 0 "
                       "WHERE character_id = ? AND mini_game = ? AND level_id >= ?;"
                    << character_id << mini_game << next_level;
            after_state = {{"character_id", character_id},
                           {"mini_game", mini_game},
                           {"rows", read_progress_rows(character_id, mini_game)}};
            entry.undo.reversible = true;
        }
    } else if (request.action_type == "support.cancel_construction") {
        const int64_t building_id = std::stoll(request.target_id);
        if (entry.before_state.value("construction_start_ts", static_cast<int64_t>(0)) <= 0) {
            apply_error = "building_not_under_construction";
        } else {
            game_db << "DELETE FROM fiefdom_buildings WHERE id = ?;" << building_id;
            after_state = {{"building_id", building_id}, {"deleted", true}};
            entry.undo.reversible = true;
        }
    } else if (request.action_type == "support.adjust_fiefdom_resource") {
        const std::string resource = request.parameters.value("resource", std::string());
        const double value = request.parameters.value("value", 0.0);
        const int64_t fiefdom_id = std::stoll(request.target_id);
        if (value < 0.0) {
            apply_error = "negative_resource_value";
        } else {
            game_db << "UPDATE fiefdoms SET " + resource + " = ? WHERE id = ?;"
                    << value << fiefdom_id;
            after_state = {{"fiefdom_id", fiefdom_id}, {"resource", resource}, {"value", value}};
            entry.undo.reversible = true;
        }
    } else if (request.action_type == "registration.invite_generate") {
        const std::string code = request.parameters.value("code", std::string());
        const invitation_code_draft draft{request.target_id, code};
        std::string insert_error;
        if (!InvitationStore::instance().insert_code(
                draft, context.account_id, "staff", now_seconds(), insert_error)) {
            apply_error = insert_error;
        } else {
            // Never put the plaintext code in the journal: invites.db is
            // temporary and may be deleted when the feature is retired.
            after_state = {{"code_id", request.target_id},
                           {"created_by_kind", "staff"}};
            entry.undo.reversible = true;
        }
    } else if (request.action_type == "registration.invite_revoke") {
        const std::string code_id = request.parameters.value("code_id", std::string());
        const std::int64_t revoked_at = now_seconds();
        std::string revoke_error;
        if (!InvitationStore::instance().revoke_code(
                code_id, context.account_id, revoked_at, revoke_error)) {
            apply_error = revoke_error;
        } else {
            after_state = {{"code_id", code_id}, {"revoked_at", revoked_at}};
            entry.undo.reversible = true;
        }
    }

    } catch (const std::exception& e) {
        apply_error = e.what();
    }

    if (!apply_error.empty()) {
        store.update_action_status(action_id, "failed", apply_error);
        result.error = apply_error;
        return result;
    }

    entry.after_state = after_state;
    entry.undo.action_type = request.action_type;
    entry.undo.version = request.version;
    entry.undo.parameters = after_state;
    entry.undo.reversible = entry.undo.reversible && !after_state.empty();
    entry.undo.resources.push_back({request.target_type, request.target_id, "primary"});
    entry.status = "applied";
    entry.applied_at = now_seconds();

    auto& sdb = Database::getInstance().staffDB();
    sdb << "UPDATE staff_action_journal SET after_state = ?, undo_descriptor = ?, status = ?, "
          "updated_at = ?, applied_at = ? WHERE id = ?;"
       << entry.after_state.dump()
       << entry.undo.parameters.dump()
       << entry.status << now_seconds() << entry.applied_at << action_id;
    store.append_event(action_id, "applied");
    store.link_resource(action_id, {request.target_type, request.target_id, "primary"});
    emit_action_notification(entry);

    result.ok = true;
    result.data = after_state;
    return result;
}

staff_preflight_result StaffActionEngine::preflight_undo(const std::string& action_id) {
    staff_preflight_result result;
    auto entry = StaffStore::instance().find_action(action_id);
    if (!entry) {
        result.status = staff_preflight_status::target_missing;
        result.reason = "action_not_found";
        return result;
    }
    if (entry->operation == "undo") {
        result.status = staff_preflight_status::not_reversible;
        result.reason = "undo_of_undo";
        return result;
    }
    if (entry->status == "undone") {
        result.status = staff_preflight_status::already_undone;
        result.reason = "already_undone";
        return result;
    }
    if (!entry->undo.reversible) {
        result.status = staff_preflight_status::not_reversible;
        result.reason = "action_not_reversible";
        return result;
    }
    if (entry->status != "applied") {
        result.status = staff_preflight_status::blocked;
        result.reason = "action_not_applied";
        return result;
    }

    auto& game_db = Database::getInstance().gameDB();
    const auto& type = entry->action_type;
    const auto& before = entry->before_state;
    const auto& after = entry->after_state;

    if (type == "staff.set_social_restriction" || type == "staff.set_support_lock") {
        bool found = false;
        json current = read_account_state(std::stoll(entry->target_id), found);
        if (!found) {
            result.status = staff_preflight_status::target_missing;
            result.reason = "account_not_found";
            return result;
        }
        const std::string key = type == "staff.set_social_restriction" ? "social_restricted"
                                                                      : "support_locked";
        if (current.value(key, false) != after.value(key, false)) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "account_restriction_changed";
            return result;
        }
    } else if (type == "staff.grant_monthly_pass" ||
               type == "staff.revoke_monthly_pass") {
        const int64_t character_id = std::stoll(entry->target_id);
        if (!character_exists(character_id)) {
            result.status = staff_preflight_status::target_missing;
            result.reason = "character_not_found";
            return result;
        }
        const json current = read_monthly_pass_state(character_id);
        if (current.value("permanent", false) != after.value("permanent", false) ||
            current.value("expires_at", static_cast<int64_t>(0)) !=
                after.value("expires_at", static_cast<int64_t>(0))) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "monthly_pass_changed";
            return result;
        }
    } else if (type.rfind("staff.set_name_lock", 0) == 0 ||
               type.rfind("staff.set_avatar_lock", 0) == 0 || type == "staff.lock_identity" ||
               type == "staff.revert_name" || type == "staff.revert_avatar" ||
               type == "staff.remove_custom_avatar") {
        bool found = false;
        json current = read_identity_state(std::stoll(entry->target_id), found);
        if (!found) {
            result.status = staff_preflight_status::target_missing;
            result.reason = "character_not_found";
            return result;
        }
        // Composite identity actions are all-or-nothing: any later identity
        // change blocks undo rather than silently overwriting it.
        const char* keys[] = {"display_name", "avatar_safe_id", "avatar_custom_id",
                              "name_locked", "avatar_locked", "identity_locked_by",
                              "identity_locked_at"};
        for (const char* key : keys) {
            if (current.value(key, json()) != after.value(key, json())) {
                result.status = staff_preflight_status::target_changed;
                result.reason = "identity_changed";
                return result;
            }
        }
    } else if (type == "staff.ban_avatar_hash") {
        auto& sdb = Database::getInstance().staffDB();
        bool active = false;
        std::string stored_action;
        const std::string sha = before.value("sha256", std::string());
        sdb << "SELECT COALESCE(action_id, '') FROM banned_avatar_hashes WHERE sha256 = ? "
               "AND revoked_at IS NULL;"
            << sha >> [&](std::string action) { active = true; stored_action = std::move(action); };
        if (!active) {
            result.status = staff_preflight_status::target_missing;
            result.reason = "hash_not_banned";
            return result;
        }
        if (!stored_action.empty() && stored_action != entry->id) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "hash_ban_replaced";
            return result;
        }
    } else if (type == "staff.unban_avatar_hash") {
        auto& sdb = Database::getInstance().staffDB();
        bool active = false;
        const std::string sha = before.value("sha256", std::string());
        sdb << "SELECT 1 FROM banned_avatar_hashes WHERE sha256 = ? AND revoked_at IS NULL LIMIT 1;"
            << sha >> [&](int) { active = true; };
        if (active) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "hash_ban_restored";
            return result;
        }
    } else if (type.rfind("staff.set_barony_", 0) == 0 ||
               type == "staff.reassign_barony_owner") {
        bool found = false;
        json current = read_barony_state(std::stoll(entry->target_id), found);
        if (!found) {
            result.status = staff_preflight_status::target_missing;
            result.reason = "barony_not_found";
            return result;
        }
        const char* barony_keys[] = {"name", "safe_name_id", "safe_tag_ids", "description",
                                     "accepts_minors", "owner_character_id"};
        for (const char* key : barony_keys) {
            if (current.value(key, json()) != after.value(key, json())) {
                result.status = staff_preflight_status::target_changed;
                result.reason = "barony_changed";
                return result;
            }
        }
    } else if (type == "support.unstick_session") {
        // Sessions are frequently replaced; restoring the exact prior active
        // rows is safe because undo re-inserts the captured state below.
        (void)game_db;
    } else if (type == "support.set_game_phase") {
        bool found = false;
        json current = read_phase_state(std::stoll(entry->target_id), found);
        if (!found) {
            result.status = staff_preflight_status::target_missing;
            result.reason = "character_not_found";
            return result;
        }
        if (current.value("game_phase", std::string()) != after.value("game_phase", std::string()) ||
            current.value("last_updated", static_cast<int64_t>(0)) !=
                after.value("last_updated", static_cast<int64_t>(0))) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "game_phase_changed";
            return result;
        }
    } else if (type == "support.set_march_progress") {
        const std::string mini_game = before.value("mini_game", std::string());
        const json current_rows = read_progress_rows(std::stoll(entry->target_id), mini_game);
        if (current_rows != after.value("rows", json::array())) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "march_progress_changed";
            return result;
        }
    } else if (type == "support.cancel_construction") {
        bool found = false;
        read_building_row(std::stoll(entry->target_id), found);
        if (found) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "building_rebuilt";
            return result;
        }
    } else if (type == "support.adjust_fiefdom_resource") {
        const std::string resource = before.value("resource", std::string());
        bool found = false;
        double current = 0.0;
        game_db << "SELECT " + resource + " FROM fiefdoms WHERE id = ?;"
                << std::stoll(entry->target_id)
                >> [&](double value) { found = true; current = value; };
        if (!found) {
            result.status = staff_preflight_status::target_missing;
            result.reason = "fiefdom_not_found";
            return result;
        }
        if (std::abs(current - after.value("value", 0.0)) > 0.0001) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "resource_changed";
            return result;
        }
    } else if (type == "registration.invite_generate") {
        const std::string code_id = before.value("code_id", std::string());
        const auto current = InvitationStore::instance().find_by_id(code_id);
        if (!current) {
            result.status = staff_preflight_status::target_missing;
            result.reason = "invitation_code_not_found";
            return result;
        }
        if (current->used_at > 0 ||
            InvitationStore::instance().is_redeemed_in_game(code_id)) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "invitation_code_used";
            return result;
        }
    } else if (type == "registration.invite_revoke") {
        const std::string code_id = before.value("code_id", std::string());
        const auto current = InvitationStore::instance().find_by_id(code_id);
        if (!current) {
            result.status = staff_preflight_status::target_missing;
            result.reason = "invitation_code_not_found";
            return result;
        }
        if (current->used_at > 0) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "invitation_code_used";
            return result;
        }
        if (current->revoked_at !=
            after.value("revoked_at", static_cast<std::int64_t>(0))) {
            result.status = staff_preflight_status::target_changed;
            result.reason = "invitation_code_changed";
            return result;
        }
    } else if (type == "chat.ban_member" || type == "chat.unban_member" ||
               type == "chat.kick_member" || type == "chat.delete_message" ||
               type == "chat.set_moderator" || type == "chat.rename_group") {
        auto& cdb = chat::ChatDB::instance().db();
        const std::string chat_id = before.value("chat_id", std::string());
        bool exists = false;
        if (type == "chat.delete_message") {
            cdb << "SELECT 1 FROM messages WHERE id = ? LIMIT 1;"
                << before.value("message_id", std::string()) >> [&](int) { exists = true; };
        } else if (type == "chat.kick_member" || type == "chat.set_moderator") {
            cdb << "SELECT 1 FROM chat_members WHERE chat_id = ? AND character_id = ? LIMIT 1;"
                << chat_id << before.value("target_character_id", static_cast<int64_t>(0))
                >> [&](int) { exists = true; };
        } else {
            cdb << "SELECT 1 FROM chats WHERE id = ? LIMIT 1;" << chat_id
                >> [&](int) { exists = true; };
        }
        if (!exists) {
            result.status = staff_preflight_status::target_missing;
            result.reason = "chat_target_missing";
            return result;
        }
    } else if (type == "staff.set_adult_status") {
        result.status = staff_preflight_status::not_reversible;
        result.reason = "demotion_is_irreversible";
        return result;
    } else {
        result.status = staff_preflight_status::blocked;
        result.reason = "no_undo_path";
        return result;
    }

    result.status = staff_preflight_status::available;
    result.reason.clear();
    return result;
}

staff_action_result StaffActionEngine::undo(const staff_context& context,
                                            const std::string& action_id,
                                            const std::string& reason) {
    staff_action_result result;
    if (!context.is_staff() || !context.has("staff.undo")) {
        result.error = "missing_capability:staff.undo";
        return result;
    }
    auto original = StaffStore::instance().find_action(action_id);
    if (!original) {
        result.error = "action_not_found";
        return result;
    }
    auto preflight = preflight_undo(action_id);
    if (preflight.status != staff_preflight_status::available) {
        result.error = preflight_status_name(preflight.status);
        if (!preflight.reason.empty()) result.error += ":" + preflight.reason;
        return result;
    }

    staff_action_journal_entry undo_entry;
    undo_entry.operation = "undo";
    undo_entry.actor_account_id = context.account_id;
    undo_entry.actor_type = "staff";
    undo_entry.authority = "staff_capability";
    undo_entry.capability = "staff.undo";
    undo_entry.action_type = original->action_type;
    undo_entry.action_version = original->action_version;
    undo_entry.target_type = original->target_type;
    undo_entry.target_id = original->target_id;
    undo_entry.scope = original->scope;
    undo_entry.reason = reason;
    undo_entry.undo_of = action_id;
    undo_entry.before_state = original->after_state;
    undo_entry.status = "pending";
    std::string append_error;
    const std::string undo_id = StaffStore::instance().append_action(undo_entry, append_error);
    if (undo_id.empty()) {
        result.error = append_error.empty() ? "journal_write_failed" : append_error;
        return result;
    }
    result.action_id = undo_id;

    json after = json::object();
    std::string error;
    const auto& type = original->action_type;
    const auto& before = original->before_state;
    auto& game_db = Database::getInstance().gameDB();

    try {
        if (type == "staff.set_social_restriction") {
            game_db << "UPDATE users SET social_restricted = ?, restriction_updated_at = ? WHERE id = ?;"
                    << (before.value("social_restricted", false) ? 1 : 0) << now_seconds()
                    << std::stoll(original->target_id);
            after = before;
        } else if (type == "staff.set_support_lock") {
            game_db << "UPDATE users SET support_locked = ?, restriction_updated_at = ? WHERE id = ?;"
                    << (before.value("support_locked", false) ? 1 : 0) << now_seconds()
                    << std::stoll(original->target_id);
            after = before;
        } else if (type == "staff.grant_monthly_pass" ||
                   type == "staff.revoke_monthly_pass") {
            monthly_pass::grant_origin origin;
            origin.source = "staff_undo";
            origin.reason = reason;
            origin.staff_account_id = context.account_id;
            origin.reference_id = undo_id;
            monthly_pass::restore(game_db, std::stoll(original->target_id),
                                  monthly_pass_status_from_json(before), origin,
                                  now_seconds());
            after = before;
        } else if (type == "staff.set_name_lock" || type == "staff.set_avatar_lock" ||
                   type == "staff.lock_identity" || type == "staff.revert_name" ||
                   type == "staff.revert_avatar" || type == "staff.remove_custom_avatar") {
            game_db << "UPDATE characters SET display_name = ?, safe_display_name = ?, "
                       "avatar_safe_id = ?, avatar_custom_id = ?, avatar_storage = ?, "
                       "avatar_changed_at = ?, name_locked = ?, avatar_locked = ?, "
                       "identity_locked_by = ?, identity_locked_at = ? WHERE id = ?;"
                    << before.value("display_name", std::string())
                    << before.value("safe_display_name", std::string())
                    << before.value("avatar_safe_id", std::string())
                    << (before.value("avatar_custom_id", std::string()).empty()
                            ? std::optional<std::string>()
                            : std::optional<std::string>(before.value("avatar_custom_id", std::string())))
                    << before.value("avatar_storage", std::string("local"))
                    << before.value("avatar_changed_at", static_cast<int64_t>(0))
                    << (before.value("name_locked", false) ? 1 : 0)
                    << (before.value("avatar_locked", false) ? 1 : 0)
                    << before.value("identity_locked_by", static_cast<int64_t>(0))
                    << before.value("identity_locked_at", static_cast<int64_t>(0))
                    << std::stoll(original->target_id);
            after = before;
        } else if (type == "staff.ban_avatar_hash") {
            const std::string sha = before.value("sha256", std::string());
            auto& sdb = Database::getInstance().staffDB();
            sdb << "UPDATE banned_avatar_hashes SET revoked_at = ?, revoked_by_account_id = ? "
                   "WHERE sha256 = ?;"
                << now_seconds() << context.account_id << sha;
            after = {{"sha256", sha}, {"active", false}};
        } else if (type == "staff.unban_avatar_hash") {
            const std::string sha = before.value("sha256", std::string());
            auto& sdb = Database::getInstance().staffDB();
            sdb << "UPDATE banned_avatar_hashes SET revoked_at = NULL, revoked_by_account_id = NULL "
                   "WHERE sha256 = ?;"
                << sha;
            after = {{"sha256", sha}, {"active", true}};
        } else if (type == "staff.set_barony_safe_identity" ||
                   type == "staff.set_barony_accepts_minors" ||
                   type == "staff.reassign_barony_owner") {
            game_db << "UPDATE baronies SET name = ?, safe_name_id = ?, safe_tag_ids = ?, "
                       "description = ?, accepts_minors = ?, owner_character_id = ?, "
                       "baron_character_id = ? WHERE id = ?;"
                    << before.value("name", std::string())
                    << before.value("safe_name_id", std::string())
                    << before.value("safe_tag_ids", std::string("[]"))
                    << before.value("description", std::string())
                    << (before.value("accepts_minors", false) ? 1 : 0)
                    << before.value("owner_character_id", static_cast<int64_t>(0))
                    << before.value("baron_character_id", static_cast<int64_t>(0))
                    << std::stoll(original->target_id);
            after = before;
        } else if (type == "support.unstick_session") {
            const int64_t character_id = std::stoll(original->target_id);
            std::set<int64_t> game_ids;
            for (const auto& row : before.value("game_sessions", json::array())) {
                game_ids.insert(row.value("id", static_cast<int64_t>(0)));
                game_db << "UPDATE game_sessions SET state = ?, last_activity = ? WHERE id = ?;"
                        << row.value("state", std::string("active"))
                        << row.value("last_activity", static_cast<int64_t>(0))
                        << row.value("id", static_cast<int64_t>(0));
            }
            for (const auto& row : before.value("weeding_sessions", json::array())) {
                game_db << "UPDATE weeding_sessions SET state = ?, last_activity = ? WHERE id = ?;"
                        << row.value("state", std::string("active"))
                        << row.value("last_activity", static_cast<int64_t>(0))
                        << row.value("id", static_cast<int64_t>(0));
            }
            (void)character_id;
            after = before;
        } else if (type == "support.set_game_phase") {
            game_db << "UPDATE player_game_state SET game_phase = ?, current_mini_game = ?, "
                       "current_level_id = ?, base_unlocked = ?, entered_at = ?, last_updated = ?, "
                       "honor_name = ?, land_patent_acknowledged = ? WHERE character_id = ?;"
                    << before.value("game_phase", std::string("initial_mission"))
                    << (before.value("current_mini_game", std::string()).empty()
                            ? std::optional<std::string>()
                            : std::optional<std::string>(before.value("current_mini_game", std::string())))
                    << before.value("current_level_id", 0)
                    << (before.value("base_unlocked", false) ? 1 : 0)
                    << before.value("entered_at", static_cast<int64_t>(0))
                    << before.value("last_updated", static_cast<int64_t>(0))
                    << (before.value("honor_name", std::string()).empty()
                            ? std::optional<std::string>()
                            : std::optional<std::string>(before.value("honor_name", std::string())))
                    << (before.value("land_patent_acknowledged", false) ? 1 : 0)
                    << std::stoll(original->target_id);
            after = before;
        } else if (type == "support.set_march_progress") {
            restore_progress_rows(std::stoll(original->target_id), before.value("rows", json::array()));
            after = before;
        } else if (type == "support.cancel_construction") {
            restore_building_row(before);
            after = before;
        } else if (type == "support.adjust_fiefdom_resource") {
            const std::string resource = before.value("resource", std::string());
            game_db << "UPDATE fiefdoms SET " + resource + " = ? WHERE id = ?;"
                    << before.value("value", 0.0) << std::stoll(original->target_id);
            after = before;
        } else if (type == "registration.invite_generate") {
            const std::string code_id = before.value("code_id", std::string());
            auto& idb = Database::getInstance().invitesDB();
            idb << "DELETE FROM invitation_codes WHERE id = ? AND used_at IS NULL;"
                << code_id;
            int changed = 0;
            idb << "SELECT changes();" >> changed;
            if (changed == 0) {
                error = "invitation_code_used";
            } else {
                after = before;
            }
        } else if (type == "registration.invite_revoke") {
            const std::string code_id = before.value("code_id", std::string());
            const std::int64_t revoked_at =
                original->after_state.value("revoked_at", static_cast<std::int64_t>(0));
            auto& idb = Database::getInstance().invitesDB();
            idb << "UPDATE invitation_codes SET revoked_at = NULL, "
                   "revoked_by_account_id = NULL "
                   "WHERE id = ? AND used_at IS NULL AND revoked_at = ?;"
                << code_id << revoked_at;
            int changed = 0;
            idb << "SELECT changes();" >> changed;
            if (changed == 0) {
                error = "invitation_code_changed";
            } else {
                after = before;
            }
        } else if (type == "chat.ban_member") {
            auto& cdb = chat::ChatDB::instance().db();
            const std::string chat_id = before.value("chat_id", std::string());
            const int64_t target = before.value("target_character_id", static_cast<int64_t>(0));
            cdb << "DELETE FROM chat_bans WHERE chat_id = ? AND character_id = ?;"
                << chat_id << target;
            if (before.value("had_ban", false)) {
                cdb << "INSERT OR REPLACE INTO chat_bans (chat_id, character_id, "
                       "banned_by_character_id, reason, created_at, change_id) "
                       "VALUES (?, ?, ?, ?, ?, ?);"
                    << chat_id << target << before.value("banned_by_character_id", static_cast<int64_t>(0))
                    << before.value("ban_reason", std::string())
                    << before.value("ban_created_at", static_cast<int64_t>(0))
                    << chat::ChatDB::instance().allocate_change_id();
            }
            const int64_t left_at = before.value("member_left_at", static_cast<int64_t>(0));
            if (left_at > 0) {
                cdb << "UPDATE chat_members SET left_at = ?, change_id = ? "
                       "WHERE chat_id = ? AND character_id = ?;"
                    << left_at << chat::ChatDB::instance().allocate_change_id() << chat_id << target;
            } else {
                cdb << "UPDATE chat_members SET left_at = NULL, change_id = ? "
                       "WHERE chat_id = ? AND character_id = ?;"
                    << chat::ChatDB::instance().allocate_change_id() << chat_id << target;
            }
            after = before;
        } else if (type == "chat.unban_member") {
            auto& cdb = chat::ChatDB::instance().db();
            const std::string chat_id = before.value("chat_id", std::string());
            const int64_t target = before.value("target_character_id", static_cast<int64_t>(0));
            if (before.value("had_ban", false)) {
                cdb << "INSERT OR REPLACE INTO chat_bans (chat_id, character_id, "
                       "banned_by_character_id, reason, created_at, change_id) "
                       "VALUES (?, ?, ?, ?, ?, ?);"
                    << chat_id << target << before.value("banned_by_character_id", static_cast<int64_t>(0))
                    << before.value("ban_reason", std::string())
                    << before.value("ban_created_at", static_cast<int64_t>(0))
                    << chat::ChatDB::instance().allocate_change_id();
            }
            after = before;
        } else if (type == "chat.kick_member") {
            auto& cdb = chat::ChatDB::instance().db();
            const std::string chat_id = before.value("chat_id", std::string());
            const int64_t target = before.value("target_character_id", static_cast<int64_t>(0));
            const int64_t left_at = before.value("left_at", static_cast<int64_t>(0));
            if (left_at > 0) {
                cdb << "UPDATE chat_members SET left_at = ?, change_id = ? "
                       "WHERE chat_id = ? AND character_id = ?;"
                    << left_at << chat::ChatDB::instance().allocate_change_id() << chat_id << target;
            } else {
                cdb << "UPDATE chat_members SET left_at = NULL, change_id = ? "
                       "WHERE chat_id = ? AND character_id = ?;"
                    << chat::ChatDB::instance().allocate_change_id() << chat_id << target;
            }
            after = before;
        } else if (type == "chat.delete_message") {
            auto& cdb = chat::ChatDB::instance().db();
            const std::string message_id = before.value("message_id", std::string());
            const std::string body = before.value("body", std::string());
            const std::string safe_id = before.value("safe_message_id", std::string());
            const std::string gif_id = before.value("gif_id", std::string());
            const int64_t deleted_at = before.value("deleted_at", static_cast<int64_t>(0));
            cdb << "UPDATE messages SET body = ?, safe_message_id = ?, gif_id = ?, deleted_at = ?, "
                   "change_id = ? WHERE id = ?;"
                << (body.empty() ? std::optional<std::string>() : std::optional<std::string>(body))
                << (safe_id.empty() ? std::optional<std::string>()
                                    : std::optional<std::string>(safe_id))
                << (gif_id.empty() ? std::optional<std::string>()
                                   : std::optional<std::string>(gif_id))
                << (deleted_at > 0 ? std::optional<int64_t>(deleted_at)
                                   : std::optional<int64_t>())
                << chat::ChatDB::instance().allocate_change_id() << message_id;
            after = before;
        } else if (type == "chat.set_moderator") {
            auto& cdb = chat::ChatDB::instance().db();
            cdb << "UPDATE chat_members SET role = ?, change_id = ? WHERE chat_id = ? AND character_id = ?;"
                << before.value("before_role", std::string("member"))
                << chat::ChatDB::instance().allocate_change_id()
                << before.value("chat_id", std::string())
                << before.value("target_character_id", static_cast<int64_t>(0));
            after = before;
        } else if (type == "chat.rename_group") {
            auto& cdb = chat::ChatDB::instance().db();
            const int64_t changed_at = before.value("name_changed_at", static_cast<int64_t>(0));
            cdb << "UPDATE chats SET name = ?, name_changed_at = ?, change_id = ? WHERE id = ?;"
                << (before.value("name", std::string()).empty()
                        ? std::optional<std::string>()
                        : std::optional<std::string>(before.value("name", std::string())))
                << (changed_at > 0 ? std::optional<int64_t>(changed_at)
                                   : std::optional<int64_t>())
                << chat::ChatDB::instance().allocate_change_id()
                << before.value("chat_id", std::string());
            after = before;
        } else {
            error = "no_undo_path";
        }
    } catch (const std::exception& e) {
        error = e.what();
    }

    if (!error.empty()) {
        StaffStore::instance().update_action_status(undo_id, "failed", error);
        result.error = error;
        return result;
    }

    StaffStore::instance().update_action_status(undo_id, "applied");
    StaffStore::instance().update_action_status(action_id, "undone");
    auto& sdb = Database::getInstance().staffDB();
    sdb << "UPDATE staff_action_journal SET after_state = ?, updated_at = ?, applied_at = ? "
          "WHERE id = ?;"
       << after.dump() << now_seconds() << now_seconds() << undo_id;

    // Reversal notification names the original action and links to support.
    // Monthly-pass staff tooling is deliberately silent toward the player for
    // now; its append-only log and the staff journal are the audit surface.
    if (original->action_type == "staff.grant_monthly_pass" ||
        original->action_type == "staff.revoke_monthly_pass") {
        // no player-facing notification
    } else if (original->target_type == "character") {
        notify_character(std::stoll(original->target_id), "moderation_reversal",
                         "ui_notification_reversal",
                         {{"character_name", character_safe_name(std::stoll(original->target_id))},
                          {"action_type", original->action_type},
                          {"reason", reason}},
                         "#/activity/support", undo_id);
    } else if (original->target_type == "account") {
        notify_account(std::stoll(original->target_id), "moderation_reversal",
                       "ui_notification_reversal",
                       {{"action_type", original->action_type}, {"reason", reason}},
                       "#/activity/support", undo_id);
    }

    result.ok = true;
    result.data = after;
    return result;
}

std::string record_scoped_chat_action(int64_t actor_character_id, int64_t actor_account_id,
                                      const std::string& action_type,
                                      const std::string& chat_id,
                                      int64_t target_character_id,
                                      const json& before_state, const json& after_state,
                                      const json& undo_parameters,
                                      const std::string& reason) {
    staff_action_journal_entry entry;
    entry.actor_account_id = actor_account_id;
    entry.actor_character_id = actor_character_id;
    entry.actor_type = "chat_member";
    entry.authority = "chat_role";
    entry.action_type = action_type;
    entry.target_type = target_character_id > 0 ? "character" : "chat";
    entry.target_id = std::to_string(target_character_id > 0 ? target_character_id : 0);
    if (target_character_id <= 0) entry.target_id = chat_id;
    entry.scope = chat_id;
    entry.reason = reason;
    entry.before_state = before_state;
    entry.after_state = after_state;
    entry.undo.action_type = action_type;
    entry.undo.parameters = undo_parameters;
    entry.undo.reversible = true;
    entry.undo.resources.push_back({"chat", chat_id, "primary"});
    if (target_character_id > 0) {
        entry.undo.resources.push_back({"character", std::to_string(target_character_id), "effect"});
    }
    entry.status = "applied";
    entry.applied_at = now_seconds();
    std::string error;
    const std::string id = StaffStore::instance().append_action(entry, error);
    if (!id.empty()) {
        StaffStore::instance().append_event(id, "applied");
        for (const auto& resource : entry.undo.resources) {
            StaffStore::instance().link_resource(id, resource);
        }
    }
    return id;
}

std::string record_read_event(const staff_context& context, const std::string& action_type,
                              const std::string& target_type, const std::string& target_id,
                              const std::string& scope, const json& detail) {
    if (!context.is_staff()) return std::string();
    staff_action_journal_entry entry;
    entry.operation = "read";
    entry.actor_account_id = context.account_id;
    entry.actor_type = "staff";
    entry.authority = "staff_capability";
    entry.capability = "staff.read_sensitive";
    entry.action_type = action_type;
    entry.target_type = target_type;
    entry.target_id = target_id;
    entry.scope = scope;
    entry.after_state = detail;
    entry.status = "applied";
    entry.applied_at = now_seconds();
    std::string error;
    return StaffStore::instance().append_action(entry, error);
}

void notify_account(int64_t account_id, const std::string& kind, const std::string& text_id,
                    const json& text_args, const std::string& route,
                    const std::string& action_id, int64_t character_id) {
    if (account_id <= 0) return;
    staff_notification_spec spec;
    spec.account_id = account_id;
    spec.character_id = character_id;
    spec.kind = kind;
    spec.text_id = text_id;
    spec.text_args = text_args;
    spec.route = route;
    spec.action_id = action_id;
    StaffStore::instance().create_notification(std::move(spec));
}

void notify_character(int64_t character_id, const std::string& kind, const std::string& text_id,
                      const json& text_args, const std::string& route,
                      const std::string& action_id) {
    int64_t account_id = 0;
    if (!character_account_id(character_id, account_id)) return;
    notify_account(account_id, kind, text_id, text_args, route, action_id, character_id);
}

void notify_support_reply(const std::string& ticket_id, int64_t account_id,
                          const std::string& subject, bool staff_created,
                          const std::string& action_id) {
    if (account_id <= 0) return;
    notify_account(account_id, staff_created ? "support_ticket" : "support_reply",
                   staff_created ? "ui_notification_support_ticket" : "ui_notification_support_reply",
                   {{"subject", subject}},
                   "#/activity/support/" + ticket_id, action_id);
}

}  // namespace staff
