#include "tower_defense_handler.hpp"
#include "../GameConfigCache.hpp"
#include "../TowerDefenseMapCache.hpp"
#include <iostream>

std::string TowerDefenseHandler::name() const {
    return "tower_defense";
}

nlohmann::json TowerDefenseHandler::start_level(const MiniGameContext& ctx) {
    nlohmann::json level_config;

    auto& config_cache = GameConfigCache::getInstance();

    if (ctx.is_random_generation) {
        level_config["map"] = "random";
        level_config["difficulty"] = 1;
        level_config["rewards"] = {{"gold", 5}};
    } else {
        const auto& mini_games = config_cache.getMiniGames();
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

    level_config["mobs"] = config_cache.getTowerDefenseMobs();
    level_config["towers"] = config_cache.getTowerDefenseTowers();
    level_config["units"] = config_cache.getTowerDefenseUnits();

    level_config["mini_game"] = "tower_defense";
    level_config["level_id"] = ctx.level_id;

    return level_config;
}

nlohmann::json TowerDefenseHandler::end_level(const MiniGameContext& ctx, const MiniGameResult& result) {
    nlohmann::json outcome;

    outcome["won"] = result.won;
    outcome["score"] = result.score;

    return outcome;
}

nlohmann::json TowerDefenseHandler::get_config() const {
    auto& config_cache = GameConfigCache::getInstance();
    const auto& mini_games = config_cache.getMiniGames();

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
