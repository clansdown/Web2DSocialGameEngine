#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// A single named hire offer on the recruit market.
struct recruit_candidate {
    std::string unit_class;
    std::string display_name;
    std::string gender;      // "male" | "female"
    int level = 1;
    int hour_bucket = 0;      // hour bucket this offer belongs to
    int64_t expires_at = 0;   // unix ts when the offer is no longer hirable
    nlohmann::json fee;       // per-resource hire fee at the candidate's level

    nlohmann::json to_json() const;
};

// Deterministic, roster-independent generation + validation of the recruit
// market. See server/tables/retinue_members.md -> "Recruit market".
//
// The market is seeded from (character_id, hour_bucket) only, so re-generating
// a bucket always reproduces the exact same offers. This lets the server verify
// that a submitted hire was a real offer without storing any market state.
namespace retinue_market {

// Highest candidate level a manor of the given level can attract for a class
// with `class_max_level` (manor level + configured offset, clamped).
int max_candidate_level(const nlohmann::json& retinue_config, int manor_level,
                        int class_max_level);

// Regenerates the candidate pool for one hour bucket (roster-independent).
std::vector<recruit_candidate> candidates_for_bucket(
    const nlohmann::json& player_combatants,
    const nlohmann::json& retinue_config,
    int64_t character_id, int hour_bucket, int manor_level,
    const std::string& text_dir);

// Current-hour offers plus the grace (previous-hour) cohort. Fills the current
// hour bucket, the next refresh ts (end of the current hour), and each
// candidate's expiry (end of its grace window).
std::vector<recruit_candidate> list_candidates(
    const nlohmann::json& player_combatants,
    const nlohmann::json& retinue_config,
    int64_t character_id, int64_t now, int manor_level,
    const std::string& text_dir, int& current_bucket, int64_t& next_refresh_at);

// True if `candidate` was a genuine offer for this character at this manor
// level, in the current or previous hour bucket (grace window).
bool is_valid_candidate(const nlohmann::json& player_combatants,
                        const nlohmann::json& retinue_config,
                        int64_t character_id, int manor_level,
                        const std::string& text_dir,
                        const recruit_candidate& candidate);

// Per-resource hire fee for a class at a level (linear extension of `costs`).
nlohmann::json compute_recruit_fee_for(const nlohmann::json& class_config, int level);

} // namespace retinue_market