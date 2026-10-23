#pragma once
#include "MissionGenerator.hpp"
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <sqlite_modern_cpp.h>
#include <string>
#include <vector>

class GameConfigCache;

// DB-backed side of the offering system: authenticity/freshness checks, board
// assembly (generation + completion filter), and persistence of completions and
// attempt logs. Offerings themselves are never stored — only completions and
// attempts are.
namespace mission_offerings {

// The character's own retinue as an army summary: summed per-unit power (full
// health) and the unit count. `army_strength` folds these into S.
mission_gen::retinue_army_summary load_retinue_army(sqlite::database& db,
                                                    int64_t character_id,
                                                    GameConfigCache& cache);

// The character's own retinue strength (aggregate over active members;
// S = Σ power × N^(β−1)). Thin wrapper over load_retinue_army.
double character_retinue_power(sqlite::database& db, int64_t character_id, GameConfigCache& cache);

// True when the character is a member of a barony.
bool character_in_barony(sqlite::database& db, int64_t character_id);

// Army summaries of the OTHER members of the character's barony (empty when not
// in one).
std::vector<mission_gen::retinue_army_summary> barony_member_armies(
    sqlite::database& db, int64_t character_id, GameConfigCache& cache);

bool offering_is_completed(sqlite::database& db, int64_t character_id,
                           const std::string& offering_id);

// Authenticity (not eligibility): the id belongs to this character, is within
// the validity window, and its frozen party fields (players / troop count /
// power bucket) are at or below the character's current party (a shrink voids
// older offers).
bool validate_offering_authentic(sqlite::database& db, const mission_gen::offering_id_parts& parts,
                                 int64_t character_id, int64_t now, GameConfigCache& cache);

// The current-hour board for the character (all applicable tiers), minus
// completed offerings.
std::vector<mission_gen::mission_offering> load_board(sqlite::database& db, int64_t character_id,
                                                      int64_t now, GameConfigCache& cache);

// Records a battle start; returns the attempt row id (0 on failure).
int64_t record_attempt_start(sqlite::database& db, const std::string& offering_id,
                             int64_t host_character_id, const std::string& match_id, int64_t now);

// Finalizes an attempt row.
void record_attempt_end(sqlite::database& db, int64_t attempt_id, const std::string& result,
                        const std::string& participants_json,
                        const std::string& warband_end_state_json, int64_t now);

// Marks an offering completed for a character (idempotent).
void record_completion(sqlite::database& db, int64_t character_id, const std::string& offering_id,
                       int64_t now);

// Adds per-class enemy deaths to the character's `slain.<class>` counters.
void increment_counters(sqlite::database& db, int64_t character_id,
                        const std::map<std::string, int>& deaths_by_class, int64_t now);

} // namespace mission_offerings
