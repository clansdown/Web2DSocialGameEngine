#include "game_logic.hpp"
#include "GameConfigCache.hpp"
#include "MoraleCalculator.hpp"
#include "FiefdomFetcher.hpp"
#include "ActionHandler.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace GameLogic {

ActionRegistry& ActionRegistry::getInstance() {
    static ActionRegistry instance;
    return instance;
}

void ActionRegistry::registerHandler(
    const std::string& action_type,
    ValidateFn validate_fn,
    ExecuteFn execute_fn,
    const std::string& description
) {
    handlers_[action_type] = {validate_fn, execute_fn, description};
}

ActionResult ActionRegistry::validate(const std::string& action_type, const json& payload, const ActionContext& ctx) {
    auto it = handlers_.find(action_type);
    if (it == handlers_.end()) {
        ActionResult result;
        result.status = ActionStatus::FAIL;
        result.error_code = "unknown_action";
        result.error_message = "Unknown action type: " + action_type;
        return result;
    }
    return it->second.validate_fn(payload, ctx);
}

ActionResult ActionRegistry::execute(const std::string& action_type, const json& payload, const ActionContext& ctx) {
    auto it = handlers_.find(action_type);
    if (it == handlers_.end()) {
        ActionResult result;
        result.status = ActionStatus::FAIL;
        result.error_code = "unknown_action";
        result.error_message = "Unknown action type: " + action_type;
        return result;
    }
    return it->second.execute_fn(payload, ctx);
}

ActionResult ActionRegistry::validateAndExecute(const std::string& action_type, const json& payload, const ActionContext& ctx) {
    auto validate_result = validate(action_type, payload, ctx);
    if (validate_result.status != ActionStatus::OK) {
        return validate_result;
    }
    return execute(action_type, payload, ctx);
}

std::vector<std::string> ActionRegistry::getRegisteredTypes() const {
    std::vector<std::string> types;
    for (const auto& [type, _] : handlers_) {
        types.push_back(type);
    }
    return types;
}

bool ActionRegistry::hasType(const std::string& action_type) const {
    return handlers_.find(action_type) != handlers_.end();
}

const std::string& ActionRegistry::getDescription(const std::string& action_type) const {
    static std::string empty;
    auto it = handlers_.find(action_type);
    if (it == handlers_.end()) {
        return empty;
    }
    return it->second.description;
}

