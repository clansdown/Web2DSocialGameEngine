#include "ReevePlacement.hpp"

#include "FiefdomFetcher.hpp"
#include "GameConfigCache.hpp"
#include "GridCollision.hpp"

#include <algorithm>
#include <cmath>
#include <climits>
#include <deque>
#include <map>
#include <set>

namespace Reeve {
namespace {

constexpr int kMaxSiteScanRadius = 64;

struct BuildingFootprint {
    int id = 0;
    std::string type;
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
};

bool is_road_or_race(const std::string& type) {
    return type == "road" || type == "head_race" || type == "tail_race";
}

std::vector<BuildingFootprint> footprints(GameConfigCache& cache, const FiefdomData& fiefdom) {
    std::vector<BuildingFootprint> out;
    for (const auto& building : fiefdom.buildings) {
        auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building.name);
        BuildingFootprint fp;
        fp.id = building.id;
        fp.type = building.name;
        fp.x = building.x;
        fp.y = building.y;
        fp.width = dims.valid ? dims.width : 1;
        fp.height = dims.valid ? dims.height : 1;
        out.push_back(std::move(fp));
    }
    return out;
}

bool cell_in_footprint(int x, int y, const BuildingFootprint& fp) {
    return x >= fp.x && x < fp.x + fp.width &&
           y >= fp.y && y < fp.y + fp.height;
}

bool cell_in_any_footprint(int x, int y, const std::vector<BuildingFootprint>& buildings,
                           bool include_roads = true) {
    for (const auto& fp : buildings) {
        if (!include_roads && is_road_or_race(fp.type)) continue;
        if (cell_in_footprint(x, y, fp)) return true;
    }
    return false;
}

bool cell_has_road(const FiefdomData& fiefdom, GameConfigCache& cache, int x, int y) {
    for (const auto& building : fiefdom.buildings) {
        if (building.name != "road") continue;
        auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building.name);
        int w = dims.valid ? dims.width : 1;
        int h = dims.valid ? dims.height : 1;
        if (x >= building.x && x < building.x + w &&
            y >= building.y && y < building.y + h) {
            return true;
        }
    }
    return false;
}

std::set<std::pair<int, int>> river_cells(const FiefdomData& fiefdom) {
    std::set<std::pair<int, int>> cells;
    for (const auto& [x, y] : FiefdomFetcher::fetchRiverCells(fiefdom.id)) {
        cells.insert({x, y});
    }
    return cells;
}

bool wall_overlap(GameConfigCache& cache, const FiefdomData& fiefdom,
                  int x, int y, int width, int height) {
    for (const auto& wall : fiefdom.walls) {
        if (GameLogic::GridCollision::overlapsWalls(
                cache, fiefdom.id, wall.generation, x, y, width, height)) {
            return true;
        }
    }
    return false;
}

std::pair<int, int> manor_center(GameConfigCache& cache, const FiefdomData& fiefdom) {
    for (const auto& building : fiefdom.buildings) {
        if (building.name != "home_base") continue;
        auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building.name);
        int w = dims.valid ? dims.width : 1;
        int h = dims.valid ? dims.height : 1;
        return {building.x + w / 2, building.y + h / 2};
    }
    return {0, 0};
}

bool is_free_cell_for_road(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::vector<BuildingFootprint>& buildings,
    const std::set<std::pair<int, int>>& rivers,
    int x,
    int y) {
    if (cell_has_road(fiefdom, cache, x, y)) return true;
    if (cell_in_any_footprint(x, y, buildings)) return false;
    if (rivers.count({x, y})) return false;
    if (wall_overlap(cache, fiefdom, x, y, 1, 1)) return false;
    return true;
}

std::optional<std::pair<int, int>> first_clear_candidate(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::string& building_type,
    const std::vector<std::pair<int, int>>& candidates,
    bool allow_corridor,
    int spacing) {
    for (const auto& [x, y] : candidates) {
        if (!allow_corridor) {
            auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building_type);
            int w = dims.valid ? dims.width : 1;
            int h = dims.valid ? dims.height : 1;
            if (footprint_overlaps_reserved_corridor(x, y, w, h, spacing)) continue;
        }
        if (placement_is_clear(cache, fiefdom, building_type, x, y)) {
            return std::make_pair(x, y);
        }
    }
    return std::nullopt;
}

} // namespace

