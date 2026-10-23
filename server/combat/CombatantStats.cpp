#include "CombatantStats.hpp"
#include <algorithm>

namespace combat {

namespace {

// rts_design.md §5.1 combatant stat-extraction helpers. Moved verbatim from
// CombatMatch.cpp so the simulation and the strength formula share one
// implementation of the extrapolation rules.

double num_of(const nlohmann::json& obj, const std::string& key, double def = 0.0) {
    if (obj.is_object() && obj.contains(key) && obj[key].is_number()) {
        return obj[key].get<double>();
    }
    return def;
}

// Per-level object entry (damage = no nulls; defense = nullable entries),
// following the damage/defense extrapolation convention.
nlohmann::json level_object(const nlohmann::json& arr, int level, bool nullable) {
    nlohmann::json empty = nlohmann::json::object();
    if (!arr.is_array() || arr.empty()) return empty;
    const int idx = level - 1;
    auto usable = [&](int i) -> bool {
        return i >= 0 && i < static_cast<int>(arr.size()) && !arr[i].is_null();
    };
    if (!nullable && idx >= 0 && idx < static_cast<int>(arr.size())) {
        return arr[idx].is_object() ? arr[idx] : empty;
    }
    if (usable(idx)) return arr[idx];
    // Most recent two usable entries for extrapolation.
    int last = -1, prev = -1;
    for (int i = std::min(idx, static_cast<int>(arr.size()) - 1); i >= 0; --i) {
        if (arr[i].is_null()) continue;
        if (last == -1) {
            last = i;
        } else if (prev == -1) {
            prev = i;
            break;
        }
    }
    if (last == -1) return empty;
    if (prev == -1) return arr[last];
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [k, v] : arr[last].items()) {
        if (!v.is_number()) continue;
        const double lv = v.get<double>();
        const double pv = num_of(arr[prev], k, lv);
        out[k] = lv + (idx - last) * (lv - pv);
    }
    return out;
}

// Level-indexed scalar array with linear extrapolation past the last entry
// (rts_design.md §5.1). `def` is returned for a missing field / negative level.
double level_array_value(const nlohmann::json& combatant, const std::string& field,
                         int level, double def) {
    if (!combatant.is_object() || !combatant.contains(field) || !combatant[field].is_array() ||
        combatant[field].empty()) {
        return def;
    }
    const auto& arr = combatant[field];
    const int idx = level - 1;
    if (idx >= 0 && idx < static_cast<int>(arr.size())) {
        return arr[idx].is_number() ? arr[idx].get<double>() : def;
    }
    if (idx < 0) return def;
    if (arr.size() < 2) {
        return arr.back().is_number() ? arr.back().get<double>() : def;
    }
    const double last = arr[arr.size() - 1].is_number() ? arr[arr.size() - 1].get<double>() : def;
    const double prev = arr[arr.size() - 2].is_number() ? arr[arr.size() - 2].get<double>() : def;
    return last + (idx - static_cast<int>(arr.size() - 1)) * (last - prev);
}

} // namespace

combat_unit_stats resolve_combatant_stats(const nlohmann::json& combatants,
                                          const std::string& class_id, int level) {
    combat_unit_stats s;
    if (!combatants.contains(class_id) || !combatants[class_id].is_object()) return s;
    const auto& c = combatants[class_id];

    if (c.contains("damage")) {
        const nlohmann::json d = level_object(c["damage"], level, false);
        s.melee_damage = num_of(d, "melee");
        s.ranged_damage = num_of(d, "ranged");
        s.magical_damage = num_of(d, "magical");
    }
    if (c.contains("defense")) {
        const nlohmann::json df = level_object(c["defense"], level, true);
        s.melee_defense = num_of(df, "melee");
        s.ranged_defense = num_of(df, "ranged");
        s.magical_defense = num_of(df, "magical");
    }

    s.max_hp = level_array_value(c, "max_hp", level, 100.0);
    s.attack_range = level_array_value(c, "attack_range", level, 0.1);
    s.attack_interval = level_array_value(c, "attack_interval", level, 1.5);
    s.movement_speed = level_array_value(c, "movement_speed", level, 1.0);
    s.turn_rate = num_of(c, "turn_rate", 3.0);
    // Body size (normalized): the required per-class `size` drives both the sim
    // collision radius and the client's drawn sprite size.
    s.radius = num_of(c, "size", k_default_unit_radius);

    const std::string atype = c.value("attack_type", "");
    if (!atype.empty()) {
        s.attack_type = atype;
    } else {
        // D5: default = highest damage stat.
        if (s.magical_damage >= s.melee_damage && s.magical_damage >= s.ranged_damage) {
            s.attack_type = "magical";
        } else if (s.ranged_damage >= s.melee_damage) {
            s.attack_type = "ranged";
        } else {
            s.attack_type = "melee";
        }
    }

    if (c.contains("direction_defense") && c["direction_defense"].is_object()) {
        s.direction_defense_front = c["direction_defense"].value("front", 1.0);
        s.direction_defense_side = c["direction_defense"].value("side", 0.6);
        s.direction_defense_back = c["direction_defense"].value("back", 0.35);
    }
    if (c.contains("dodge") && c["dodge"].is_object()) {
        s.dodge_front = c["dodge"].value("front", 0.0);
        s.dodge_side = c["dodge"].value("side", 0.0);
        s.dodge_back = c["dodge"].value("back", 0.0);
    }
    if (c.contains("aoe") && c["aoe"].is_object()) {
        s.is_aoe = c["aoe"].value("splash", false);
        s.splash_radius = c["aoe"].value("splash_radius", 0.0);
    }
    return s;
}

int combatant_max_level(const nlohmann::json& combatants, const std::string& class_id) {
    if (!combatants.is_object() || class_id.empty()) return 1;
    const auto it = combatants.find(class_id);
    if (it == combatants.end() || !it->is_object()) return 1;
    return std::clamp(it->value("max_level", 20), 1, 100);
}

} // namespace combat
