#include "../mini_games.hpp"
#include <iostream>

std::string WeedingHandler::name() const {
    return "weeding";
}

nlohmann::json WeedingHandler::start_level(const MiniGameContext& ctx) {
    nlohmann::json level_config;

    if (ctx.is_random_generation) {
        level_config["map"] = "random";
        level_config["difficulty"] = 1;
        level_config["grid_size"] = 8;
        level_config["weed_density"] = 0.3;
        level_config["rewards"] = {{"gold", 5}, {"wood", 3}};
    } else {
        const auto& mini_games = config_cache_.getMiniGames();
        const auto& weed_config = mini_games["weeding"];
        const auto& levels = weed_config["levels"];

        for (const auto& level : levels) {
            if (level["id"] == ctx.level_id) {
                level_config = level;
                break;
            }
        }

        level_config["map"] = "weeding_level_" + std::to_string(ctx.level_id);
        level_config["grid_size"] = 6 + ctx.level_id;
        level_config["weed_density"] = 0.2 + (ctx.level_id * 0.05);
    }

    level_config["mini_game"] = "weeding";
    level_config["level_id"] = ctx.level_id;

    return level_config;
}

nlohmann::json WeedingHandler::end_level(const MiniGameContext& ctx, const MiniGameResult& result) {
    nlohmann::json outcome;

    outcome["won"] = result.won;
    outcome["score"] = result.score;

    return outcome;
}

nlohmann::json WeedingHandler::get_config() const {
    const auto& mini_games = config_cache_.getMiniGames();

    if (mini_games.contains("weeding")) {
        return mini_games["weeding"];
    }

    return nlohmann::json::object();
}

bool WeedingHandler::validate_prerequisites(int character_id, int level_id) const {
    (void)character_id;
    (void)level_id;
    return true;
}
