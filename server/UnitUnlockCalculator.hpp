#pragma once
#include <nlohmann/json.hpp>
#include <sqlite_modern_cpp.h>
#include <string>

/**
 * Manages tower defense unit/tower unlocks from milestones.
 * 
 * Handles awarding starting equipment, querying what's currently
 * unlocked, and checking/grants milestone rewards on level completion.
 */
class UnitUnlockCalculator {
public:
    /** Insert starting units/towers for a character if they don't have any unlocks yet. */
    static void grant_starting_unlocks(sqlite::database& db, int character_id, int64_t timestamp);

    /** Get the set of currently unlocked unit and tower IDs for a character. */
    static nlohmann::json get_player_unlocks(sqlite::database& db, int character_id);

    /**
     * Evaluate milestones against the character's completed level count.
     * Inserts any newly-achieved unlocks into the DB.
     * Returns the new unlocks in { new_units: [...], new_towers: [...] } format.
     */
    static nlohmann::json check_and_grant_milestones(
        sqlite::database& db,
        int character_id,
        int completed_count,
        int64_t timestamp
    );
};
