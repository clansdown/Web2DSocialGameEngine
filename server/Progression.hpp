#pragma once
#include <nlohmann/json.hpp>
#include <sqlite_modern_cpp.h>
#include <cstdint>
#include <string>
#include <vector>

// Progression gates that are not tied to the mini-game track: the freehold →
// mesne-lord qualification (manor level + RTS milestones) and the tenure
// cooldown. Config lives in game/config/progression.json.
namespace progression {

// The letter sent when a freeholder becomes eligible to apply to be a mesne lord.
inline constexpr const char* k_mesne_lord_letter_key = "letter_mesne_lord_eligible";

struct mesne_lord_requirements {
    int manor_level = 0;
    std::vector<std::string> milestones;
};

// Parses progression.json's "mesne_lord.requirements" block.
mesne_lord_requirements parse_mesne_lord_requirements(const nlohmann::json& progression_config);

// Barony join cooldown in seconds from "barony.join_cooldown_days" (default 3
// days). 0 disables the cooldown.
int join_cooldown_seconds(const nlohmann::json& progression_config);

// The character's manor-house level (0 when they own no fiefdom).
int get_manor_level(sqlite::database& db, int character_id);

// True when the character owns a fiefdom (manor play is available).
bool has_fiefdom(sqlite::database& db, int character_id);

// Milestone helpers (e.g. "ettin_slain").
bool has_milestone(sqlite::database& db, int character_id, const std::string& milestone_id);
void grant_milestone(sqlite::database& db, int character_id,
                     const std::string& milestone_id, int64_t now);

// A counter-based milestone condition (progression.json "milestones"): grant
// `id` once the character's `counter` reaches `min` (e.g. slain.ettin >= 1).
struct milestone_condition {
    std::string id;
    std::string counter;
    int min = 1;
};

// Parses progression.json's "milestones" array.
std::vector<milestone_condition> parse_milestone_conditions(const nlohmann::json& progression_config);

// The character's current value for a namespaced counter (0 when absent).
int get_counter(sqlite::database& db, int character_id, const std::string& counter_key);

// Grants every condition already satisfied by the character's counters
// (idempotent per character+milestone).
void evaluate_milestone_conditions(sqlite::database& db, int character_id,
                                   const std::vector<milestone_condition>& conditions, int64_t now);

// True when the character meets every requirement (manor level + milestones).
bool meets_mesne_lord_requirements(sqlite::database& db, int character_id,
                                   const mesne_lord_requirements& reqs);

// If the character is a freeholder who now meets the requirements, promotes
// them to `freehold_qualified` and sends the king's letter exactly once.
// Returns true only on the transition.
bool check_freehold_qualification(sqlite::database& db, int character_id, int64_t now,
                                  const mesne_lord_requirements& reqs);

} // namespace progression
