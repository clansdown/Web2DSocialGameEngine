#include "TDGoldCalculator.hpp"
#include "TDPlacementValidator.hpp"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <iostream>

using json = nlohmann::json;

GoldCalculationResult calculateGoldForRound(
    int session_gold,
    int gold_earned,
    const json& old_placements,
    const json& new_placements,
    const json& towers_config,
    const json& units_config,
    const json& map_metadata)
{
    GoldCalculationResult result;
    result.placement_cost = 0;
    result.refund = 0;

    // Build lookup maps keyed by placement id
    std::unordered_map<std::string, json> old_by_id;
    for (auto& p : old_placements) {
        std::string id = p.value("id", "");
        if (!id.empty()) old_by_id[id] = p;
    }

    std::unordered_map<std::string, json> new_by_id;
    for (auto& p : new_placements) {
        std::string id = p.value("id", "");
        if (!id.empty()) new_by_id[id] = p;
    }

    std::unordered_set<std::string> seen;

    // Process new placements (not in old set)
    for (auto& p : new_placements) {
        std::string id = p.value("id", "");
        if (id.empty()) continue;
        seen.insert(id);

        auto old_it = old_by_id.find(id);
        if (old_it == old_by_id.end()) {
            // Brand new placement — charge full cost
            std::string config_id = p.value("config_id", "");
            double cost = getPlacementCost(towers_config, units_config, config_id);
            result.placement_cost += static_cast<int>(cost);

            // Validate position
            double x = p["x"].get<double>();
            double y = p["y"].get<double>();
            double exclusion_r = p.value("exclusion_radius", 0.0);
            auto vr = validateTDPlacement(x, y, map_metadata, new_placements, id, exclusion_r);
            if (!vr.valid) {
                result.error = "Invalid placement '" + id + "' (" + config_id + "): " + vr.error;
                return result;
            }
        } else {
            // Existing placement — check for upgrade
            std::string old_config = old_it->second.value("config_id", "");
            std::string new_config = p.value("config_id", "");
            if (old_config != new_config) {
                // Upgrade: check if old config's 'becomes' matches new config
                double upgradeCost = 0.0;
                bool found_upgrade = false;

                auto check_upgrade = [&](const json& configs, const std::string& cfg_key) {
                    if (!configs.contains(cfg_key)) return;
                    auto& cat = configs[cfg_key];
                    if (cat.contains(old_config)) {
                        auto& entry = cat[old_config];
                        if (entry.value("becomes", "") == new_config &&
                            entry.contains("upgrade_cost") &&
                            entry["upgrade_cost"].is_array() &&
                            !entry["upgrade_cost"].empty())
                        {
                            upgradeCost = entry["upgrade_cost"][0].value("gold", 0);
                            found_upgrade = true;
                        }
                    }
                };

                check_upgrade(towers_config, "towers");
                if (!found_upgrade) check_upgrade(units_config, "units");

                if (found_upgrade) {
                    result.placement_cost += static_cast<int>(upgradeCost);
                } else {
                    // Not a valid upgrade — charge full new cost
                    double cost = getPlacementCost(towers_config, units_config, new_config);
                    result.placement_cost += static_cast<int>(cost);
                }
            }
            // else: same config_id → no cost (move or unchanged)
            // Validate new position if moved (x or y changed)
            double old_x = old_it->second["x"].get<double>();
            double old_y = old_it->second["y"].get<double>();
            double new_x = p["x"].get<double>();
            double new_y = p["y"].get<double>();
            if (old_x != new_x || old_y != new_y) {
                double exclusion_r = p.value("exclusion_radius", 0.0);
                auto vr = validateTDPlacement(new_x, new_y, map_metadata, new_placements, id, exclusion_r);
                if (!vr.valid) {
                    result.error = "Invalid move for placement '" + id + "': " + vr.error;
                    return result;
                }
            }
        }
    }

    // Process removals (in old set but not in new set)
    for (auto& [id, p] : old_by_id) {
        if (seen.count(id)) continue;
        // Unit was removed — refund 50%
        std::string config_id = p.value("config_id", "");
        double cost = getPlacementCost(towers_config, units_config, config_id);
        result.refund += static_cast<int>(cost * 0.5);
    }

    result.new_gold = std::max(0, session_gold + gold_earned - result.placement_cost + result.refund);

    std::cerr << "[GoldCalculator] session_gold=" << session_gold
              << " gold_earned=" << gold_earned
              << " placement_cost=" << result.placement_cost
              << " refund=" << result.refund
              << " new_gold=" << result.new_gold
              << std::endl;

    return result;
}
