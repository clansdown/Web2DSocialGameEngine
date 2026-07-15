#pragma once
#include <nlohmann/json.hpp>
#include <sqlite_modern_cpp.h>
#include <string>

class GameConfigCache;

class UnitUnlockCalculator {
public:
    static void grant_starting_unlocks(GameConfigCache& config_cache, sqlite::database& db, int character_id, int64_t timestamp);

    static nlohmann::json get_player_unlocks(sqlite::database& db, int character_id);

    static nlohmann::json check_and_grant_milestones(
        GameConfigCache& config_cache,
        sqlite::database& db,
        int character_id,
        int completed_count,
        int64_t timestamp
    );
};