bool is_reserved_corridor_cell(int x, int y, int spacing) {
    if (spacing < 1) return false;
    int mx = ((x % spacing) + spacing) % spacing;
    int my = ((y % spacing) + spacing) % spacing;
    return mx == 0 || my == 0;
}

bool footprint_overlaps_reserved_corridor(int x, int y, int width, int height, int spacing) {
    for (int cx = x; cx < x + width; ++cx) {
        for (int cy = y; cy < y + height; ++cy) {
            if (is_reserved_corridor_cell(cx, cy, spacing)) return true;
        }
    }
    return false;
}

bool placement_is_clear(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::string& building_type,
    int x,
    int y) {
    auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building_type);
    if (!dims.valid) return false;
    if (!GameLogic::GridCollision::isValidPosition(x, y)) return false;
    if (!GameLogic::GridCollision::isValidPosition(x + dims.width - 1, y + dims.height - 1)) {
        return false;
    }

    auto buildings = footprints(cache, fiefdom);
    for (const auto& fp : buildings) {
        bool overlap = x < fp.x + fp.width && x + dims.width > fp.x &&
                       y < fp.y + fp.height && y + dims.height > fp.y;
        if (overlap) return false;
    }

    auto rivers = river_cells(fiefdom);
    for (int cx = x; cx < x + dims.width; ++cx) {
        for (int cy = y; cy < y + dims.height; ++cy) {
            if (rivers.count({cx, cy})) return false;
        }
    }

    if (wall_overlap(cache, fiefdom, x, y, dims.width, dims.height)) return false;
    return true;
}

bool site_touches_road(
    const FiefdomData& fiefdom,
    GameConfigCache& cache,
    int x,
    int y,
    int width,
    int height) {
    for (int cy = y; cy < y + height; ++cy) {
        if (cell_has_road(fiefdom, cache, x - 1, cy)) return true;
        if (cell_has_road(fiefdom, cache, x + width, cy)) return true;
    }
    for (int cx = x; cx < x + width; ++cx) {
        if (cell_has_road(fiefdom, cache, cx, y - 1)) return true;
        if (cell_has_road(fiefdom, cache, cx, y + height)) return true;
    }
    return false;
}

bool building_touches_road(
    const FiefdomData& fiefdom,
    GameConfigCache& cache,
    int building_id) {
    for (const auto& building : fiefdom.buildings) {
        if (building.id != building_id) continue;
        auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building.name);
        int w = dims.valid ? dims.width : 1;
        int h = dims.valid ? dims.height : 1;
        return site_touches_road(fiefdom, cache, building.x, building.y, w, h);
    }
    return false;
}

bool is_dangling_road_end(
    const FiefdomData& fiefdom,
    GameConfigCache& cache,
    int x,
    int y) {
    if (!cell_has_road(fiefdom, cache, x, y)) return false;
    auto buildings = footprints(cache, fiefdom);
    const std::pair<int, int> neighbours[] = {
        {x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}
    };

    int road_neighbours = 0;
    for (const auto& [nx, ny] : neighbours) {
        if (cell_has_road(fiefdom, cache, nx, ny)) ++road_neighbours;
    }
    // A dangling end is an endpoint of the road network, not an interior tile.
    if (road_neighbours > 1) return false;

    for (const auto& [nx, ny] : neighbours) {
        for (const auto& fp : buildings) {
            if (fp.type == "home_base" && cell_in_footprint(nx, ny, fp)) {
                // Do not anchor a speculative road's start (the manor edge) as
                // if it were the intended destination.
                return false;
            }
        }
        if (cell_in_any_footprint(nx, ny, buildings, /*include_roads=*/false)) {
            return false;
        }
    }
    return true;
}

