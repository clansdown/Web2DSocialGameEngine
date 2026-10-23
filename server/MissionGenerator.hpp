#pragma once
#include "combat/CombatTypes.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class GameConfigCache;

// Auto-generated, deterministic, per-character PvE mission offerings. An
// offering's content is a PURE function of its id: the id encodes
// (player_id, hour_bucket, tier, index, players, troop_count, power_bucket), so
// the server re-derives an offering exactly without storing it. Only
// completions and attempt logs are persisted (see MissionOfferings.hpp).
//
// Warband assembly (MissionGenerator.cpp): the party's aggregate power S sets a
// target T = rho * S (rho drawn from the seed); a horde/elite coefficient e
// weights the monster classes; every pick rolls its unmet prerequisites and adds
// units at the highest level that still fits under T + p_min. The warband's
// REALIZED power (not the target) drives the danger label and XP.
namespace mission_gen {

struct offering_id_parts {
    int64_t player_id = 0;
    int64_t hour_bucket = 0;
    std::string tier;
    int index = 0;
    int players = 1;            // expected party size frozen at generation time
    int troop_count = 0;        // party unit count frozen at generation time
    uint32_t power_bucket = 0;  // quantized party power (power_scale)
};

struct mission_tier {
    std::string id;
    int min_players = 1;
    int max_players = 1;
    bool requires_barony = false;
};

// One character's retinue priced at full health: the summed per-unit power and
// the unit count (the two inputs to army_strength).
struct retinue_army_summary {
    double sum_power = 0.0;
    int unit_count = 0;
};

// The party a warband is built for (aggregate S, not per-unit power).
struct party_army {
    int expected_players = 1;
    int troop_count = 0;
    double power = 0.0;  // S = (sum power) * N^(beta-1)
};

// Inputs to warband assembly. `seed` is derived from the offering id by the
// caller (make_warband_seed); the generator is a pure function of the spec.
struct warband_spec {
    uint64_t seed = 0;
    int expected_players = 1;
    int party_troop_count = 0;
    double party_power = 0.0;
};

struct assembled_warband {
    std::vector<combat::warband_spawn_entry> entries;
    double difficulty = 1.0;      // rho: T = rho * party_power
    double target_power = 0.0;    // T
    double eliteness = 0.0;       // e: < 0 horde, > 0 elite
    double realized_power = 0.0;  // warband_power(entries)
};

struct mission_offering {
    std::string id;
    std::string tier;
    int index = 0;
    int64_t hour_bucket = 0;
    int players = 1;
    int troop_count = 0;
    uint32_t power_bucket = 0;
    double encounter_power = 0.0;  // target T (kept for the client's preview)
    double target_power = 0.0;
    double realized_power = 0.0;
    double difficulty = 1.0;
    double eliteness = 0.0;
    std::string ruleset;
    std::string map_id;
    std::vector<combat::warband_spawn_entry> warband;
    double xp_base = 0.0;
    std::string danger_band;
    double danger_ratio = 0.0;
    int total_units = 0;

    nlohmann::json to_json() const;
};

// Deterministic splitmix64 PRNG seeded from an offering id.
struct offering_rng {
    uint64_t state = 0;
    uint64_t next();
    double unit();              // [0, 1)
    int range(int lo, int hi);  // inclusive
};

offering_rng make_offering_rng(const offering_id_parts& parts);

// The warband RNG seed. A salted variant of the id hash, so the map pick and the
// warband assembly consume independent streams.
uint64_t make_warband_seed(const offering_id_parts& parts);

std::string encode_offering_id(const offering_id_parts& parts);
bool decode_offering_id(const std::string& id, offering_id_parts& out);

int64_t current_hour_bucket(int64_t now_seconds);

// Power quantizer (the `power_scale` block).
uint32_t quantize_power(double power, const nlohmann::json& offerings_config);
double dequantize_power(uint32_t bucket, const nlohmann::json& offerings_config);

std::vector<mission_tier> parse_tiers(const nlohmann::json& offerings_config);

// Combine the character's retinue with up to (tier.max_players - 1) barony
// members (strongest first) into the party the tier is built for.
party_army party_army_for_tier(const mission_tier& tier,
                               const retinue_army_summary& self_army,
                               const std::vector<retinue_army_summary>& barony_armies,
                               GameConfigCache& cache);

// Assemble an enemy warband for `spec` (pure function of spec + config).
assembled_warband assemble_warband(const warband_spec& spec, GameConfigCache& cache);

// The pure generator: content is a function of the id alone.
mission_offering generate_offering(const offering_id_parts& parts, GameConfigCache& cache);

// The current-hour board for a character (all applicable tiers).
std::vector<mission_offering> generate_board(
    int64_t character_id, int64_t hour_bucket, const retinue_army_summary& self_army,
    const std::vector<retinue_army_summary>& barony_armies, bool in_barony,
    GameConfigCache& cache);

std::string danger_band_for(double ratio, const nlohmann::json& offerings_config);
double xp_for_power(double warband_power, const nlohmann::json& offerings_config);

} // namespace mission_gen
