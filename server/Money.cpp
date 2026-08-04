#include "Money.hpp"
#include <cmath>

namespace money {

long gold_to_pence(double gold) {
    if (gold <= 0) return 0;
    return static_cast<long>(std::llround(gold * pence_per_gold));
}

void normalize(double& gold, int& silver_pence) {
    long total_pence = gold_to_pence(gold) + static_cast<long>(silver_pence);
    if (total_pence < 0) total_pence = 0;
    gold = static_cast<double>(total_pence / static_cast<long>(pence_per_gold));
    silver_pence = static_cast<int>(total_pence % static_cast<long>(pence_per_gold));
}

// Convert a single gold_cost element (number or {gold, shillings, pence} object)
// into a gold double. Non-object entries (plain numbers) pass through unchanged.
static void normalize_gold_cost_entry(nlohmann::json& entry) {
    if (!entry.is_object()) return;
    double gold = entry.value("gold", 0.0);
    double shillings = entry.value("shillings", 0.0);
    double pence = entry.value("pence", 0.0);
    entry = gold + shillings / shillings_per_pound + pence / pence_per_gold;
}

// Normalize the gold_cost array of a single building or wall config object.
static void normalize_gold_cost_array(nlohmann::json& cfg) {
    if (!cfg.is_object() || !cfg.contains("gold_cost") || !cfg["gold_cost"].is_array()) {
        return;
    }
    for (auto& entry : cfg["gold_cost"]) {
        normalize_gold_cost_entry(entry);
    }
}

void normalize_money_costs(nlohmann::json& cfg) {
    // fiefdom_building_types.json: an array of { type_id: {...} } objects
    if (cfg.is_array()) {
        for (auto& building_entry : cfg) {
            if (!building_entry.is_object()) continue;
            for (auto& [type_id, building_cfg] : building_entry.items()) {
                (void)type_id;
                normalize_gold_cost_array(building_cfg);
            }
        }
        return;
    }

    // wall_config.json: { "walls": { "1": {...}, ... } }
    if (cfg.is_object() && cfg.contains("walls") && cfg["walls"].is_object()) {
        for (auto& [generation, wall_cfg] : cfg["walls"].items()) {
            (void)generation;
            normalize_gold_cost_array(wall_cfg);
        }
    }
}

} // namespace money
