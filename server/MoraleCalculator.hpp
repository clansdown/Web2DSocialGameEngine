#pragma once
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "FiefdomData.hpp"

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

double calculateWallMorale(const std::vector<WallData>& walls);

double calculateFiefdomMorale(
    int fiefdom_id,
    const std::vector<BuildingData>& buildings,
    const std::vector<WallData>& walls,
    const std::vector<OfficialData>& officials,
    const std::vector<FiefdomHero>& heroes,
    const std::vector<StationedCombatant>& combatants
);

} // namespace Morale