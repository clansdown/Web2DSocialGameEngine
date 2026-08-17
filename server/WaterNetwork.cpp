#include "WaterNetwork.hpp"
#include <algorithm>
#include <queue>

namespace Water {

namespace {

// Returns the building-type config object for `name` (array of single-key
// objects), or an empty object if not found.
nlohmann::json getBuildingConfigJson(const nlohmann::json& building_types,
                                     const std::string& name) {
    for (const auto& type_obj : building_types) {
        if (type_obj.is_object() && type_obj.contains(name) && type_obj[name].is_object()) {
            return type_obj[name];
        }
    }
    return nlohmann::json::object();
}

// True if the 1x1 cell (rx, ry) shares an edge with the rectangle
// [x, x+w) x [y, y+h) — i.e. touches any part of the building footprint.
bool cellAdjacentToRect(int rx, int ry, int x, int y, int w, int h) {
    if (rx == x - 1 || rx == x + w) {
        return (ry >= y && ry < y + h);
    }
    if (ry == y - 1 || ry == y + h) {
        return (rx >= x && rx < x + w);
    }
    return false;
}

// Resolves the current pond type's capacity from `pond_types` in the mill_pond
// config. Falls back to the first type when the stored type is unknown/blank.
int pondCapacity(const nlohmann::json& cfg, const std::string& pond_type) {
    if (!cfg.contains("pond_types") || !cfg["pond_types"].is_array()) return 0;
    auto types = cfg["pond_types"];
    for (const auto& pt : types) {
        if (pt.is_object() && pt.value("id", "") == pond_type) {
            return pt.value("capacity", 0);
        }
    }
    if (!types.empty() && types[0].is_object()) {
        return types[0].value("capacity", 0);
    }
    return 0;
}

// BFS over 1x1 connector cells (`cells`) seeded from every cell touching the
// building footprint, returning the reachable set.
std::set<std::pair<int, int>> bfsFromFootprint(
    const BuildingData& building, int w, int h,
    const std::set<std::pair<int, int>>& cells) {
    const std::pair<int, int> neighbors[4] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
    std::set<std::pair<int, int>> visited;
    std::queue<std::pair<int, int>> queue;
    for (const auto& [rx, ry] : cells) {
        if (cellAdjacentToRect(rx, ry, building.x, building.y, w, h)) {
            visited.insert({rx, ry});
            queue.push({rx, ry});
        }
    }
    while (!queue.empty()) {
        auto cell = queue.front();
        queue.pop();
        for (const auto& [dx, dy] : neighbors) {
            std::pair<int, int> next = {cell.first + dx, cell.second + dy};
            if (visited.count(next)) continue;
            if (!cells.count(next)) continue;
            visited.insert(next);
            queue.push(next);
        }
    }
    return visited;
}

} // namespace

WaterPowerResult computeWaterPower(
    const nlohmann::json& building_types,
    const std::vector<BuildingData>& buildings,
    const std::set<std::pair<int, int>>& river_cells) {
    WaterPowerResult result;
    if (buildings.empty() || river_cells.empty()) return result;

    // Collect level >= 1 connector cells.
    std::set<std::pair<int, int>> head_cells, tail_cells;
    for (const auto& b : buildings) {
        if (b.level < 1) continue;
        if (b.name == "head_race") head_cells.insert({b.x, b.y});
        else if (b.name == "tail_race") tail_cells.insert({b.x, b.y});
    }

    // Active ponds: water-source buildings whose footprint touches the river.
    struct PondInfo {
        int id;
        int capacity;
        std::set<std::pair<int, int>> reach; // head-race cells reachable from the pond
    };
    std::vector<PondInfo> ponds;
    for (const auto& b : buildings) {
        if (b.level < 1) continue;
        auto cfg = getBuildingConfigJson(building_types, b.name);
        if (!cfg.value("water_source", false)) continue;
        int w = cfg.value("width", 1);
        int h = cfg.value("height", 1);
        bool touches_river = false;
        for (const auto& [rx, ry] : river_cells) {
            if (cellAdjacentToRect(rx, ry, b.x, b.y, w, h)) {
                touches_river = true;
                break;
            }
        }
        if (!touches_river) continue;
        PondInfo info;
        info.id = b.id;
        info.capacity = pondCapacity(cfg, b.pond_type);
        info.reach = bfsFromFootprint(b, w, h, head_cells);
        ponds.push_back(std::move(info));
    }
    std::sort(ponds.begin(), ponds.end(),
              [](const PondInfo& a, const PondInfo& b) { return a.id < b.id; });

    // Tail-reachable cells: BFS over tail-race cells seeded from cells touching
    // a river cell (the tail race drains back into the river).
    std::set<std::pair<int, int>> tail_reach;
    {
        const std::pair<int, int> neighbors[4] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
        std::set<std::pair<int, int>> visited;
        std::queue<std::pair<int, int>> queue;
        for (const auto& [rx, ry] : tail_cells) {
            bool touches_river = false;
            for (const auto& [dx, dy] : neighbors) {
                if (river_cells.count({rx + dx, ry + dy})) {
                    touches_river = true;
                    break;
                }
            }
            if (touches_river) {
                visited.insert({rx, ry});
                queue.push({rx, ry});
            }
        }
        while (!queue.empty()) {
            auto cell = queue.front();
            queue.pop();
            tail_reach.insert(cell);
            for (const auto& [dx, dy] : neighbors) {
                std::pair<int, int> next = {cell.first + dx, cell.second + dy};
                if (visited.count(next)) continue;
                if (!tail_cells.count(next)) continue;
                visited.insert(next);
                queue.push(next);
            }
        }
    }

    // Assign water-powered buildings to ponds (lowest-id pond first), subject
    // to each pond's capacity.
    std::unordered_map<int, int> pond_counts;
    for (const auto& b : buildings) {
        if (b.level < 1) continue;
        auto cfg = getBuildingConfigJson(building_types, b.name);
        if (!cfg.value("water_powered", false)) continue;
        int w = cfg.value("width", 1);
        int h = cfg.value("height", 1);

        // Drained: footprint touches a tail-reachable cell.
        bool drained = false;
        for (const auto& [rx, ry] : tail_reach) {
            if (cellAdjacentToRect(rx, ry, b.x, b.y, w, h)) {
                drained = true;
                break;
            }
        }
        if (!drained) continue;

        // Fed: footprint touches a head-reachable cell of a pond with spare capacity.
        for (const auto& pond : ponds) {
            if (pond_counts[pond.id] >= pond.capacity) continue;
            bool fed = false;
            for (const auto& [rx, ry] : pond.reach) {
                if (cellAdjacentToRect(rx, ry, b.x, b.y, w, h)) {
                    fed = true;
                    break;
                }
            }
            if (!fed) continue;
            result.powered[b.id] = true;
            result.powered_by[b.id] = pond.id;
            pond_counts[pond.id]++;
            break;
        }
    }

    for (const auto& pond : ponds) {
        result.pond_load[pond.id] = pond_counts[pond.id];
    }

    return result;
}

} // namespace Water