TimeUpdateResult updateStateSince(Timestamp last_update_time, const std::string& fiefdom_filter_id) {
    TimeUpdateResult result;
    result.new_timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    result.time_hours_elapsed = static_cast<double>(result.new_timestamp - last_update_time) / 3600.0;
    result.fiefdoms_updated = 0;
    
    if (result.time_hours_elapsed < 0.001) {
        return result;
    }
    
    auto& db = Database::getInstance().gameDB();
    auto& cache = GameConfigCache::getInstance();
    auto building_types = cache.getFiefdomBuildingTypes();
    
    try {
        db << "BEGIN TRANSACTION;";
        
        std::vector<FiefdomData> fiefdoms;
        
        if (!fiefdom_filter_id.empty()) {
            db << "SELECT id, owner_id, name, x, y, peasants, gold, grain, wood, steel, bronze, stone, leather, mana, wall_count, morale FROM fiefdoms WHERE id = ?;"
               << std::stoi(fiefdom_filter_id)
               >> [&](int id, int owner_id, std::string name, int x, int y,
                      int peasants, int gold, int grain, int wood, int steel,
                      int bronze, int stone, int leather, int mana, int wall_count, double morale) {
                  FiefdomData f;
                  f.id = id;
                  f.owner_id = owner_id;
                  f.name = name;
                  f.x = x;
                  f.y = y;
                  f.peasants = peasants;
                  f.gold = gold;
                  f.grain = grain;
                  f.wood = wood;
                  f.steel = steel;
                  f.bronze = bronze;
                  f.stone = stone;
                  f.leather = leather;
                  f.mana = mana;
                  f.wall_count = wall_count;
                  f.morale = morale;
                  fiefdoms.push_back(f);
               };
        } else {
            db << "SELECT id, owner_id, name, x, y, peasants, gold, grain, wood, steel, bronze, stone, leather, mana, wall_count, morale FROM fiefdoms;"
               >> [&](int id, int owner_id, std::string name, int x, int y,
                      int peasants, int gold, int grain, int wood, int steel,
                      int bronze, int stone, int leather, int mana, int wall_count, double morale) {
                  FiefdomData f;
                  f.id = id;
                  f.owner_id = owner_id;
                  f.name = name;
                  f.x = x;
                  f.y = y;
                  f.peasants = peasants;
                  f.gold = gold;
                  f.grain = grain;
                  f.wood = wood;
                  f.steel = steel;
                  f.bronze = bronze;
                  f.stone = stone;
                  f.leather = leather;
                  f.mana = mana;
                  f.wall_count = wall_count;
                  f.morale = morale;
                  fiefdoms.push_back(f);
               };
        }
        
            for (auto& fiefdom : fiefdoms) {
            fiefdom.buildings = FiefdomFetcher::fetchFiefdomBuildings(fiefdom.id);
            fiefdom.walls = FiefdomFetcher::fetchFiefdomWalls(fiefdom.id);
            fiefdom.officials = FiefdomFetcher::fetchFiefdomOfficials(fiefdom.id);
            fiefdom.heroes = FiefdomFetcher::fetchFiefdomHeroes(fiefdom.id);
            fiefdom.stationed_combatants = FiefdomFetcher::fetchStationedCombatants(fiefdom.id);

            double time_factor = result.time_hours_elapsed;

            for (auto& building : fiefdom.buildings) {
                if (building.construction_start_ts > 0) {
                    auto config_opt = Validation::getBuildingConfig(building.name);
                    if (config_opt) {
                        auto config = *config_opt;
                        int construction_seconds = 0;

                        if (config.contains("construction_times") && config["construction_times"].is_array()) {
                            auto times = config["construction_times"];
                            int level = building.level;
                            int max_index = static_cast<int>(times.size()) - 1;

                            if (level <= max_index) {
                                construction_seconds = times[level].get<int>();
                            } else if (max_index >= 1) {
                                int last = times[max_index].get<int>();
                                int prev = times[max_index - 1].get<int>();
                                int slope = last - prev;
                                construction_seconds = last + slope * (level - max_index);
                            } else if (times.size() > 0) {
                                construction_seconds = times[0].get<int>();
                            }
                        }

                        if (construction_seconds > 0) {
                            int64_t elapsed_seconds = result.new_timestamp - building.construction_start_ts;
                            if (elapsed_seconds >= construction_seconds) {
                                int new_level = building.level + 1;
                                if (FiefdomFetcher::updateBuildingLevel(building.id, new_level, result.new_timestamp)) {
                                    building.level = new_level;
                                    building.construction_start_ts = 0;
                                    result.completed_trainings.push_back({building.name, new_level});
                                }
                            }
                        }
                    }
                }
            }

            for (auto& wall : fiefdom.walls) {
                if (wall.construction_start_ts > 0) {
                    auto config_opt = Validation::getWallConfigByGeneration(wall.generation);
                    if (config_opt) {
                        auto config = *config_opt;
                        int construction_seconds = 0;

                        if (config.contains("construction_times") && config["construction_times"].is_array()) {
                            auto times = config["construction_times"];
                            int level = wall.level;
                            int max_index = static_cast<int>(times.size()) - 1;

                            if (level <= max_index) {
                                construction_seconds = times[level].get<int>();
                            } else if (max_index >= 1) {
                                int last = times[max_index].get<int>();
                                int prev = times[max_index - 1].get<int>();
                                int slope = last - prev;
                                construction_seconds = last + slope * (level - max_index);
                            } else if (times.size() > 0) {
                                construction_seconds = times[0].get<int>();
                            }
                        }

                        if (construction_seconds > 0) {
                            int64_t elapsed_seconds = result.new_timestamp - wall.construction_start_ts;
                            if (elapsed_seconds >= construction_seconds) {
                                int new_level = wall.level + 1;
                                int new_hp = Validation::getWallHP(wall.generation, new_level);
                                if (FiefdomFetcher::updateWallLevel(wall.id, new_level, new_hp, result.new_timestamp)) {
                                    wall.level = new_level;
                                    wall.hp = new_hp;
                                    wall.construction_start_ts = 0;
                                    result.completed_trainings.push_back({"wall_gen_" + std::to_string(wall.generation), new_level});
                                }
                            }
                        }
                    }
                }
            }

            for (const auto& building : fiefdom.buildings) {
                if (building.level <= 0) continue;

                for (const auto& type_obj : building_types) {
                    if (!type_obj.contains(building.name)) continue;
                    auto type_config = type_obj[building.name];

                    for (const auto& resource : {"peasants", "gold", "grain", "wood", "steel", "bronze", "stone", "leather", "mana"}) {
                        if (type_config.contains(resource)) {
                            auto production = type_config[resource];
                            double amount = production["amount"].get<double>();
                            double amount_multiplier = production.value("amount_multiplier", 1.0);
                            double periodicity = production["periodicity"].get<double>();
                            double periodicity_multiplier = production.value("periodicity_multiplier", 1.0);

                            double cycles = time_factor / periodicity;
                            int full_cycles = static_cast<int>(cycles);

                            if (full_cycles > 0) {
                                double total_amount;
                                if (amount_multiplier == 1.0) {
                                    total_amount = amount * full_cycles;
                                } else {
                                    total_amount = amount * (std::pow(amount_multiplier, full_cycles) - 1.0) / (amount_multiplier - 1.0);
                                }

                                double old_value;
                                if (resource == "peasants") old_value = fiefdom.peasants;
                                else if (resource == "gold") old_value = fiefdom.gold;
                                else if (resource == "grain") old_value = fiefdom.grain;
                                else if (resource == "wood") old_value = fiefdom.wood;
                                else if (resource == "steel") old_value = fiefdom.steel;
                                else if (resource == "bronze") old_value = fiefdom.bronze;
                                else if (resource == "stone") old_value = fiefdom.stone;
                                else if (resource == "leather") old_value = fiefdom.leather;
                                else if (resource == "mana") old_value = fiefdom.mana;
                                else old_value = 0.0;

                                double new_value = old_value + total_amount;

                                db << "UPDATE fiefdoms SET " + resource + " = ? WHERE id = ?;"
                                   << new_value << fiefdom.id;

                                ProductionUpdate pu;
                                pu.resource_type = resource;
                                pu.amount_produced = total_amount;
                                pu.source_type = "building";
                                pu.source_id = building.id;
                                pu.fiefdom_id = fiefdom.id;
                                result.productions.push_back(pu);
                            }
                        }
                    }
                }
            }

            db << "UPDATE fiefdoms SET last_update_time = ? WHERE id = ?;"
               << result.new_timestamp << fiefdom.id;
            
            result.fiefdoms_updated++;
        }
        
        db << "COMMIT;";
        result.production_updates_applied = result.productions.size();
        
    } catch (const std::exception& e) {
        db << "ROLLBACK;";
        std::cerr << "Time update failed: " << e.what() << std::endl;
    }
    
    return result;
}

} // namespace GameLogic