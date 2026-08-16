#pragma once
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "FiefdomData.hpp"

class GameConfigCache;

namespace Morale {

enum class EffectMode {
    Add,
    Max,
    Multiply
};

EffectMode parseMode(const std::string& mode_str);
double clampMorale(double value);

double calculateBuildingMorale(
    const std::string& building_name,
    int building_count,
    const nlohmann::json& building_config
);

double calculateWallMorale(GameConfigCache& cache, const std::vector<WallData>& walls);

double calculateFiefdomMorale(
    GameConfigCache& cache,
    int fiefdom_id,
    const std::vector<BuildingData>& buildings,
    const std::vector<WallData>& walls,
    const std::vector<OfficialData>& officials,
    const std::vector<FiefdomHero>& heroes,
    const std::vector<StationedCombatant>& combatants
);

// Computes per-building morale points from the road network (see .cpp).
std::unordered_map<int, double> computeRoadMoralePoints(
    const nlohmann::json& building_types,
    const std::vector<BuildingData>& buildings
);

} // namespace Morale
