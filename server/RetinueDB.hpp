#pragma once
#include "combat/CombatTypes.hpp"
#include <nlohmann/json.hpp>
#include <sqlite_modern_cpp.h>
#include <string>
#include <vector>

// Persistence layer for the player's retinue (see server/tables/retinue_members.md).
// The combat game reads a retinue snapshot when a player joins a match and
// writes casualties back after a match ends. The manor game's train_troops
// action will create members (currently stubbed).
namespace retinue_db {

struct retinue_member {
    int64_t id = 0;
    int64_t character_id = 0;
    std::string display_name;
    std::string unit_class;
    bool is_knight = false;
    int level = 1;
    nlohmann::json weapons;
    nlohmann::json armor;
    nlohmann::json abilities;
    std::string status = "active";      // active | casualty | retired
    int64_t created_at = 0;

    nlohmann::json to_json() const;
};

// All members for a character, ordered by is_knight desc, then id.
std::vector<retinue_member> load_retinue(sqlite::database& db, int64_t character_id);

// The knight is the character itself. If no knight row exists yet, one is
// created from the character's display name/level on first access.
retinue_member get_or_create_knight(sqlite::database& db, int64_t character_id);

// Active members only (knight first). Ensures the knight row exists.
std::vector<retinue_member> load_active_retinue(sqlite::database& db, int64_t character_id);

// Marks the given members as casualties (status = 'casualty').
void apply_casualties(sqlite::database& db, int64_t character_id,
                      const std::vector<int64_t>& member_ids);

} // namespace retinue_db
