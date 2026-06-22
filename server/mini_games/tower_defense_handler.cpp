#include "tower_defense_handler.hpp"
#include "../GameConfigCache.hpp"
#include <iostream>

std::string TowerDefenseHandler::name() const {
    return "tower_defense";
}

nlohmann::json TowerDefenseHandler::start_level(const MiniGameContext& ctx) {
    nlohmann::json level_config;

    if (ctx.is_random_generation) {
        level_config["map"] = "random";
        level_config["difficulty"] = 1;
        level_config["num_waves"] = 5;
        level_config["lane_count"] = 1;
        level_config["enemy_types"] = nlohmann::json::array({"basic"});
        level_config["rewards"] = {{"gold", 5}};
    } else {
        auto& config_cache = GameConfigCache::getInstance();
        const auto& mini_games = config_cache.getMiniGames();
        const auto& td_config = mini_games["tower_defense"];
        const auto& levels = td_config["levels"];

        for (const auto& level : levels) {
            if (level["id"] == ctx.level_id) {
                level_config = level;
                break;
            }
        }

        level_config["map"] = "tower_defense_level_" + std::to_string(ctx.level_id);
        level_config["num_waves"] = 3 + ctx.level_id;
        level_config["lane_count"] = 1;
        level_config["enemy_types"] = nlohmann::json::array({"basic"});
    }

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