std::optional<std::pair<int, int>> find_dangling_road_target(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::string& building_type,
    const ReeveStyle& style) {
    auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building_type);
    if (!dims.valid) return std::nullopt;

    std::vector<std::pair<int, int>> roads;
    for (const auto& building : fiefdom.buildings) {
        if (building.name == "road") roads.push_back({building.x, building.y});
    }
    auto [mx, my] = manor_center(cache, fiefdom);
    std::sort(roads.begin(), roads.end(), [&](const auto& a, const auto& b) {
        int da = std::max(std::abs(a.first - mx), std::abs(a.second - my));
        int db = std::max(std::abs(b.first - mx), std::abs(b.second - my));
        if (da != db) return da > db;
        if (a.second != b.second) return a.second < b.second;
        return a.first < b.first;
    });

    for (const auto& [rx, ry] : roads) {
        if (!is_dangling_road_end(fiefdom, cache, rx, ry)) continue;

        std::vector<std::pair<int, int>> candidates;
        // Above the road.
        for (int x = rx - dims.width + 1; x <= rx; ++x) {
            candidates.push_back({x, ry + 1});
        }
        // Below the road.
        for (int x = rx - dims.width + 1; x <= rx; ++x) {
            candidates.push_back({x, ry - dims.height});
        }
        // Left of the road.
        for (int y = ry - dims.height + 1; y <= ry; ++y) {
            candidates.push_back({rx + 1, y});
        }
        // Right of the road.
        for (int y = ry - dims.height + 1; y <= ry; ++y) {
            candidates.push_back({rx - dims.width, y});
        }

        if (auto found = first_clear_candidate(
                cache, fiefdom, building_type, candidates, false, style.corridor_spacing)) {
            return found;
        }
        if (auto found = first_clear_candidate(
                cache, fiefdom, building_type, candidates, true, style.corridor_spacing)) {
            return found;
        }
    }
    return std::nullopt;
}

int score_building_site(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::string& building_type,
    int x,
    int y,
    const ReeveStyle& style) {
    auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building_type);
    if (!dims.valid) return INT_MIN;

    int road_adjacency = 0;
    for (int cx = x - 1; cx <= x + dims.width; ++cx) {
        for (int cy = y - 1; cy <= y + dims.height; ++cy) {
            bool inside = cx >= x && cx < x + dims.width &&
                          cy >= y && cy < y + dims.height;
            if (inside) continue;
            if (cell_has_road(fiefdom, cache, cx, cy)) ++road_adjacency;
        }
    }

    int corridor_overlap = 0;
    for (int cx = x; cx < x + dims.width; ++cx) {
        for (int cy = y; cy < y + dims.height; ++cy) {
            if (is_reserved_corridor_cell(cx, cy, style.corridor_spacing)) {
                ++corridor_overlap;
            }
        }
    }

    auto [origin_x, origin_y] = manor_center(cache, fiefdom);
    int distance = std::max(std::abs(x - origin_x), std::abs(y - origin_y));
    return road_adjacency * 10 - corridor_overlap * 2 - distance / 2;
}

