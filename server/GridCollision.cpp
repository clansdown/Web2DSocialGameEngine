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
    bool check_home_base_position,
    int exclude_building_id
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
    if (exclude_building_id > 0) {
        db << "SELECT id, name, level, x, y FROM fiefdom_buildings WHERE fiefdom_id = ? AND id != ?;"
           << fiefdom_id << exclude_building_id
           >> [&](int id, std::string name, int level, int bx, int by) {
               nlohmann::json b;
               b["id"] = id;
               b["name"] = name;
               b["level"] = level;
               b["x"] = bx;
               b["y"] = by;
               existingBuildings.push_back(b);
           };
    } else {
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
    }

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

WallDimensions getWallDimensions(int generation) {
    WallDimensions dims;
    auto config_opt = getWallConfigByGeneration(generation);
    if (!config_opt) return dims;

    auto config = *config_opt;
    dims.width = config.value("width", 0);
    dims.length = config.value("length", 0);
    dims.thickness = config.value("thickness", 0);
    return dims;
}

std::optional<nlohmann::json> getWallConfigByGeneration(int generation) {
    auto& cache = GameConfigCache::getInstance();
    if (!cache.isLoaded()) return std::nullopt;

    auto config = cache.getAllConfigs();
    if (!config.contains("wall_config") || !config["wall_config"].is_object()) return std::nullopt;

    auto wall_config = config["wall_config"];
    if (!wall_config.contains("walls") || !wall_config["walls"].is_object()) return std::nullopt;

    auto walls = wall_config["walls"];
    std::string gen_key = std::to_string(generation);
    if (walls.contains(gen_key)) return walls[gen_key];

    return std::nullopt;
}

bool overlapsWalls(int fiefdom_id, int generation, int x, int y, int building_width, int building_height) {
    WallDimensions dims = getWallDimensions(generation);
    if (dims.width == 0 || dims.length == 0 || dims.thickness == 0) return false;

    int half_w = dims.width / 2;
    int half_l = dims.length / 2;
    int thick = dims.thickness;

    Rect building(x, y, building_width, building_height);

    std::vector<Rect> wall_rects;
    wall_rects.push_back(Rect(-half_w, half_l, dims.width, thick));           // North
    wall_rects.push_back(Rect(-half_w, -half_l - thick, dims.width, thick));  // South
    wall_rects.push_back(Rect(half_w, -half_l, thick, dims.length));         // East
    wall_rects.push_back(Rect(-half_w - thick, -half_l, thick, dims.length)); // West

    for (const auto& wall_rect : wall_rects) {
        if (building.overlaps(wall_rect)) return true;
    }

    return false;
}

std::vector<nlohmann::json> getOverlappingBuildings(int fiefdom_id, int generation, int x, int y, int building_width, int building_height) {
    std::vector<nlohmann::json> overlapping;

    auto& db = Database::getInstance().gameDB();
    db << "SELECT id, name, level, x, y FROM fiefdom_buildings WHERE fiefdom_id = ? AND level > 0;"
       << fiefdom_id
       >> [&](int id, std::string name, int level, int bx, int by) {
               auto dims = getBuildingDimensionsPair(name);
               int bw = dims.first;
               int bh = dims.second;
               if (overlapsWalls(fiefdom_id, generation, bx, by, bw, bh)) {
               nlohmann::json b;
               b["id"] = id;
               b["name"] = name;
               b["level"] = level;
               b["x"] = bx;
               b["y"] = by;
               overlapping.push_back(b);
           }
       };

    return overlapping;
}

} // namespace GridCollision
} // namespace GameLogic