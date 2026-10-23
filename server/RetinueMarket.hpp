#pragma once
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

// A single named hire offer on the recruit market.
struct recruit_candidate {
    std::string unit_class;
    std::string display_name;
    std::string gender;      // "male" | "female"
    std::string headshot_id; // path under images/retinue/headshots/ (e.g. "male/03.png")
    int level = 1;
    int hour_bucket = 0;      // hour bucket this offer belongs to
    int64_t expires_at = 0;   // unix ts when the offer is no longer hirable
    nlohmann::json fee;       // per-resource hire fee at the candidate's level

    nlohmann::json to_json() const;
};

// Deterministic generation + structural validation of the recruit market. See
// server/tables/retinue_members.md -> "Recruit market" and docs/retinue_design.md.
//
// The market is seeded from (character_id, hour_bucket) only, so re-generating
// a bucket reproduces the exact same offers for a given roster. Names and
// headshots are re-rolled deterministically past identities already used by the
// character (roster names + headshots) and past the rest of the displayed
// cohort, so the normal UI never shows a duplicate. Validation is structural
// only (class/level/bucket + pool membership) — see the plan's decision to drop
// the exact-offer regeneration and the name-uniqueness gate.
namespace retinue_market {

// Highest candidate level a manor of the given level can attract for a class
// with `class_max_level` (manor level + configured offset, clamped).
int max_candidate_level(const nlohmann::json& retinue_config, int manor_level,
                        int class_max_level);

// Generates one hour bucket's candidates. `used_names` (lowercased) and
// `used_headshots` are consulted for uniqueness and are updated in place as
// candidates are emitted, so callers generating several buckets against one set
// get a de-duplicated combined list.
std::vector<recruit_candidate> candidates_for_bucket(
    const nlohmann::json& player_combatants,
    const nlohmann::json& retinue_config,
    int64_t character_id, int hour_bucket, int manor_level,
    const std::string& text_dir,
    std::set<std::string>& used_names,
    std::set<std::string>& used_headshots);

// Current-hour offers plus the grace (previous-hour) cohort, generated against
// one shared uniqueness set. Fills the current hour bucket, the next refresh ts
// (end of the current hour), and each candidate's expiry (end of its grace
// window).
std::vector<recruit_candidate> list_candidates(
    const nlohmann::json& player_combatants,
    const nlohmann::json& retinue_config,
    int64_t character_id, int64_t now, int manor_level,
    const std::string& text_dir,
    std::set<std::string>& used_names,
    std::set<std::string>& used_headshots,
    int& current_bucket, int64_t& next_refresh_at);

// True if `candidate` is structurally valid for this character at this manor
// level: known + unlocked class, in-range level, current-or-previous hour
// bucket, valid gender, a name from the class/sex pool, and an existing
// headshot for that sex. It intentionally does NOT re-derive the exact offer or
// enforce name uniqueness (a scripted client that hires a duplicate name or
// headshot is harmless because class/level — and therefore the fee and stats —
// remain fully validated).
bool is_valid_candidate(const nlohmann::json& player_combatants,
                        const nlohmann::json& retinue_config,
                        int manor_level,
                        const std::string& text_dir,
                        const recruit_candidate& candidate);

// Per-resource hire fee for a class at a level (linear extension of `costs`).
nlohmann::json compute_recruit_fee_for(const nlohmann::json& class_config, int level);

} // namespace retinue_market