std::optional<std::pair<int, int>> find_building_site(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::string& building_type,
    const ReeveStyle& style,
    int max_scan_radius) {
    auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building_type);
    if (!dims.valid) return std::nullopt;
    if (max_scan_radius < 1) max_scan_radius = kMaxSiteScanRadius;

    if (auto dangling = find_dangling_road_target(cache, fiefdom, building_type, style)) {
        return dangling;
    }

    auto [origin_x, origin_y] = manor_center(cache, fiefdom);
    auto buildings = footprints(cache, fiefdom);
    auto rivers = river_cells(fiefdom);
    std::set<std::pair<int, int>> roads;
    for (const auto& fp : buildings) {
        if (fp.type != "road") continue;
        for (int cx = fp.x; cx < fp.x + fp.width; ++cx) {
            for (int cy = fp.y; cy < fp.y + fp.height; ++cy) {
                roads.insert({cx, cy});
            }
        }
    }

    auto clear_site = [&](int x, int y) -> bool {
        if (!GameLogic::GridCollision::isValidPosition(x, y)) return false;
        if (!GameLogic::GridCollision::isValidPosition(
                x + dims.width - 1, y + dims.height - 1)) {
            return false;
        }
        for (const auto& fp : buildings) {
            bool overlap = x < fp.x + fp.width && x + dims.width > fp.x &&
                           y < fp.y + fp.height && y + dims.height > fp.y;
            if (overlap) return false;
        }
        for (int cx = x; cx < x + dims.width; ++cx) {
            for (int cy = y; cy < y + dims.height; ++cy) {
                if (rivers.count({cx, cy})) return false;
            }
        }
        if (wall_overlap(cache, fiefdom, x, y, dims.width, dims.height)) return false;
        return true;
    };

    struct Best {
        int score = INT_MIN;
        int distance = INT_MAX;
        int x = 0;
        int y = 0;
        bool valid = false;
    };

    auto scan = [&](bool allow_corridor) -> Best {
        Best best;
        for (int x = origin_x - max_scan_radius;
             x <= origin_x + max_scan_radius; ++x) {
            for (int y = origin_y - max_scan_radius;
                 y <= origin_y + max_scan_radius; ++y) {
                if (!allow_corridor &&
                    footprint_overlaps_reserved_corridor(
                        x, y, dims.width, dims.height, style.corridor_spacing)) {
                    continue;
                }
                if (!clear_site(x, y)) continue;
                int road_adjacency = 0;
                for (int cx = x; cx < x + dims.width; ++cx) {
                    if (roads.count({cx, y - 1})) ++road_adjacency;
                    if (roads.count({cx, y + dims.height})) ++road_adjacency;
                }
                for (int cy = y; cy < y + dims.height; ++cy) {
                    if (roads.count({x - 1, cy})) ++road_adjacency;
                    if (roads.count({x + dims.width, cy})) ++road_adjacency;
                }
                int corridor_overlap = 0;
                for (int cx = x; cx < x + dims.width; ++cx) {
                    for (int cy = y; cy < y + dims.height; ++cy) {
                        if (is_reserved_corridor_cell(cx, cy, style.corridor_spacing)) {
                            ++corridor_overlap;
                        }
                    }
                }
                int distance = std::max(std::abs(x - origin_x), std::abs(y - origin_y));
                int score = road_adjacency * 10 - corridor_overlap * 2 - distance / 2;
                bool better = !best.valid ||
                              score > best.score ||
                              (score == best.score && distance < best.distance) ||
                              (score == best.score && distance == best.distance && y < best.y) ||
                              (score == best.score && distance == best.distance && y == best.y && x < best.x);
                if (better) {
                    best.valid = true;
                    best.score = score;
                    best.distance = distance;
                    best.x = x;
                    best.y = y;
                }
            }
        }
        return best;
    };

    Best best = scan(/*allow_corridor=*/false);
    if (!best.valid) best = scan(/*allow_corridor=*/true);
    if (!best.valid) return std::nullopt;
    return std::make_pair(best.x, best.y);
}

