#include "GridCollision.hpp"
#include "GameConfigCache.hpp"
#include "FiefdomFetcher.hpp"
#include "Database.hpp"
#include <algorithm>

namespace GameLogic {
namespace GridCollision {

BuildingDimensions getBuildingDimensions(const std::string& building_type) {
    auto& cache = GameConfigCache::getInstance();
    auto types = cache.getFiefdomBuildingTypes();

    for (const auto& type_obj : types) {
        if (type_obj.contains(building_type)) {
            auto config = type_obj[building_type];
            BuildingDimensions dims;
            dims.width = config.value("width", 1);
            dims.height = config.value("height", 1);
            dims.valid = true;
            return dims;
        }
    }

    return BuildingDimensions();
}

std::pair<int, int> getBuildingDimensionsPair(const std::string& building_type) {
    auto dims = getBuildingDimensions(building_type);
    return {dims.width, dims.height};
}

bool isValidPosition(int x, int y) {
    const int MAX_RANGE = 1000;
    return (x >= -MAX_RANGE && x <= MAX_RANGE && y >= -MAX_RANGE && y <= MAX_RANGE);
}

PlacementCheck checkPlacementWithExisting(
    const std::string& building_type,
    int x,
    int y,
    const std::vector<nlohmann::json>& existing_buildings
) {
    PlacementCheck result;

    auto newDims = getBuildingDimensions(building_type);
    if (!newDims.valid) {
        result.valid = false;
        result.error_message = "Unknown building type: " + building_type;
        return result;
    }

    Rect newRect(x, y, newDims.width, newDims.height);

    for (const auto& existing : existing_buildings) {
        if (!existing.contains("id") || !existing.contains("name") || !existing.contains("x") || !existing.contains("y")) {
            continue;
        }

        auto [ew, eh] = getBuildingDimensionsPair(existing["name"]);
        Rect existingRect(existing["x"].get<int>(), existing["y"].get<int>(), ew, eh);

        if (newRect.overlaps(existingRect)) {
            result.valid = false;
            result.overlapping_building_ids.push_back(existing["id"].get<int>());
        }
    }

    if (!result.valid && result.overlapping_building_ids.empty()) {
        result.error_message = "Cannot build at this location";
    } else if (!result.valid) {
        result.error_message = "Location overlaps with existing buildings";
    }

    return result;
}

PlacementCheck checkPlacement(
    int fiefdom_id,
    const std::string& building_type,
    int x,
    int y,
    bool check_home_base_position
) {
    PlacementCheck result;

    if (!isValidPosition(x, y)) {
        result.valid = false;
        result.error_message = "Position is outside the valid range";
        return result;
    }

    if (building_type == "home_base" && check_home_base_position) {
        if (x != 0 || y != 0) {
            result.valid = false;
            result.error_message = "Manor House (home_base) must be built at location (0, 0)";
            return result;
        }
    }

    auto newDims = getBuildingDimensions(building_type);
    if (!newDims.valid) {
        result.valid = false;
        result.error_message = "Unknown building type: " + building_type;
        return result;
    }

    auto& db = Database::getInstance().gameDB();
    Rect newRect(x, y, newDims.width, newDims.height);

    std::vector<nlohmann::json> existingBuildings;
    db << "SELECT id, name, level, x, y FROM fiefdom_buildings WHERE fiefdom_id = ?;"
       << fiefdom_id
       >> [&](int id, std::string name, int level, int bx, int by) {
           nlohmann::json b;
           b["id"] = id;
           b["name"] = name;
           b["level"] = level;
           b["x"] = bx;
           b["y"] = by;
           existingBuildings.push_back(b);
       };

    for (const auto& existing : existingBuildings) {
        auto [ew, eh] = getBuildingDimensionsPair(existing["name"]);
        Rect existingRect(existing["x"].get<int>(), existing["y"].get<int>(), ew, eh);

        if (newRect.overlaps(existingRect)) {
            result.valid = false;
            result.overlapping_building_ids.push_back(existing["id"].get<int>());
        }
    }

    if (!result.valid && result.overlapping_building_ids.empty()) {
        result.error_message = "Cannot build at this location";
    } else if (!result.valid) {
        result.error_message = "Location overlaps with existing buildings";
    }

    return result;
}

int getMaxBuildingSize() {
    return 32;
}

} // namespace GridCollision
} // namespace GameLogic