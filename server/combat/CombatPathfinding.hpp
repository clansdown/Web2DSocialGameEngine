#pragma once
#include <nlohmann/json.hpp>
#include <deque>
#include <utility>
#include <vector>

namespace combat {

// A* over the tile_costs grid (rts_design.md §8.1). Plans over STATIC geometry
// only (terrain + movement-blocking obstacles/structures); living units are
// resolved by the hard-body collision pass, never treated as impassable walls
// for planning.
struct pathfinding_grid {
    int width = 0;
    int height = 0;
    std::vector<double> costs;           // per-cell movement cost (0 = impassable)
    std::vector<bool> static_blocked;    // impassable for movement planning
};

// Builds the static grid from map metadata. `tile_costs` (0 = impassable) plus
// any obstacle whose `blocks_movement` is not false (default solid). Missing
// optional fields default to an open grid.
pathfinding_grid build_pathfinding_grid(const nlohmann::json& map_metadata);

// A* from start to goal in normalized 0..1 coordinates. Returns normalized
// cell-center waypoints excluding the start, including the goal, or empty if
// unreachable. A blocked goal resolves to the nearest open tile (the mover
// stops adjacent when the exact spot is impassable). `extra_blocked`, when
// given, is a per-cell bool mask (e.g. live structures) consulted in addition
// to the static grid.
std::deque<std::pair<double, double>> find_path(const pathfinding_grid& grid,
                                                double start_x, double start_y,
                                                double goal_x, double goal_y,
                                                const std::vector<bool>* extra_blocked = nullptr);

// A shared group flow field (rts_design.md §8.1): a distance-to-goal field over
// the passable grid plus a per-cell unit direction. Enemy warband groups advance
// by sampling one shared field instead of each unit planning its own path.
struct flow_field {
    int width = 0;
    int height = 0;
    bool valid = false;
    double goal_x = 0.0;
    double goal_y = 0.0;
    std::vector<double> dist;                    // distance-to-goal (inf = unreachable)
    std::vector<std::pair<double, double>> dir;  // unit direction toward the goal
};

// Builds a flow field toward a goal (normalized 0..1 coordinates). Cells that
// are statically blocked or in `extra_blocked` are unreachable. A blocked goal
// resolves to the nearest open cell.
flow_field build_flow_field(const pathfinding_grid& grid, double goal_x, double goal_y,
                            const std::vector<bool>* extra_blocked = nullptr);

// Samples the descent direction at a normalized point. Returns false when the
// point is at the goal or unreachable.
bool flow_direction_at(const flow_field& field, double x, double y, double& out_dx, double& out_dy);

} // namespace combat