RoadLink find_road_link_to_site(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    int target_x,
    int target_y,
    int target_width,
    int target_height,
    int max_length,
    int corridor_spacing) {
    RoadLink result;
    if (site_touches_road(fiefdom, cache, target_x, target_y, target_width, target_height)) {
        result.needed = false;
        result.found = true;
        return result;
    }
    result.needed = true;
    if (max_length <= 0) return result;

    auto buildings = footprints(cache, fiefdom);
    auto rivers = river_cells(fiefdom);

    auto in_target_footprint = [&](int x, int y) {
        return x >= target_x && x < target_x + target_width &&
               y >= target_y && y < target_y + target_height;
    };

    std::set<std::pair<int, int>> targets;
    for (int x = target_x; x < target_x + target_width; ++x) {
        targets.insert({x, target_y - 1});
        targets.insert({x, target_y + target_height});
    }
    for (int y = target_y; y < target_y + target_height; ++y) {
        targets.insert({target_x - 1, y});
        targets.insert({target_x + target_width, y});
    }

    std::deque<std::pair<int, int>> queue;
    std::set<std::pair<int, int>> visited;
    std::map<std::pair<int, int>, std::pair<int, int>> parent;
    std::map<std::pair<int, int>, int> distance;

    bool has_existing_road = false;
    for (const auto& building : fiefdom.buildings) {
        if (building.name != "road") continue;
        has_existing_road = true;
        std::pair<int, int> cell{building.x, building.y};
        if (visited.count(cell)) continue;
        visited.insert(cell);
        parent[cell] = cell;
        distance[cell] = 0;
        queue.push_back(cell);
    }

    if (!has_existing_road) {
        // No road yet: start from free cells around the manor house.
        for (const auto& building : fiefdom.buildings) {
            if (building.name != "home_base") continue;
            auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building.name);
            int w = dims.valid ? dims.width : 1;
            int h = dims.valid ? dims.height : 1;
            for (int x = building.x - 1; x <= building.x + w; ++x) {
                for (int y = building.y - 1; y <= building.y + h; ++y) {
                    bool inside = x >= building.x && x < building.x + w &&
                                  y >= building.y && y < building.y + h;
                    if (inside) continue;
                    bool adjacent = (x == building.x - 1 || x == building.x + w ||
                                     y == building.y - 1 || y == building.y + h);
                    if (!adjacent) continue;
                    if (!is_free_cell_for_road(cache, fiefdom, buildings, rivers, x, y)) continue;
                    std::pair<int, int> cell{x, y};
                    if (visited.count(cell)) continue;
                    visited.insert(cell);
                    parent[cell] = cell;
                    distance[cell] = 1;
                    queue.push_back(cell);
                }
            }
        }
    }

    if (queue.empty()) return result;

    std::pair<int, int> found_target{-1, -1};
    while (!queue.empty()) {
        auto [x, y] = queue.front();
        queue.pop_front();
        int dist = distance[{x, y}];

        if (targets.count({x, y}) && !cell_has_road(fiefdom, cache, x, y)) {
            found_target = {x, y};
            break;
        }

        if (dist >= max_length) continue;

        std::vector<std::pair<int, int>> neighbours = {
            {x, y - 1}, {x + 1, y}, {x, y + 1}, {x - 1, y}
        };
        // Prefer corridor cells for deterministic, tidy road networks.
        std::sort(neighbours.begin(), neighbours.end(), [corridor_spacing](const auto& a, const auto& b) {
            bool ca = is_reserved_corridor_cell(a.first, a.second, corridor_spacing);
            bool cb = is_reserved_corridor_cell(b.first, b.second, corridor_spacing);
            if (ca != cb) return ca > cb;
            if (a.second != b.second) return a.second < b.second;
            return a.first < b.first;
        });

        for (const auto& [nx, ny] : neighbours) {
            if (in_target_footprint(nx, ny)) continue;
            if (visited.count({nx, ny})) continue;
            if (!is_free_cell_for_road(cache, fiefdom, buildings, rivers, nx, ny)) continue;
            visited.insert({nx, ny});
            parent[{nx, ny}] = {x, y};
            distance[{nx, ny}] = dist + 1;
            queue.push_back({nx, ny});
        }
    }

    if (found_target.first < 0) return result;

    std::vector<std::pair<int, int>> path;
    std::pair<int, int> current = found_target;
    while (true) {
        if (!cell_has_road(fiefdom, cache, current.first, current.second)) {
            path.push_back(current);
        }
        auto pit = parent.find(current);
        if (pit == parent.end() || pit->second == current) break;
        current = pit->second;
    }
    std::reverse(path.begin(), path.end());

    if (path.empty()) {
        result.needed = false;
        result.found = true;
        return result;
    }
    if (static_cast<int>(path.size()) > max_length) {
        result.found = false;
        return result;
    }
    result.cells = std::move(path);
    result.found = true;
    return result;
}

RoadLink find_road_link_to_building(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    int building_id,
    int max_length,
    int corridor_spacing) {
    for (const auto& building : fiefdom.buildings) {
        if (building.id != building_id) continue;
        auto dims = GameLogic::GridCollision::getBuildingDimensions(cache, building.name);
        int w = dims.valid ? dims.width : 1;
        int h = dims.valid ? dims.height : 1;
        return find_road_link_to_site(
            cache, fiefdom, building.x, building.y, w, h, max_length, corridor_spacing);
    }
    return RoadLink{};
}

} // namespace Reeve
