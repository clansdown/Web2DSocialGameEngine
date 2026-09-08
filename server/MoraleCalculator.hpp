#pragma once
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "FiefdomData.hpp"

class GameConfigCache;

namespace Morale {

// Computes per-building morale points from the road network (see .cpp).
std::unordered_map<int, double> computeRoadMoralePoints(
    const nlohmann::json& building_types,
    const std::vector<BuildingData>& buildings
);

// Derived household morale (never stored — computed from state). Many small
// contributors (chapel buildings, funded/unfunded retinue, recent battle win/
// loss decaying from stored timestamps) sum into a capped percent bonus. See
// docs/retinue_design.md §9.
struct household_morale_result {
    double percent = 0.0;      // production/recovery bonus 0..max_bonus_percent
    double points = 0.0;       // raw contributor points
    int chapel_points = 0;
    int funded_count = 0;
    int unfunded_count = 0;
    double victory_decay = 0.0;
    double defeat_decay = 0.0;
};

household_morale_result computeHouseholdMorale(
    const nlohmann::json& building_types,
    const nlohmann::json& retinue_config,
    const std::vector<BuildingData>& buildings,
    int funded, int unfunded,
    int64_t now, int64_t last_victory_ts, int64_t last_defeat_ts
);

} // namespace Morale
