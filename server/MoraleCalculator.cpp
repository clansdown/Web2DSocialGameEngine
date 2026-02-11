#include "MoraleCalculator.hpp"
#include "GameConfigCache.hpp"
#include "Heroes.hpp"
#include "Combatants.hpp"
#include "fiefdom_officials.hpp"
#include <algorithm>
#include <cmath>

namespace Morale {

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

double calculateWallMorale(const std::vector<WallData>& walls) {
    double total_wall_morale = 0.0;

    for (const auto& wall : walls) {
        if (wall.level <= 0) continue;

        auto& cache = GameConfigCache::getInstance();
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
    int fiefdom_id,
    const std::vector<BuildingData>& buildings,
    const std::vector<WallData>& walls,
    const std::vector<OfficialData>& officials,
    const std::vector<FiefdomHero>& heroes,
    const std::vector<StationedCombatant>& combatants
) {
    double total_morale = 0.0;

    auto& cache = GameConfigCache::getInstance();
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

    total_morale += calculateWallMorale(walls);

    for (const auto& official : officials) {
        auto official_opt = official_registry.getOfficial(official.template_id);
        if (official_opt && !official_opt->morale_boost.empty() && official.level > 0) {
            int idx = std::min(official.level - 1, static_cast<int>(official_opt->morale_boost.size()) - 1);
            if (idx >= 0) {
                total_morale += official_opt->morale_boost[idx];
            }
        }
    }

    for (const auto& hero : heroes) {
        auto hero_opt = hero_registry.getHero(hero.hero_config_id);
        if (hero_opt && !hero_opt->morale_boost.empty() && hero.level > 0) {
            int idx = std::min(hero.level - 1, static_cast<int>(hero_opt->morale_boost.size()) - 1);
            if (idx >= 0) {
                total_morale += hero_opt->morale_boost[idx];
            }
        }
    }

    for (const auto& combatant : combatants) {
        auto combatant_opt = combatant_registry.getPlayerCombatant(combatant.combatant_config_id);
        if (combatant_opt && !combatant_opt->morale_boost.empty() && combatant.level > 0) {
            int idx = std::min(combatant.level - 1, static_cast<int>(combatant_opt->morale_boost.size()) - 1);
            if (idx >= 0) {
                total_morale += combatant_opt->morale_boost[idx];
            }
        }
    }

    return clampMorale(total_morale);
}

} // namespace Morale