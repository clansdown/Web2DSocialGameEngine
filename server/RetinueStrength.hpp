#pragma once
#include "RetinueDB.hpp"
#include "combat/CombatTypes.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class GameConfigCache;

// Generic combat-power primitive. Used by mission generation (both sides),
// strength-derived XP, and the danger estimate — and later by matchmaking and
// the balance analyzer.
//
// Per-unit power is TTK-accurate (offense × effective HP), and an army's
// strength folds in the N-vs-1 effect:
//
//     dps      = damage[attack_type] / attack_interval
//     def_eff  = (w_melee·defense.melee + w_ranged·defense.ranged
//                 + w_magical·defense.magical) × direction_defense.front
//     ehp      = max_hp × (100 + def_eff)/100 / (1 − dodge.front)
//     power    = dps × ehp × (is_aoe ? aoe_multiplier : 1)
//                × (1 + k_range × max(0, attack_range − melee_ref))
//     S        = (Σ power) × N^(β−1)          β = 2, N = total unit count
//
// Stats are resolved through the SAME resolver the simulation uses
// (combat/CombatantStats.hpp), so the two can never drift. See
// server/docs/combat_system.md → "Strength" and docs/rts_design.md §14.1.
namespace retinue_strength {

// Tuning for the strength formula, read from the `encounter` block of
// combat/offerings.json (load_strength_tuning). Defaults match the shipped
// config so the formula is well-defined even without config.
struct strength_tuning {
    double engagement_exponent = 2.0;    // β: 1 = linear/duel, 2 = square/all-engage
    double defense_weight_melee = 0.5;
    double defense_weight_ranged = 0.3;
    double defense_weight_magical = 0.2;
    double aoe_multiplier = 1.5;         // splash offense bonus
    double ranged_bonus_per_unit = 1.0;  // k_range: offense bonus per unit of range
    double ranged_melee_ref = 0.07;      // melee_ref: range treated as "melee" baseline
};

// Read the tuning constants from the offerings config's `encounter` block,
// falling back to the struct defaults for anything missing.
strength_tuning load_strength_tuning(const nlohmann::json& offerings_config);

// Power of one combatant of `unit_class` at `level`, resolved from a combatant
// config (player or enemy) through the shared stat resolver. `equipment` is an
// optional slot->item map; it is currently a no-op hook (gear has no combat
// effect yet — TODO(gear)).
double combatant_power(const nlohmann::json& combatants, const std::string& unit_class,
                       int level, const strength_tuning& tuning,
                       const nlohmann::json& equipment = nlohmann::json());

// Aggregate army strength from a summed per-unit power and the total unit count:
// S = summed_power × N^(β−1). Reduces to linear (β=1) or square (β=2).
double army_strength(double summed_power, int unit_count, const strength_tuning& tuning);

// Power of one retinue member (class + level). Full health only — the anchor
// always prices a member at 100% (a wounded retinue is intentionally
// over-estimated here; the sim's HP bridge still weakens it in battle).
double member_power(const retinue_db::retinue_member& member, GameConfigCache& cache,
                    const strength_tuning& tuning);

// Total strength of a retinue (aggregate over its members).
double retinue_power(const std::vector<retinue_db::retinue_member>& members,
                     GameConfigCache& cache, const strength_tuning& tuning);

// Total strength of an enemy warband (aggregate over its spawn entries).
double warband_power(const std::vector<combat::warband_spawn_entry>& warband,
                     GameConfigCache& cache, const strength_tuning& tuning);

} // namespace retinue_strength
