#include "RetinueStrength.hpp"
#include "GameConfigCache.hpp"
#include "combat/CombatantStats.hpp"
#include <algorithm>
#include <cmath>

namespace retinue_strength {

namespace {

// Damage per attack for the unit's chosen attack type (rts_design.md §5.1).
double damage_for(const combat::combat_unit_stats& s) {
    if (s.attack_type == "ranged") return s.ranged_damage;
    if (s.attack_type == "magical") return s.magical_damage;
    return s.melee_damage;
}

} // namespace

strength_tuning load_strength_tuning(const nlohmann::json& offerings_config) {
    strength_tuning t;
    if (!offerings_config.is_object()) return t;
    const nlohmann::json enc = offerings_config.value("encounter", nlohmann::json::object());
    if (!enc.is_object()) return t;
    t.engagement_exponent =
        std::max(1.0, enc.value("engagement_exponent", t.engagement_exponent));
    t.aoe_multiplier = std::max(0.0, enc.value("aoe_multiplier", t.aoe_multiplier));
    t.ranged_bonus_per_unit =
        std::max(0.0, enc.value("ranged_bonus_per_unit", t.ranged_bonus_per_unit));
    t.ranged_melee_ref = std::max(0.0, enc.value("ranged_melee_ref", t.ranged_melee_ref));
    const nlohmann::json dw = enc.value("defense_weights", nlohmann::json::object());
    if (dw.is_object()) {
        t.defense_weight_melee = std::max(0.0, dw.value("melee", t.defense_weight_melee));
        t.defense_weight_ranged = std::max(0.0, dw.value("ranged", t.defense_weight_ranged));
        t.defense_weight_magical = std::max(0.0, dw.value("magical", t.defense_weight_magical));
    }
    return t;
}

double combatant_power(const nlohmann::json& combatants, const std::string& unit_class,
                       int level, const strength_tuning& tuning,
                       const nlohmann::json& /*equipment*/) {
    if (unit_class.empty()) return 0.0;
    const combat::combat_unit_stats s =
        combat::resolve_combatant_stats(combatants, unit_class, level);

    // Offense: damage per second for the unit's chosen attack type.
    const double dps = damage_for(s) / std::max(1e-3, s.attack_interval);

    // Defense: weighted average across damage types, front arc only.
    const double def_avg = tuning.defense_weight_melee * s.melee_defense +
                           tuning.defense_weight_ranged * s.ranged_defense +
                           tuning.defense_weight_magical * s.magical_defense;
    const double def_eff = def_avg * s.direction_defense_front;

    // Effective HP: mitigation by defense, then dodge as a per-hit negate.
    const double mitigation = (100.0 + def_eff) / 100.0;
    const double dodge_avoid = std::max(1e-3, 1.0 - std::clamp(s.dodge_front, 0.0, 0.99));
    const double ehp = std::max(0.0, s.max_hp) * mitigation / dodge_avoid;

    // Splash offense bonus (AoE).
    const double aoe = s.is_aoe ? tuning.aoe_multiplier : 1.0;

    // Ranged engagement bonus: reach beyond the melee baseline gets to fire
    // first / more often; linear in range above melee_ref.
    const double engagement =
        1.0 + tuning.ranged_bonus_per_unit * std::max(0.0, s.attack_range - tuning.ranged_melee_ref);

    return dps * ehp * aoe * engagement;
}

double army_strength(double summed_power, int unit_count, const strength_tuning& tuning) {
    if (unit_count <= 0 || summed_power <= 0.0) return 0.0;
    const double beta = std::max(1.0, tuning.engagement_exponent);
    return summed_power * std::pow(static_cast<double>(unit_count), beta - 1.0);
}

double member_power(const retinue_db::retinue_member& member, GameConfigCache& cache,
                    const strength_tuning& tuning) {
    return combatant_power(cache.getPlayerCombatants(), member.unit_class, member.level, tuning);
}

double retinue_power(const std::vector<retinue_db::retinue_member>& members,
                     GameConfigCache& cache, const strength_tuning& tuning) {
    double summed = 0.0;
    for (const auto& m : members) summed += member_power(m, cache, tuning);
    return army_strength(summed, static_cast<int>(members.size()), tuning);
}

double warband_power(const std::vector<combat::warband_spawn_entry>& warband,
                     GameConfigCache& cache, const strength_tuning& tuning) {
    double summed = 0.0;
    int total = 0;
    for (const auto& e : warband) {
        if (e.count <= 0) continue;
        summed += combatant_power(cache.getEnemyCombatants(), e.unit_class, e.level, tuning) *
                  static_cast<double>(e.count);
        total += e.count;
    }
    return army_strength(summed, total, tuning);
}

} // namespace retinue_strength
