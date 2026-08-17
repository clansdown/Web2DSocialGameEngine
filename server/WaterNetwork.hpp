#pragma once
#include <string>
#include <unordered_map>
#include <set>
#include <utility>
#include <nlohmann/json.hpp>
#include "FiefdomData.hpp"

namespace Water {

// Result of computing water power across a fiefdom's water infrastructure.
struct WaterPowerResult {
    std::unordered_map<int, bool> powered;     // water-building id -> powered
    std::unordered_map<int, int>  powered_by;  // water-building id -> pond id (0 = none)
    std::unordered_map<int, int>  pond_load;   // pond id -> count of powered buildings
};

// Computes, for each water-powered building, whether it is powered by the
// manor's water network (see .cpp for the full model):
//   river -> (adjacent) mill pond -> head race -> building -> tail race -> river
// A building is powered iff a head race traces orthogonally from an active
// pond's footprint to the building AND a tail race traces from the river to the
// building, AND the assigned pond still has spare capacity for its type.
WaterPowerResult computeWaterPower(
    const nlohmann::json& building_types,
    const std::vector<BuildingData>& buildings,
    const std::set<std::pair<int, int>>& river_cells);

} // namespace Water