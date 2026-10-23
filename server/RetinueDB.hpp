#pragma once
#include "combat/CombatTypes.hpp"
#include <functional>
#include <map>
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
    std::string gender = "male";        // "male" | "female" (name-pool and gender-aware prose)
    std::string headshot_id;            // path under images/retinue/headshots/ (e.g. "male/03.png"); empty for the knight
    double health = 100.0;              // persistent health 0..100, recovers over wall-clock time
    int64_t health_updated = 0;         // unix ts of the last health snapshot
    int priority = 0;                   // strict intra-roster rank (funding + infirmary beds)
    nlohmann::json weapons;
    nlohmann::json armor;
    nlohmann::json equipment;           // generic slot->item map {weapon, armor, mount, potions, ...}
    nlohmann::json abilities;
    std::string status = "active";      // active | casualty | retired (health < min_deploy_hp == infirmary)
    bool maintained = true;             // funded by the economy tick this period (priority order)
    int64_t xp = 0;                     // cumulative RTS experience (§14.1)
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

// Hires a new (non-knight) member for a character. Priority defaults to the
// highest existing priority + 1 (i.e. lowest rank). Returns the created member.
retinue_member hire_member(sqlite::database& db, int64_t character_id,
                           const std::string& display_name, const std::string& unit_class,
                           const std::string& gender, const std::string& headshot_id,
                           int level, int64_t now);

// Reassigns strict priority ranks (0..n, knight keeps 0) from an ordered list
// of member ids. `ordered_member_ids` must be exactly a permutation of the
// character's non-knight member ids (any mismatch writes nothing). Returns
// true on success. This is the player-facing roster reorder (funding order +
// infirmary-bed order), see /api/setRetinuePriority.
bool reorder_priority(sqlite::database& db, int64_t character_id,
                      const std::vector<int64_t>& ordered_member_ids);

// Roster size of a character's retinue, knight excluded (the knight is free).
size_t recruited_member_count(sqlite::database& db, int64_t character_id);

// Marks the given members as casualties (status = 'casualty').
void apply_casualties(sqlite::database& db, int64_t character_id,
                      const std::vector<int64_t>& member_ids);

// Continuous-health settings used by recovery/fielding (retinue.json +
// the fiefdom's infirmary buildings).
struct recovery_settings {
    double max_recovery_hours = 16.0;  // a full 0->100 heal at base rate
    double min_deploy_hp = 50.0;       // below this a member is locked ("infirmary")
    double multiplier = 1.0;           // healing-rate multiplier (infirmary bonus stack)
};

// Advances each member's health toward 100 based on elapsed wall-clock time
// since health_updated at (100 / max_recovery_hours) * multiplier per hour.
// Persists only on full healing (idempotent; partial recovery is re-derived
// from health_updated on the next read).
void apply_recovery(sqlite::database& db, std::vector<retinue_member>& members,
                    int64_t now, double recovery_multiplier = 1.0,
                    double max_recovery_hours = 16.0);

// Wounds (no-death model): forces each member's health down to at most their
// given `health` value (end-of-combat HP%), restarts their recovery clock,
// and keeps them 'active' — they heal; they never die. Used by `wounded`
// combat rulesets. A member whose current health is already below the wound
// value keeps it (and its recovery clock) untouched.
struct wound_spec {
    int64_t member_id = 0;
    double health = 10.0;       // healing floor 0..100 (severity = end HP%)
};
void apply_wounds(sqlite::database& db, int64_t character_id,
                  const std::vector<wound_spec>& wounds, int64_t now = 0);

// A level-up produced by grant_experience (old → new level, capped at the
// member's class `max_level`).
struct level_up_record {
    int64_t member_id = 0;
    int old_level = 1;
    int new_level = 1;
};

// Grants cumulative RTS experience to fielded members (rts_design.md §14.1).
// Adds `xp_gain` to each member's `xp`, recomputes levels against the
// cumulative `xp_curve` (index i = XP to reach level i+2; level 1 = 0 XP), and
// caps each member at `max_level_for_class(unit_class)`. Knights level
// independently in the RTS. Returns the resulting level-ups.
std::vector<level_up_record> grant_experience(
    sqlite::database& db, int64_t character_id,
    const std::map<int64_t, double>& xp_by_member,
    const std::vector<double>& xp_curve,
    const std::function<int(const std::string& unit_class)>& max_level_for_class);

} // namespace retinue_db
