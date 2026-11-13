#pragma once
#include <nlohmann/json.hpp>
#include "db/postgres_sqlite_compat.hpp"
#include "GameConfigCache.hpp"
#include <string>

class UnitUnlockCalculator {
public:
    static void grant_starting_unlocks(const config_snapshot& config_cache, sqlite::database& db, int character_id, int64_t timestamp);

    static nlohmann::json get_player_unlocks(sqlite::database& db, int character_id);

    static nlohmann::json check_and_grant_milestones(
        const config_snapshot& config_cache,
        sqlite::database& db,
        int character_id,
        int completed_count,
        int64_t timestamp
    );
};
