#include "game_logic.hpp"
#include "GameConfigCache.hpp"
#include "MoraleCalculator.hpp"
#include "FiefdomFetcher.hpp"
#include "ActionHandler.hpp"
#include "ActionHandlers.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

using json = nlohmann::json;

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

TimeUpdateResult updateStateSince(GameConfigCache& config_cache, Timestamp last_update_time, const std::string& fiefdom_filter_id) {
    TimeUpdateResult result;
    result.new_timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    result.time_hours_elapsed = static_cast<double>(result.new_timestamp - last_update_time) / 3600.0;
    result.fiefdoms_updated = 0;
    
    if (result.time_hours_elapsed < (1.0 / 3600.0)) {
        return result;
    }
    
    auto& db = Database::getInstance().gameDB();
    auto building_types = config_cache.getFiefdomBuildingTypes();
    
    try {
        db << "BEGIN TRANSACTION;";
        
        std::vector<FiefdomData> fiefdoms;
        
        auto read_fiefdom = [&](int id, int owner_id, std::string name, int x, int y,
                                int peasants, int gold, int grain, int wood, int steel,
                                int bronze, int stone, int leather, int mana, int wall_count,
                                double morale, std::string import_settings_str) {
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
            try { f.import_settings = nlohmann::json::parse(import_settings_str); }
            catch (...) { f.import_settings = nlohmann::json::object(); }
            fiefdoms.push_back(f);
        };

        if (!fiefdom_filter_id.empty()) {
            db << "SELECT id, owner_id, name, x, y, peasants, gold, grain, wood, steel, bronze, stone, leather, mana, wall_count, morale, import_settings FROM fiefdoms WHERE id = ?;"
               << std::stoi(fiefdom_filter_id)
               >> read_fiefdom;
        } else {
            db << "SELECT id, owner_id, name, x, y, peasants, gold, grain, wood, steel, bronze, stone, leather, mana, wall_count, morale, import_settings FROM fiefdoms;"
               >> read_fiefdom;
        }
        
            for (auto& fiefdom : fiefdoms) {
            fiefdom.buildings = FiefdomFetcher::fetchFiefdomBuildings(fiefdom.id);
            fiefdom.walls = FiefdomFetcher::fetchFiefdomWalls(fiefdom.id);
            fiefdom.officials = FiefdomFetcher::fetchFiefdomOfficials(fiefdom.id);
            fiefdom.heroes = FiefdomFetcher::fetchFiefdomHeroes(fiefdom.id);
            fiefdom.stationed_combatants = FiefdomFetcher::fetchStationedCombatants(fiefdom.id);

            double time_factor = result.time_hours_elapsed;

            // Pre-compute building-to-building modifiers for this fiefdom
            auto modifier_map = computeBuildingModifiers(fiefdom.buildings, building_types);

            for (auto& building : fiefdom.buildings) {
                if (building.construction_start_ts > 0) {
                    auto config_opt = Validation::getBuildingConfig(config_cache, building.name);
                    if (config_opt) {
                        auto config = *config_opt;
                        int construction_seconds = 0;

                        if (config.contains("construction_times")) {
                            construction_seconds = getIntForLevel(config["construction_times"], building.level, 0);
                        }

                        if (construction_seconds > 0) {
                            int64_t elapsed_seconds = result.new_timestamp - building.construction_start_ts;
                            if (elapsed_seconds >= construction_seconds) {
                                int new_level = building.level + 1;
                                int old_level = building.level;
                                int building_id = building.id;
                                
                                bool prerequisites_met = true;
                                if (config.contains("prerequisites") && config["prerequisites"].is_array()) {
                                    auto prerequisites_opt = Validation::getPrerequisitesForLevel(config_cache, building.name, new_level);
                                    if (prerequisites_opt && !prerequisites_opt->empty()) {
                                        prerequisites_met = Validation::checkFiefdomPrerequisites(fiefdom.id, *prerequisites_opt);
                                    }
                                }

                                // Check dependencies for the new level
                                if (prerequisites_met) {
                                    nlohmann::json deps = Validation::getDependenciesForLevel(config_cache, building.name, new_level);
                                    if (!deps.empty()) {
                                        auto dep_result = Validation::checkBuildingDependencies(config_cache, fiefdom.id, fiefdom.buildings, deps);
                                        if (!dep_result.first) {
                                            prerequisites_met = false;
                                        }
                                    }
                                }
                                
                                if (prerequisites_met) {
                                    if (FiefdomFetcher::updateBuildingLevel(building.id, new_level, result.new_timestamp)) {
                                        building.level = new_level;
                                        building.construction_start_ts = 0;
                                        result.completed_trainings.push_back({building.name, new_level});
                                    }
                                } else {
                                    nlohmann::json refund;
                                    std::string cost_fields[] = {"gold_cost", "wood_cost", "stone_cost", "steel_cost", 
                                                                 "bronze_cost", "grain_cost", "leather_cost", "mana_cost"};
                                    std::string resource_fields[] = {"gold", "wood", "stone", "steel", 
                                                                     "bronze", "grain", "leather", "mana"};
                                    
                                    for (size_t i = 0; i < 8; i++) {
                                        const auto& cost_key = cost_fields[i];
                                        const auto& resource_key = resource_fields[i];
                                        
                                        if (config.contains(cost_key) && config[cost_key].is_array()) {
                                            auto costs = config[cost_key];
                                            int level_index = old_level > 0 ? old_level - 1 : 0;
                                            if (level_index >= 0 && level_index < costs.size()) {
                                                int refund_amount = costs[level_index].get<int>();
                                                if (refund_amount > 0) refund[resource_key] = refund_amount;
                                            }
                                        }
                                    }
                                    
                                    if (!refund.empty()) {
                                        ActionResult temp_result;
                                        Validation::refundResources(fiefdom.id, refund, temp_result);
                                    }
                                    
                                    FailedUpgrade fail;
                                    fail.building_id = building_id;
                                    fail.attempted_level = new_level;
                                    fail.reason = "prerequisites_not_met";
                                    fail.refund = refund;
                                    result.failed_upgrades.push_back(fail);
                                }
                            }
                        }
                    }
                }
            }

            for (auto& wall : fiefdom.walls) {
                if (wall.construction_start_ts > 0) {
                    auto config_opt = Validation::getWallConfigByGeneration(config_cache, wall.generation);
                    if (config_opt) {
                        auto config = *config_opt;
                        int construction_seconds = 0;

                        if (config.contains("construction_times")) {
                            construction_seconds = getIntForLevel(config["construction_times"], wall.level, 0);
                        }

                        if (construction_seconds > 0) {
                            int64_t elapsed_seconds = result.new_timestamp - wall.construction_start_ts;
                            if (elapsed_seconds >= construction_seconds) {
                                int new_level = wall.level + 1;
                                int new_hp = Validation::getWallHP(config_cache, wall.generation, new_level);
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

                                // Apply building-to-building modifiers
                                auto bm_it = modifier_map.find(building.id);
                                if (bm_it != modifier_map.end()) {
                                    auto rm_it = bm_it->second.find(resource);
                                    if (rm_it != bm_it->second.end()) {
                                        total_amount *= rm_it->second;
                                    }
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

                                db << (std::string("UPDATE fiefdoms SET ") + resource + " = ? WHERE id = ?;").c_str()
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

            // ---- Economy: consumption + import + grain->gold + morale ----
            {
                auto economy_cfg = config_cache.getEconomyConfig();
                double grain_to_gold = economy_cfg.value("grain_to_gold_rate", 5.0);
                double unmet_penalty = economy_cfg.value("unmet_need_morale_penalty", 0.5);
                int default_prio = economy_cfg.value("default_priority", 50);
                auto import_prices = economy_cfg.value("import_prices", json::object());
                auto pop_costs = economy_cfg.value("population_costs", json::object());

                int cur_peasants = 0;
                double cur_gold = 0, cur_grain = 0, cur_wood = 0, cur_steel = 0;
                double cur_bronze = 0, cur_stone = 0, cur_leather = 0, cur_mana = 0;
                db << "SELECT peasants, gold, grain, wood, steel, bronze, stone, leather, mana "
                      "FROM fiefdoms WHERE id = ?;" << fiefdom.id
                   >> [&](int p, double g, double gr, double w, double s,
                          double b, double st, double l, double m) {
                       cur_peasants = p; cur_gold = g; cur_grain = gr;
                       cur_wood = w; cur_steel = s; cur_bronze = b;
                       cur_stone = st; cur_leather = l; cur_mana = m;
                   };

                struct ConsEntry {
                    std::string resource;
                    double amount;
                    int priority;
                };
                std::vector<ConsEntry> entries;

                auto add_entry = [&](const std::string& res, double amount, int prio) {
                    if (amount > 0) entries.push_back({res, amount, prio});
                };

                // 1. Building hourly costs
                for (const auto& building : fiefdom.buildings) {
                    if (building.level <= 0) continue;
                    json bld_cfg;
                    for (const auto& obj : building_types) {
                        if (obj.contains(building.name)) { bld_cfg = obj[building.name]; break; }
                    }
                    if (bld_cfg.is_null() || !bld_cfg.contains("hourly_cost")) continue;
                    auto costs = bld_cfg["hourly_cost"];
                    int prio = bld_cfg.value("priority", default_prio);
                    for (auto& [res, rate] : costs.items()) {
                        add_entry(res, rate.get<double>() * result.time_hours_elapsed, prio);
                    }
                }

                // 2. Population costs
                for (auto& [pop_type, costs] : pop_costs.items()) {
                    int count = 0;
                    if (pop_type == "peasants") count = cur_peasants;
                    if (count <= 0) continue;
                    int prio = costs.value("priority", 1);
                    for (auto& [res, rate] : costs.items()) {
                        if (res == "priority") continue;
                        add_entry(res, rate.get<double>() * count * result.time_hours_elapsed, prio);
                    }
                }

                std::sort(entries.begin(), entries.end(),
                    [](const ConsEntry& a, const ConsEntry& b) { return a.priority < b.priority; });

                auto get_cur = [&](const std::string& r) -> double* {
                    if (r == "gold") return &cur_gold;
                    if (r == "grain") return &cur_grain;
                    if (r == "wood") return &cur_wood;
                    if (r == "steel") return &cur_steel;
                    if (r == "bronze") return &cur_bronze;
                    if (r == "stone") return &cur_stone;
                    if (r == "leather") return &cur_leather;
                    if (r == "mana") return &cur_mana;
                    return nullptr;
                };

                double morale_damage = 0;

                for (const auto& entry : entries) {
                    if (entry.resource == "gold") continue;
                    double* avail = get_cur(entry.resource);
                    if (!avail) continue;
                    double effective = std::min(*avail, entry.amount);
                    *avail -= effective;
                    double unmet = entry.amount - effective;

                    if (unmet > 0.001) {
                        bool auto_import = true;
                        if (fiefdom.import_settings.is_object() &&
                            fiefdom.import_settings.contains(entry.resource)) {
                            auto_import = fiefdom.import_settings[entry.resource].get<bool>();
                        }
                        if (auto_import) {
                            double price = import_prices.value(entry.resource, 2.0);
                            double affordable = std::min(unmet, std::floor(cur_gold / price));
                            if (affordable >= 1.0) {
                                cur_gold -= affordable * price;
                                *avail += affordable;
                                unmet -= affordable;
                            }
                        }
                        if (unmet > 0.001) {
                            morale_damage += unmet * unmet_penalty;
                        }
                    }
                }

                // 3. Grain surplus -> gold
                if (cur_grain > 0) {
                    cur_gold += cur_grain * grain_to_gold;
                    cur_grain = 0;
                }

                // 4. Gold consumption (last — imports already deducted)
                {
                    double gold_needed = 0;
                    for (const auto& building : fiefdom.buildings) {
                        if (building.level <= 0) continue;
                        json bld_cfg;
                        for (const auto& obj : building_types) {
                            if (obj.contains(building.name)) { bld_cfg = obj[building.name]; break; }
                        }
                        if (bld_cfg.is_null() || !bld_cfg.contains("hourly_cost")) continue;
                        auto costs = bld_cfg["hourly_cost"];
                        if (!costs.contains("gold")) continue;
                        gold_needed += costs["gold"].get<double>() * result.time_hours_elapsed;
                    }
                    if (gold_needed > 0) {
                        double effective = std::min(cur_gold, gold_needed);
                        cur_gold -= effective;
                        double unmet = gold_needed - effective;
                        if (unmet > 0.001) morale_damage += unmet * unmet_penalty * 2;
                    }
                }

                // 5. Apply morale damage
                if (morale_damage > 0.001) {
                    double new_morale = std::max(-1000.0, std::min(1000.0, fiefdom.morale - morale_damage));
                    db << "UPDATE fiefdoms SET morale = ? WHERE id = ?;" << new_morale << fiefdom.id;
                    fiefdom.morale = new_morale;
                }

                // 6. Write updated resources
                auto wr = [&](const std::string& r, double val) {
                    db << ("UPDATE fiefdoms SET " + r + " = ? WHERE id = ?;").c_str()
                       << std::max(0.0, val) << fiefdom.id;
                };
                wr("gold", cur_gold);
                wr("grain", cur_grain);
                wr("wood", cur_wood);
                wr("steel", cur_steel);
                wr("bronze", cur_bronze);
                wr("stone", cur_stone);
                wr("leather", cur_leather);
                wr("mana", cur_mana);
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

// ---------------------------------------------------------------------------
// Helper: index into a level-indexed JSON array with linear extrapolation
// ---------------------------------------------------------------------------

int getIntForLevel(const json& arr, int level, int default_val) {
    if (arr.is_number()) {
        return arr.get<int>();
    }
    if (!arr.is_array() || arr.empty()) {
        return default_val;
    }
    int max_index = static_cast<int>(arr.size()) - 1;
    if (level <= max_index) {
        return arr[level].get<int>();
    }
    if (max_index >= 1) {
        int last = arr[max_index].get<int>();
        int prev = arr[max_index - 1].get<int>();
        return last + (last - prev) * (level - max_index);
    }
    return arr[0].get<int>();
}

double getDoubleForLevel(const json& arr, int level, double default_val) {
    if (arr.is_number()) {
        return arr.get<double>();
    }
    if (!arr.is_array() || arr.empty()) {
        return default_val;
    }
    int max_index = static_cast<int>(arr.size()) - 1;
    if (level <= max_index) {
        return arr[level].get<double>();
    }
    if (max_index >= 1) {
        double last = arr[max_index].get<double>();
        double prev = arr[max_index - 1].get<double>();
        return last + (last - prev) * (level - max_index);
    }
    return arr[0].get<double>();
}

// ---------------------------------------------------------------------------
// Building-to-building modifier computation
// ---------------------------------------------------------------------------

ModifierMap computeBuildingModifiers(
    const std::vector<BuildingData>& buildings,
    const json& building_types
) {
    ModifierMap result;

    if (!building_types.is_array()) return result;

    struct SourceInfo {
        int building_id;
        int level;
        json modifier_config;
    };
    std::unordered_map<std::string, std::vector<SourceInfo>> sources_by_modifier;

    for (const auto& building : buildings) {
        if (building.level <= 0) continue;

        json type_config;
        for (const auto& type_obj : building_types) {
            if (type_obj.contains(building.name)) {
                type_config = type_obj[building.name];
                break;
            }
        }
        if (type_config.is_null() || !type_config.contains("modifiers")) continue;

        auto modifiers = type_config["modifiers"];
        if (!modifiers.is_array()) continue;

        for (const auto& mod : modifiers) {
            if (!mod.contains("modifier_id")) continue;
            std::string modifier_id = mod["modifier_id"].get<std::string>();
            sources_by_modifier[modifier_id].push_back({building.id, building.level, mod});
        }
    }

    struct ModifierApplied {
        std::string resource;
        double multiplier;
    };
    std::unordered_map<int, std::vector<ModifierApplied>> target_mods;

    for (const auto& [modifier_id, sources] : sources_by_modifier) {
        if (sources.empty()) continue;

        auto first_mod = sources[0].modifier_config;
        if (!first_mod.contains("target_building") || !first_mod.contains("target_resource")) continue;
        std::string target_building = first_mod["target_building"].get<std::string>();
        std::string target_resource = first_mod["target_resource"].get<std::string>();

        std::vector<int> all_targets;
        for (const auto& building : buildings) {
            if (building.name == target_building && building.level > 0) {
                all_targets.push_back(building.id);
            }
        }
        if (all_targets.empty()) continue;

        std::unordered_set<int> assigned_targets;

        auto sorted_sources = sources;
        std::sort(sorted_sources.begin(), sorted_sources.end(),
            [](const SourceInfo& a, const SourceInfo& b) { return a.level > b.level; });

        for (const auto& source : sorted_sources) {
            int max_targets = getIntForLevel(source.modifier_config["max_targets"], source.level - 1, 0);
            if (max_targets <= 0) continue;

            double multiplier = getDoubleForLevel(source.modifier_config["multiplier"], source.level - 1, 1.0);

            int assigned = 0;
            for (int target_id : all_targets) {
                if (assigned >= max_targets) break;
                if (assigned_targets.count(target_id)) continue;
                assigned_targets.insert(target_id);
                target_mods[target_id].push_back({target_resource, multiplier});
                assigned++;
            }
        }
    }

    for (const auto& [target_id, mods] : target_mods) {
        std::unordered_map<std::string, double> combined;
        for (const auto& m : mods) {
            combined[m.resource] *= m.multiplier;
        }
        result[target_id] = std::move(combined);
    }

    return result;
}

} // namespace GameLogic
