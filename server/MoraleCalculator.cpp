#include "MoraleCalculator.hpp"
#include "GameConfigCache.hpp"
#include "heroes.hpp"
#include "combatants.hpp"
#include "fiefdom_officials.hpp"
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

EffectMode parseMode(const std::string& mode_str) {
    if (mode_str == "add") return EffectMode::Add;
    if (mode_str == "max") return EffectMode::Max;
    if (mode_str == "multiply") return EffectMode::Multiply;
    return EffectMode::Add;
}

double clampMorale(double value) {
    if (value < -1000.0) return -1000.0;
    if (value > 1000.0) return 1000.0;
    return value;
}

double calculateBuildingMorale(
    const std::string& building_name,
    int building_count,
    const nlohmann::json& building_config
) {
    if (!building_config.contains("morale_boost") || building_count == 0) {
        return 0.0;
    }

    double boost = building_config["morale_boost"].get<double>();
    std::string mode_str = building_config.value("morale_effect_mode", "add");
    EffectMode mode = parseMode(mode_str);

    switch (mode) {
        case EffectMode::Add:
            return boost * building_count;
        case EffectMode::Max:
            return boost;
        case EffectMode::Multiply: {
            double result = 1.0;
            for (int i = 0; i < building_count; i++) {
                result *= boost;
            }
            return result;
        }
    }
}

double calculateWallMorale(GameConfigCache& cache, const std::vector<WallData>& walls) {
    double total_wall_morale = 0.0;

    for (const auto& wall : walls) {
        if (wall.level <= 0) continue;

        auto config = cache.getAllConfigs();

        if (config.contains("wall_config") && config["wall_config"].is_object()) {
            auto wall_config = config["wall_config"];
            if (wall_config.contains("walls") && wall_config["walls"].is_object()) {
                auto walls_obj = wall_config["walls"];
                std::string gen_key = std::to_string(wall.generation);
                if (walls_obj.contains(gen_key)) {
                    auto gen_config = walls_obj[gen_key];
                    if (gen_config.contains("morale_boost") && gen_config["morale_boost"].is_array()) {
                        auto morale_array = gen_config["morale_boost"];
                        int idx = std::min(wall.level - 1, static_cast<int>(morale_array.size()) - 1);
                        if (idx >= 0) {
                            total_wall_morale += morale_array[idx].get<double>();
                        }
                    }
                }
            }
        }
    }

    return total_wall_morale;
}

double calculateFiefdomMorale(
    GameConfigCache& cache,
    int fiefdom_id,
    const std::vector<BuildingData>& buildings,
    const std::vector<WallData>& walls,
    const std::vector<OfficialData>& officials,
    const std::vector<FiefdomHero>& heroes,
    const std::vector<StationedCombatant>& combatants
) {
    double total_morale = 0.0;

    auto& hero_registry = Heroes::HeroRegistry::getInstance();
    auto& combatant_registry = Combatants::CombatantRegistry::getInstance();
    auto& official_registry = Officials::OfficialRegistry::getInstance();

    nlohmann::json building_types = cache.getFiefdomBuildingTypes();

    std::unordered_map<std::string, int> building_counts;
    for (const auto& building : buildings) {
        building_counts[building.name]++;
    }

    for (const auto& [name, count] : building_counts) {
        for (const auto& type_obj : building_types) {
            if (type_obj.contains(name)) {
                nlohmann::json type_config = type_obj[name];
                total_morale += calculateBuildingMorale(name, count, type_config);
                break;
            }
        }
    }

    total_morale += calculateWallMorale(cache, walls);

    for (const auto& official : officials) {
        auto official_opt = official_registry.getOfficial(official.template_id);
        if (official_opt && !(*official_opt)->morale_boost.empty() && official.level > 0) {
            int idx = std::min(official.level - 1, static_cast<int>((*official_opt)->morale_boost.size()) - 1);
            if (idx >= 0) {
                total_morale += (*official_opt)->morale_boost[idx];
            }
        }
    }

    for (const auto& hero : heroes) {
        auto hero_opt = hero_registry.getHero(hero.hero_config_id);
        if (hero_opt && !(*hero_opt)->morale_boost.empty() && hero.level > 0) {
            int idx = std::min(hero.level - 1, static_cast<int>((*hero_opt)->morale_boost.size()) - 1);
            if (idx >= 0) {
                total_morale += (*hero_opt)->morale_boost[idx];
            }
        }
    }

    for (const auto& combatant : combatants) {
        auto combatant_opt = combatant_registry.getPlayerCombatant(combatant.combatant_config_id);
        if (combatant_opt && !(*combatant_opt)->morale_boost.empty() && combatant.level > 0) {
            int idx = std::min(combatant.level - 1, static_cast<int>((*combatant_opt)->morale_boost.size()) - 1);
            if (idx >= 0) {
                total_morale += (*combatant_opt)->morale_boost[idx];
            }
        }
    }

    return clampMorale(total_morale);
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

} // namespace Morale
