#pragma once
#include <string>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>

namespace GameLogic {
namespace GridCollision {

struct Rect {
    int x;
    int y;
    int width;
    int height;

    Rect(int x, int y, int width, int height) : x(x), y(y), width(width), height(height) {}

    inline bool overlaps(const Rect& other) const {
        return (x < other.x + other.width && x + width > other.x &&
                y < other.y + other.height && y + height > other.y);
    }

    inline bool containsPoint(int px, int py) const {
        return (px >= x && px < x + width && py >= y && py < y + height);
    }
};

struct PlacementCheck {
    bool valid = true;
    std::vector<int> overlapping_building_ids;
    std::string error_message;
};

struct BuildingDimensions {
    int width = 1;
    int height = 1;
    bool valid = false;
};

struct WallDimensions {
    int width = 0;
    int length = 0;
    int thickness = 0;
};

PlacementCheck checkPlacement(
    int fiefdom_id,
    const std::string& building_type,
    int x,
    int y,
    bool check_home_base_position = true,
    int exclude_building_id = 0
);

BuildingDimensions getBuildingDimensions(const std::string& building_type);

std::pair<int, int> getBuildingDimensionsPair(const std::string& building_type);

bool isValidPosition(int x, int y);

PlacementCheck checkPlacementWithExisting(
    const std::string& building_type,
    int x,
    int y,
    const std::vector<nlohmann::json>& existing_buildings
);

int getMaxBuildingSize();

WallDimensions getWallDimensions(int generation);

std::optional<nlohmann::json> getWallConfigByGeneration(int generation);

bool overlapsWalls(int fiefdom_id, int generation, int x, int y, int building_width, int building_height);

std::vector<nlohmann::json> getOverlappingBuildings(int fiefdom_id, int generation, int x, int y, int building_width, int building_height);

} // namespace GridCollision
} // namespace GameLogic