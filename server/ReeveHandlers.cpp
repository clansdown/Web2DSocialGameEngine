#include "ReeveHandlers.hpp"

#include "Database.hpp"
#include "FiefdomFetcher.hpp"
#include "GameConfigCache.hpp"
#include "ReeveAdvisor.hpp"
#include "ReeveConfig.hpp"
#include "game_logic.hpp"

#include <chrono>

using json = nlohmann::json;

ApiResponse handleGetReeveReport(
    GameConfigCache& config_cache,
    const json& body,
    const std::optional<std::string>& username,
    const ClientInfo& client,
    const std::optional<std::string>& new_token) {
    (void)client;

    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }

    int fiefdom_id = body.value("fiefdom_id", 0);
    auto& db = Database::getInstance().gameDB();

    if (fiefdom_id == 0 && body.contains("character_id") &&
        body["character_id"].is_number_integer()) {
        int character_id = body["character_id"].get<int>();
        db << "SELECT id FROM fiefdoms WHERE owner_id = ? LIMIT 1;" << character_id
           >> [&](int id) { fiefdom_id = id; };
    }
    if (fiefdom_id == 0) {
        response.error = "fiefdom_id or character_id required";
        return response;
    }

    int owner_id = 0;
    db << "SELECT owner_id FROM fiefdoms WHERE id = ?;" << fiefdom_id
       >> [&](int id) { owner_id = id; };
    int user_id = 0;
    db << "SELECT id FROM users WHERE username = ?;" << *username
       >> [&](int id) { user_id = id; };
    if (owner_id == 0 || user_id == 0) {
        response.error = "fiefdom not found";
        return response;
    }
    int owner_user_id = 0;
    db << "SELECT user_id FROM characters WHERE id = ?;" << owner_id
       >> [&](int id) { owner_user_id = id; };
    if (owner_user_id != user_id) {
        response.error = "Fiefdom does not belong to this user";
        return response;
    }

    int64_t last_update_time = 0;
    db << "SELECT last_update_time FROM fiefdoms WHERE id = ?;" << fiefdom_id
       >> [&](int64_t ts) { last_update_time = ts; };

    auto time_result = GameLogic::updateStateSince(
        config_cache, last_update_time, std::to_string(fiefdom_id));

    // The river is lazy-seeded; ensure it exists outside the dry-run transaction
    // so placement can avoid river cells even if this is the first request.
    FiefdomFetcher::ensureFiefdomRiver(fiefdom_id, config_cache.getManorRiver());

    // Project one full day through the real economy engine, then roll the
    // transaction back. This gives the Reeve a stable per-day ledger without
    // advancing the fiefdom (and avoids short-tick fractional-import artifacts).
    const int64_t now_ts = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    auto projection = GameLogic::updateStateSince(
        config_cache, now_ts - 86400, std::to_string(fiefdom_id),
        /*dry_run=*/true);

    auto projection_it = projection.economy_reports.find(fiefdom_id);
    auto real_it = time_result.economy_reports.find(fiefdom_id);

    auto fiefdom_opt = FiefdomFetcher::fetchFiefdomById(
        fiefdom_id, /*include_buildings=*/true, /*include_officials=*/false,
        /*include_heroes=*/false, /*include_combatants=*/false);
    if (!fiefdom_opt) {
        response.error = "fiefdom not found";
        return response;
    }

    json economy_report = json::object();
    if (projection_it != projection.economy_reports.end()) {
        economy_report = projection_it->second;
    } else if (real_it != time_result.economy_reports.end()) {
        economy_report = real_it->second;
    }

    Reeve::ReeveConfig config = Reeve::parse_reeve_config(
        config_cache.getReeveConfig());
    const std::string style_tag = Reeve::get_default_reeve_style_tag(config);
    const Reeve::ReeveStyle& style = Reeve::get_reeve_style(config, style_tag);

    Reeve::ReeveReport report = Reeve::build_reeve_report(
        config_cache, *fiefdom_opt, economy_report, config, style);
    response.data = Reeve::reeve_report_to_json(report);

    if (new_token) response.data["token"] = *new_token;
    return response;
}
