#include "../mini_games.hpp"
#include "../TowerDefenseMapCache.hpp"
#include "../UnitUnlockCalculator.hpp"
#include "../Database.hpp"
#include <iostream>
#include <chrono>
#include <set>

static void resolve_td_image_urls(nlohmann::json& entries, const std::string& category) {
    for (auto& [id, entry] : entries.items()) {
        std::string img = entry.value("image_file", id);
        entry["image_url"] = "/images/tower_defense/" + category + "/" + img;
    }
}

/** Filter a set of items (units or towers) to only those whose IDs are in the allowed set. */
static nlohmann::json filter_by_allowed(const nlohmann::json& items, const std::set<std::string>& allowed_ids) {
    nlohmann::json filtered = nlohmann::json::object();
    if (!items.is_object()) return filtered;
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (allowed_ids.count(it.key())) {
            filtered[it.key()] = it.value();
        }
    }
    return filtered;
}

/** Build a set of available item IDs from player unlocks minus level disallowed. */
static std::set<std::string> compute_available_set(
    const nlohmann::json& unlocks,
    const std::string& item_type,
    const nlohmann::json& disallowed)
{
    std::set<std::string> result;
    if (unlocks.contains(item_type)) {
        for (const auto& id : unlocks[item_type]) {
            result.insert(id.get<std::string>());
        }
    }
    if (disallowed.contains(item_type) && disallowed[item_type].is_array()) {
        for (const auto& id : disallowed[item_type]) {
            result.erase(id.get<std::string>());
        }
    }
    return result;
}

std::string TowerDefenseHandler::name() const {
    return "tower_defense";
}

nlohmann::json TowerDefenseHandler::start_level(const MiniGameContext& ctx) {
    nlohmann::json level_config;

    auto& db = Database::getInstance().gameDB();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    // Grant starting unlocks on first play
    UnitUnlockCalculator::grant_starting_unlocks(config_cache_, db, ctx.character_id, now);

    if (ctx.is_random_generation) {
        level_config["map"] = "random";
        level_config["difficulty"] = 1;
        level_config["rewards"] = {{"gold", 5}};
    } else {
        const auto& mini_games = config_cache_.getMiniGames();
        const auto& td_config = mini_games["tower_defense"];
        const auto& levels = td_config["levels"];

        for (const auto& level : levels) {
            if (level["id"] == ctx.level_id) {
                level_config = level;
                break;
            }
        }

        std::string map_file = level_config.value("map", "");
        if (!map_file.empty()) {
            auto map_meta = TowerDefenseMapCache::get_instance().get_map(map_file);
            if (map_meta.has_value()) {
                level_config["map_metadata"] = *map_meta;
            }
        }
    }

    // Load full item catalogs
    level_config["mobs"] = config_cache_.getTowerDefenseMobs();
    level_config["towers"] = config_cache_.getTowerDefenseTowers();
    level_config["units"] = config_cache_.getTowerDefenseUnits();

    // Filter towers/units by player unlocks minus level disallowed
    nlohmann::json unlocks = UnitUnlockCalculator::get_player_unlocks(db, ctx.character_id);
    nlohmann::json disallowed = level_config.value("disallowed", nlohmann::json::object());

    if (level_config["towers"].contains("towers")) {
        std::set<std::string> allowed = compute_available_set(unlocks, "towers", disallowed);
        level_config["towers"]["towers"] = filter_by_allowed(level_config["towers"]["towers"], allowed);
    }

    if (level_config["units"].contains("units")) {
        std::set<std::string> allowed = compute_available_set(unlocks, "units", disallowed);
        level_config["units"]["units"] = filter_by_allowed(level_config["units"]["units"], allowed);
    }

    resolve_td_image_urls(level_config["mobs"]["mobs"], "mobs");
    resolve_td_image_urls(level_config["towers"]["towers"], "towers");
    resolve_td_image_urls(level_config["units"]["units"], "units");

    level_config["mini_game"] = "tower_defense";
    level_config["level_id"] = ctx.level_id;

    return level_config;
}

nlohmann::json TowerDefenseHandler::end_level(const MiniGameContext& ctx, const MiniGameResult& result) {
    nlohmann::json outcome;

    outcome["won"] = result.won;
    outcome["score"] = result.score;

    // On win, check and grant milestone unlocks
    if (result.won) {
        auto& db = Database::getInstance().gameDB();
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        // Count completed tower defense levels
        int completed_count = 0;
        db << "SELECT COUNT(*) FROM mini_game_progress "
              "WHERE character_id = ? AND mini_game = 'tower_defense' AND completed = 1;"
           << ctx.character_id
           >> [&](int count) { completed_count = count; };

        nlohmann::json new_unlocks = UnitUnlockCalculator::check_and_grant_milestones(
            config_cache_, db, ctx.character_id, completed_count, now);

        if (!new_unlocks["new_units"].empty() || !new_unlocks["new_towers"].empty()) {
            outcome["new_unlocks"] = new_unlocks;
        }
    }

    return outcome;
}

nlohmann::json TowerDefenseHandler::get_config() const {
    const auto& mini_games = config_cache_.getMiniGames();

    if (mini_games.contains("tower_defense")) {
        return mini_games["tower_defense"];
    }

    return nlohmann::json::object();
}

bool TowerDefenseHandler::validate_prerequisites(int character_id, int level_id) const {
    (void)character_id;
    (void)level_id;
    return true;
}
