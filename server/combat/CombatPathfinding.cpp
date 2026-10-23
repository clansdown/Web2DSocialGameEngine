#include "CombatPathfinding.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

namespace combat {

namespace {

int tile_index(const pathfinding_grid& g, int tx, int ty) {
    return ty * g.width + tx;
}

bool in_bounds(const pathfinding_grid& g, int tx, int ty) {
    return tx >= 0 && ty >= 0 && tx < g.width && ty < g.height;
}

// Four-way movement neighbors (no diagonal cut-throughs).
void for_neighbors(const pathfinding_grid& g, int tx, int ty,
                   const std::function<void(int, int)>& fn) {
    if (in_bounds(g, tx + 1, ty)) fn(tx + 1, ty);
    if (in_bounds(g, tx - 1, ty)) fn(tx - 1, ty);
    if (in_bounds(g, tx, ty + 1)) fn(tx, ty + 1);
    if (in_bounds(g, tx, ty - 1)) fn(tx, ty - 1);
}

} // namespace

pathfinding_grid build_pathfinding_grid(const nlohmann::json& map_metadata) {
    pathfinding_grid grid;
    grid.width = map_metadata.value("width_tiles", 0);
    grid.height = map_metadata.value("height_tiles", 0);
    if (grid.width <= 0 || grid.height <= 0) {
        grid.width = 16;
        grid.height = 16;
    }

    const size_t n = static_cast<size_t>(grid.width) * static_cast<size_t>(grid.height);
    grid.costs.assign(n, 1.0);
    grid.static_blocked.assign(n, false);

    if (map_metadata.contains("tile_costs") && map_metadata["tile_costs"].is_array()) {
        const auto& tc = map_metadata["tile_costs"];
        for (size_t i = 0; i < n && i < tc.size(); ++i) {
            if (tc[i].is_number()) {
                grid.costs[i] = tc[i].get<double>();
            }
        }
    }
    for (size_t i = 0; i < n; ++i) {
        grid.static_blocked[i] = grid.costs[i] <= 0.0;
    }

    // Movement-blocking obstacles (blocks_movement defaults to true).
    if (map_metadata.contains("obstacles") && map_metadata["obstacles"].is_array()) {
        for (const auto& obs : map_metadata["obstacles"]) {
            if (!obs.is_object()) continue;
            const bool blocks_movement = obs.value("blocks_movement", true);
            if (!blocks_movement) continue;
            const double ox = obs.value("x", 0.5);
            const double oy = obs.value("y", 0.5);
            const double radius = obs.value("radius", 0.06);
            const int r_tiles = std::max(1, static_cast<int>(std::ceil(radius * grid.width)));
            const int cx = static_cast<int>(ox * grid.width);
            const int cy = static_cast<int>(oy * grid.height);
            for (int ty = cy - r_tiles; ty <= cy + r_tiles; ++ty) {
                for (int tx = cx - r_tiles; tx <= cx + r_tiles; ++tx) {
                    if (!in_bounds(grid, tx, ty)) continue;
                    const double px = (tx + 0.5) / grid.width;
                    const double py = (ty + 0.5) / grid.height;
                    const double dx = px - ox;
                    const double dy = py - oy;
                    if (std::sqrt(dx * dx + dy * dy) <= radius) {
                        grid.static_blocked[tile_index(grid, tx, ty)] = true;
                    }
                }
            }
        }
    }

    return grid;
}

std::deque<std::pair<double, double>> find_path(const pathfinding_grid& grid,
                                                double start_x, double start_y,
                                                double goal_x, double goal_y,
                                                const std::vector<bool>* extra_blocked) {
    std::deque<std::pair<double, double>> empty;
    if (grid.width <= 0 || grid.height <= 0) return empty;

    const auto extra = [&](int idx) -> bool {
        return extra_blocked && idx >= 0 && idx < static_cast<int>(extra_blocked->size())
                   ? (*extra_blocked)[idx]
                   : false;
    };

    const int start_tx = std::clamp(static_cast<int>(start_x * grid.width), 0, grid.width - 1);
    const int start_ty = std::clamp(static_cast<int>(start_y * grid.height), 0, grid.height - 1);
    int goal_tx = std::clamp(static_cast<int>(goal_x * grid.width), 0, grid.width - 1);
    int goal_ty = std::clamp(static_cast<int>(goal_y * grid.height), 0, grid.height - 1);
    const int n = grid.width * grid.height;

    const auto blocked_tile = [&](int tx, int ty) -> bool {
        if (!in_bounds(grid, tx, ty)) return true;
        return grid.static_blocked[tile_index(grid, tx, ty)] || extra(tile_index(grid, tx, ty));
    };

    // If the goal tile is blocked, walk to the nearest open tile instead.
    if (blocked_tile(goal_tx, goal_ty)) {
        std::vector<int> parent(n, -1);  // recompute nearest via BFS
        std::deque<int> q;
        q.push_back(tile_index(grid, goal_tx, goal_ty));
        parent[q.front()] = q.front();
        bool found = false;
        while (!q.empty() && !found) {
            const int cur = q.front();
            q.pop_front();
            const int cx = cur % grid.width;
            const int cy = cur / grid.width;
            if (!blocked_tile(cx, cy)) {
                goal_tx = cx;
                goal_ty = cy;
                found = true;
            } else {
                for_neighbors(grid, cx, cy, [&](int nx, int ny) {
                    const int idx = tile_index(grid, nx, ny);
                    if (parent[idx] == -1) {
                        parent[idx] = cur;
                        q.push_back(idx);
                    }
                });
            }
        }
        if (!found) return empty;   // no reachable open tile anywhere
    }

    if (start_tx == goal_tx && start_ty == goal_ty) return empty;

    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> g_score(n, inf);
    std::vector<double> f_score(n, inf);
    std::vector<int> came_from(n, -1);
    std::vector<bool> closed(n, false);

    auto h = [&](int tx, int ty) -> double {
        return static_cast<double>(std::abs(tx - goal_tx) + std::abs(ty - goal_ty));
    };
    auto cell_center = [&](int tx, int ty) -> std::pair<double, double> {
        return {(tx + 0.5) / grid.width, (ty + 0.5) / grid.height};
    };

    struct pq_entry {
        double f;
        int idx;
    };
    auto cmp = [](const pq_entry& a, const pq_entry& b) { return a.f > b.f; };
    std::priority_queue<pq_entry, std::vector<pq_entry>, decltype(cmp)> open(cmp);

    const int start_idx = tile_index(grid, start_tx, start_ty);
    g_score[start_idx] = 0.0;
    f_score[start_idx] = h(start_tx, start_ty);
    open.push({f_score[start_idx], start_idx});

    bool reached = false;
    while (!open.empty()) {
        const int cur = open.top().idx;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = true;
        const int cx = cur % grid.width;
        const int cy = cur / grid.width;
        if (cx == goal_tx && cy == goal_ty) {
            reached = true;
            break;
        }
        for_neighbors(grid, cx, cy, [&](int nx, int ny) {
            const int idx = tile_index(grid, nx, ny);
            if (closed[idx] || blocked_tile(nx, ny)) return;
            const double step_cost = grid.costs[idx] > 0.0 ? grid.costs[idx] : 1.0;
            const double tentative = g_score[cur] + step_cost;
            if (tentative < g_score[idx]) {
                came_from[idx] = cur;
                g_score[idx] = tentative;
                f_score[idx] = tentative + h(nx, ny);
                open.push({f_score[idx], idx});
            }
        });
    }

    if (!reached) return empty;

    // Reconstruct path goal → start, then reverse.
    std::vector<int> tile_path;
    int cur = tile_index(grid, goal_tx, goal_ty);
    while (cur != start_idx && came_from[cur] != -1) {
        tile_path.push_back(cur);
        cur = came_from[cur];
    }
    std::reverse(tile_path.begin(), tile_path.end());

    std::deque<std::pair<double, double>> waypoints;
    for (int idx : tile_path) {
        waypoints.push_back(cell_center(idx % grid.width, idx / grid.width));
    }
    // Guarantee the final waypoint is the exact goal (so the mover touches it).
    if (waypoints.empty()) return empty;
    waypoints.back() = {goal_x, goal_y};
    return waypoints;
}

flow_field build_flow_field(const pathfinding_grid& grid, double goal_x, double goal_y,
                            const std::vector<bool>* extra_blocked) {
    // Dijkstra outward from the goal over passable cells; then each cell's
    // direction points at its cheapest neighbour (the descent toward the goal).
    flow_field f;
    f.width = grid.width;
    f.height = grid.height;
    f.goal_x = goal_x;
    f.goal_y = goal_y;
    if (grid.width <= 0 || grid.height <= 0) return f;

    const auto extra = [&](int idx) -> bool {
        return extra_blocked && idx >= 0 && idx < static_cast<int>(extra_blocked->size())
                   ? (*extra_blocked)[idx]
                   : false;
    };
    const int n = grid.width * grid.height;
    const auto blocked_tile = [&](int tx, int ty) -> bool {
        if (!in_bounds(grid, tx, ty)) return true;
        const int idx = tile_index(grid, tx, ty);
        return grid.static_blocked[idx] || extra(idx);
    };

    int goal_tx = std::clamp(static_cast<int>(goal_x * grid.width), 0, grid.width - 1);
    int goal_ty = std::clamp(static_cast<int>(goal_y * grid.height), 0, grid.height - 1);
    if (blocked_tile(goal_tx, goal_ty)) {
        // Nearest open cell to the requested goal (BFS).
        std::vector<int> parent(n, -1);
        std::deque<int> q;
        q.push_back(tile_index(grid, goal_tx, goal_ty));
        parent[q.front()] = q.front();
        bool found = false;
        while (!q.empty() && !found) {
            const int cur = q.front();
            q.pop_front();
            const int cx = cur % grid.width;
            const int cy = cur / grid.width;
            if (!blocked_tile(cx, cy)) {
                goal_tx = cx;
                goal_ty = cy;
                found = true;
            } else {
                for_neighbors(grid, cx, cy, [&](int nx, int ny) {
                    const int idx = tile_index(grid, nx, ny);
                    if (parent[idx] == -1) {
                        parent[idx] = cur;
                        q.push_back(idx);
                    }
                });
            }
        }
        if (!found) return f;   // no reachable open cell
    }

    const double inf = std::numeric_limits<double>::infinity();
    f.dist.assign(n, inf);
    f.dir.assign(n, {0.0, 0.0});

    struct entry { double d; int idx; };
    auto cmp = [](const entry& a, const entry& b) { return a.d > b.d; };
    std::priority_queue<entry, std::vector<entry>, decltype(cmp)> pq(cmp);
    const int goal_idx = tile_index(grid, goal_tx, goal_ty);
    f.dist[goal_idx] = 0.0;
    pq.push({0.0, goal_idx});

    while (!pq.empty()) {
        const entry e = pq.top();
        pq.pop();
        const int cur = e.idx;
        if (e.d > f.dist[cur]) continue;
        const int cx = cur % grid.width;
        const int cy = cur / grid.width;
        for_neighbors(grid, cx, cy, [&](int nx, int ny) {
            if (blocked_tile(nx, ny)) return;
            const int idx = tile_index(grid, nx, ny);
            const double step = grid.costs[idx] > 0.0 ? grid.costs[idx] : 1.0;
            const double nd = e.d + step;
            if (nd < f.dist[idx]) {
                f.dist[idx] = nd;
                pq.push({nd, idx});
            }
        });
    }

    bool any_reachable = false;
    for (int ty = 0; ty < grid.height; ++ty) {
        for (int tx = 0; tx < grid.width; ++tx) {
            const int idx = tile_index(grid, tx, ty);
            if (blocked_tile(tx, ty) || !std::isfinite(f.dist[idx]) || idx == goal_idx) continue;
            const double cxp = (tx + 0.5) / grid.width;
            const double cyp = (ty + 0.5) / grid.height;
            double best = f.dist[idx];
            int best_idx = -1;
            for_neighbors(grid, tx, ty, [&](int nx, int ny) {
                if (blocked_tile(nx, ny)) return;
                const int nidx = tile_index(grid, nx, ny);
                if (f.dist[nidx] < best) {
                    best = f.dist[nidx];
                    best_idx = nidx;
                }
            });
            if (best_idx >= 0) {
                const double nxp = (best_idx % grid.width + 0.5) / grid.width;
                const double nyp = (best_idx / grid.width + 0.5) / grid.height;
                const double dx = nxp - cxp;
                const double dy = nyp - cyp;
                const double len = std::sqrt(dx * dx + dy * dy);
                if (len > 1e-9) f.dir[idx] = {dx / len, dy / len};
                any_reachable = true;
            }
        }
    }
    f.valid = any_reachable || (goal_tx >= 0 && goal_ty >= 0);
    return f;
}

bool flow_direction_at(const flow_field& field, double x, double y,
                       double& out_dx, double& out_dy) {
    if (!field.valid || field.width <= 0 || field.height <= 0) return false;
    const int tx = std::clamp(static_cast<int>(x * field.width), 0, field.width - 1);
    const int ty = std::clamp(static_cast<int>(y * field.height), 0, field.height - 1);
    const int idx = ty * field.width + tx;
    if (idx < 0 || idx >= static_cast<int>(field.dir.size())) return false;
    const auto d = field.dir[idx];
    if (std::abs(d.first) > 1e-9 || std::abs(d.second) > 1e-9) {
        out_dx = d.first;
        out_dy = d.second;
        return true;
    }
    // At the goal cell (or a dead end): steer the last step directly.
    const double gx = field.goal_x - x;
    const double gy = field.goal_y - y;
    const double len = std::sqrt(gx * gx + gy * gy);
    if (len <= 0.01) return false;
    out_dx = gx / len;
    out_dy = gy / len;
    return true;
}

} // namespace combat