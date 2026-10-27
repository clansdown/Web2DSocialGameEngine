#pragma once

#include "FiefdomData.hpp"
#include "ReeveConfig.hpp"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

class GameConfigCache;

namespace Reeve {

/// A road link that may be needed to connect a footprint to the road network.
struct RoadLink {
    bool needed = false; // false = the target already touches a road
    bool found = false;  // true = cells contains a valid path
    std::vector<std::pair<int, int>> cells;
};

/// Finds a deterministic building site, preferring a dangling road end (so a
/// speculative road is followed through) and then road adjacency.
std::optional<std::pair<int, int>> find_building_site(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::string& building_type,
    const ReeveStyle& style,
    int max_scan_radius = 64);

/// Finds a site adjacent to a dangling road end for the requested building.
std::optional<std::pair<int, int>> find_dangling_road_target(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::string& building_type,
    const ReeveStyle& style);

/// Scores a site for deterministic selection (road adjacency, corridor
/// reservation, distance from the manor).
int score_building_site(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::string& building_type,
    int x,
    int y,
    const ReeveStyle& style);

/// True if a cell belongs to a reserved road corridor.
bool is_reserved_corridor_cell(int x, int y, int spacing);

/// True if any cell of a footprint is a reserved corridor cell.
bool footprint_overlaps_reserved_corridor(
    int x, int y, int width, int height, int spacing);

/// True if a road cell has no orthogonally adjacent non-road building.
bool is_dangling_road_end(
    const FiefdomData& fiefdom,
    GameConfigCache& cache,
    int x,
    int y);

/// True if a building footprint touches at least one road cell.
bool building_touches_road(
    const FiefdomData& fiefdom,
    GameConfigCache& cache,
    int building_id);

/// True if a site footprint touches at least one road cell.
bool site_touches_road(
    const FiefdomData& fiefdom,
    GameConfigCache& cache,
    int x,
    int y,
    int width,
    int height);

/// Builds a road link from the nearest existing road (or the manor house's
/// perimeter when no road exists) to a target footprint.
RoadLink find_road_link_to_site(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    int target_x,
    int target_y,
    int target_width,
    int target_height,
    int max_length,
    int corridor_spacing = 6);

/// Builds a road link to an existing building's footprint.
RoadLink find_road_link_to_building(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    int building_id,
    int max_length,
    int corridor_spacing = 6);

/// Fast placement check (overlap, river, walls, map bounds). The full action
/// validator is still run by the Reeve before an action is emitted.
bool placement_is_clear(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::string& building_type,
    int x,
    int y);

} // namespace Reeve
