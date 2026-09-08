#include "MoraleCalculator.hpp"
#include "GameConfigCache.hpp"
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <utility>

namespace Morale {

// Returns the building-type config object for `name` from the building-types
// JSON (an array of single-key objects), or an empty object if not found.
static nlohmann::json getBuildingConfigJson(const nlohmann::json& building_types,
                                            const std::string& name) {
    for (const auto& type_obj : building_types) {
        if (type_obj.is_object() && type_obj.contains(name) && type_obj[name].is_object()) {
            return type_obj[name];
        }
    }
    return nlohmann::json::object();
}

// True if the 1x1 road cell (rx, ry) is orthogonally adjacent to the rectangle
// [x, x+w) x [y, y+h) — i.e. shares an edge with the building footprint.
static bool roadCellAdjacentToRect(int rx, int ry, int x, int y, int w, int h) {
    if (rx == x - 1 || rx == x + w) {
        return (ry >= y && ry < y + h);
    }
    if (ry == y - 1 || ry == y + h) {
        return (rx >= x && rx < x + w);
    }
    return false;
}

// Builds a set of all road cells (orthogonally-connected 1x1 road tiles) from
// the fiefdom's level>=1 road buildings, plus the building footprint rects.
struct RoadNetworkData {
    std::set<std::pair<int, int>> road_cells;
    std::vector<BuildingData> road_buildings;
};

// Collects every placed road tile and the list of road buildings, so road
// connectivity can be computed without touching the database.
static RoadNetworkData collectRoadNetwork(const std::vector<BuildingData>& buildings) {
    RoadNetworkData data;
    for (const auto& building : buildings) {
        if (building.name != "road" || building.level < 1) continue;
        data.road_cells.insert({building.x, building.y});
        data.road_buildings.push_back(building);
    }
    return data;
}

// Computes, for each building in the fiefdom, the total morale points it
// receives from road-connected morale-source buildings. A source (a building
// whose config declares `road_morale: { boost, distance }`) radiates its boost
// outward along the orthogonally-connected road network: every road tile within
// `distance` road-steps of a road tile touching the source's footprint, and any
// building whose footprint touches one of those road tiles, receives the
// source's `boost`. Multiple sources stack additively. A source never boosts
// itself — it can still receive boosts from other sources.
//
// Returns a map from building id -> total morale points (0 if none).
std::unordered_map<int, double> computeRoadMoralePoints(
    const nlohmann::json& building_types,
    const std::vector<BuildingData>& buildings
) {
    std::unordered_map<int, double> morale_points;
    if (buildings.empty()) return morale_points;

    auto network = collectRoadNetwork(buildings);
    if (network.road_cells.empty()) return morale_points;

    const std::pair<int, int> neighbors[4] = {
        {0, 1}, {0, -1}, {1, 0}, {-1, 0}
    };

    for (const auto& source : buildings) {
        if (source.level < 1) continue;
        auto config = getBuildingConfigJson(building_types, source.name);
        if (!config.contains("road_morale") || !config["road_morale"].is_object()) continue;

        double boost = config["road_morale"].value("boost", 0.0);
        int distance = config["road_morale"].value("distance", 0);
        if (boost <= 0.0 || distance <= 0) continue;

        int sw = config.value("width", 1);
        int sh = config.value("height", 1);

        // Seed BFS from every road tile touching the source's footprint.
        std::set<std::pair<int, int>> visited;
        std::queue<std::pair<std::pair<int, int>, int>> queue; // (cell, dist)
        for (const auto& [rx, ry] : network.road_cells) {
            if (roadCellAdjacentToRect(rx, ry, source.x, source.y, sw, sh)) {
                visited.insert({rx, ry});
                queue.push({{rx, ry}, 1});
            }
        }

        // Reachable road cells within `distance` road-steps.
        std::set<std::pair<int, int>> reached;
        while (!queue.empty()) {
            auto [cell, dist] = queue.front();
            queue.pop();
            reached.insert(cell);
            if (dist >= distance) continue;
            for (const auto& [dx, dy] : neighbors) {
                std::pair<int, int> next = {cell.first + dx, cell.second + dy};
                if (visited.count(next)) continue;
                if (!network.road_cells.count(next)) continue;
                visited.insert(next);
                queue.push({next, dist + 1});
            }
        }

        // Every building whose footprint touches a reached road cell gets the
        // boost — except the source itself, which never boosts its own outputs
        // (it can still receive boosts from other sources).
        for (const auto& target : buildings) {
            if (target.level < 1) continue;
            if (target.id == source.id) continue;
            auto tconfig = getBuildingConfigJson(building_types, target.name);
            int tw = tconfig.value("width", 1);
            int th = tconfig.value("height", 1);
            bool touches = false;
            for (const auto& [rx, ry] : reached) {
                if (roadCellAdjacentToRect(rx, ry, target.x, target.y, tw, th)) {
                    touches = true;
                    break;
                }
            }
            if (touches) {
                morale_points[target.id] += boost;
            }
        }
    }

    return morale_points;
}

household_morale_result computeHouseholdMorale(
    const nlohmann::json& building_types,
    const nlohmann::json& retinue_config,
    const std::vector<BuildingData>& buildings,
    int funded, int unfunded,
    int64_t now, int64_t last_victory_ts, int64_t last_defeat_ts)
{
    household_morale_result result;

    const nlohmann::json morale_cfg = retinue_config.value("morale", nlohmann::json::object());
    const double funded_bonus = morale_cfg.value("funded_member_bonus", 1.0);
    const double unfunded_penalty = morale_cfg.value("unfunded_member_penalty", 1.0);
    const double max_percent = morale_cfg.value("max_bonus_percent", 10.0);
    const double points_per_percent = morale_cfg.value("points_per_percent", 2.0);
    const double victory_points = morale_cfg.value("victory_points", 12.0);
    const double defeat_points = morale_cfg.value("defeat_points", 16.0);
    const double decay_hours = morale_cfg.value("decay_hours", 24.0);

    // Chapel/church buildings contribute their morale_boost (small, stable).
    int chapel_points = 0;
    for (const auto& building : buildings) {
        if (building.level < 1 || building.name == "road") continue;
        const nlohmann::json cfg = getBuildingConfigJson(building_types, building.name);
        if (!cfg.is_object()) continue;
        if (cfg.value("class", "") != "chapel") continue;
        chapel_points += cfg.value("morale_boost", 0.0);
    }

    // Retinue funding state (persisted `maintained` flags from the last tick).
    const double funded_score = funded * funded_bonus;
    const double unfunded_score = unfunded * unfunded_penalty;

    // Temporary terms decay from stored recent-battle timestamps.
    auto decay = [&](int64_t ts, double pts) -> double {
        if (ts <= 0) return 0.0;
        const double hours = std::max(0.0, static_cast<double>(now - ts) / 3600.0);
        if (decay_hours <= 0.0 || hours >= decay_hours) return 0.0;
        return pts * (1.0 - hours / decay_hours);
    };
    result.victory_decay = decay(last_victory_ts, victory_points);
    result.defeat_decay = decay(last_defeat_ts, defeat_points);

    result.points = chapel_points + funded_score - unfunded_score
                  + result.victory_decay - result.defeat_decay;

    result.chapel_points = chapel_points;
    result.funded_count = funded;
    result.unfunded_count = unfunded;

    // Clamp to 0..max: a low-morale household earns no bonus but is never
    // penalized below baseline (no doom-spiral, per design).
    double percent = (points_per_percent > 0.0) ? result.points / points_per_percent : 0.0;
    result.percent = std::max(0.0, std::min(max_percent, percent));
    return result;
}

} // namespace Morale
