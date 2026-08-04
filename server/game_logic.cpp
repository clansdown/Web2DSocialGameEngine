#include "game_logic.hpp"
#include "GameConfigCache.hpp"
#include "MoraleCalculator.hpp"
#include "FiefdomFetcher.hpp"
#include "ActionHandler.hpp"
#include "ActionHandlers.hpp"
#include "combatants.hpp"
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
                                int peasants, double gold, int silver_pence, int grain, int wood, int steel,
                                int bronze, int stone, int leather, int mana, int charcoal,
                                int iron, int ironwork, int fancy_ironwork, int wall_count,
                                double morale, std::string import_settings_str, std::string reserves_str) {
            FiefdomData f;
            f.id = id;
            f.owner_id = owner_id;
            f.name = name;
            f.x = x;
            f.y = y;
            f.peasants = peasants;
            f.gold = gold;
            f.silver_pence = silver_pence;
            f.grain = grain;
            f.wood = wood;
            f.steel = steel;
            f.bronze = bronze;
            f.stone = stone;
            f.leather = leather;
            f.mana = mana;
            f.charcoal = charcoal;
            f.iron = iron;
            f.ironwork = ironwork;
            f.fancy_ironwork = fancy_ironwork;
            f.wall_count = wall_count;
            f.morale = morale;
            try { f.import_settings = nlohmann::json::parse(import_settings_str); }
            catch (...) { f.import_settings = nlohmann::json::object(); }
            try { f.reserves = nlohmann::json::parse(reserves_str); }
            catch (...) { f.reserves = nlohmann::json::object(); }
            fiefdoms.push_back(f);
        };

        if (!fiefdom_filter_id.empty()) {
            db << "SELECT id, owner_id, name, x, y, peasants, gold, silver_pence, grain, wood, steel, bronze, stone, leather, mana, charcoal, iron, ironwork, fancy_ironwork, wall_count, morale, import_settings, reserves FROM fiefdoms WHERE id = ?;"
               << std::stoi(fiefdom_filter_id)
               >> read_fiefdom;
        } else {
            db << "SELECT id, owner_id, name, x, y, peasants, gold, silver_pence, grain, wood, steel, bronze, stone, leather, mana, charcoal, iron, ironwork, fancy_ironwork, wall_count, morale, import_settings, reserves FROM fiefdoms;"
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
                                                double refund_amount = costs[level_index].get<double>();
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

            // ---- Economy: input gating -> production -> consumption -> import -> exports ----
            {
                auto economy_cfg = config_cache.getEconomyConfig();
                double unmet_penalty = economy_cfg.value("unmet_need_morale_penalty", 0.5);
                double export_sell_multiplier = economy_cfg.value("export_sell_multiplier", 0.5);
                int default_prio = economy_cfg.value("default_priority", 50);
                int upkeep_prio = economy_cfg.value("combatant_upkeep_priority", 10);
                auto import_prices = economy_cfg.value("import_prices", json::object());
                auto export_prices = economy_cfg.value("export_prices", json::object());
                auto export_sell_multipliers = economy_cfg.value("export_sell_multipliers", json::object());
                auto pop_costs = economy_cfg.value("population_costs", json::object());
                auto default_reserves = economy_cfg.value("default_reserves", json::object());

                // All production and consumption share a fixed 1-day period.
                // Fractional days avoid flooring so short elapsed spans still
                // compute proportional amounts.
                double days_elapsed = result.time_hours_elapsed / 24.0;

                // Penny-market currency ratios (mirror economy.json "currency").
                auto currency_cfg = economy_cfg.value("currency", json::object());
                double pence_per_shilling = currency_cfg.value("pence_per_shilling", 12.0);
                double shillings_per_pound = currency_cfg.value("shillings_per_pound", 20.0);
                double pence_per_gold = currency_cfg.value("pence_per_gold", pence_per_shilling * shillings_per_pound);

                // Silver-pence wallet (separate from gold). Used for penny-market
                // resources like grain: imports deduct pence, exports credit pence.
                int64_t cur_silver_pence = fiefdom.silver_pence;

                // Convert a money-form import price ({gold, shillings, pence}) to
                // whole pence. Returns 0 for plain-number (gold-denominated) prices.
                auto money_price_to_pence = [&](const json& price) -> int64_t {
                    if (!price.is_object()) return 0;
                    double gold = price.value("gold", 0.0);
                    double shillings = price.value("shillings", 0.0);
                    double pence = price.value("pence", 0.0);
                    return static_cast<int64_t>(std::llround(
                        gold * pence_per_gold + shillings * pence_per_shilling + pence));
                };

                // Current resource stock (gold is fractional-capable)
                int cur_peasants = 0;
                double cur_gold = 0, cur_grain = 0, cur_wood = 0, cur_steel = 0;
                double cur_bronze = 0, cur_stone = 0, cur_leather = 0, cur_mana = 0;
                double cur_charcoal = 0, cur_iron = 0, cur_ironwork = 0, cur_fancy_ironwork = 0;
                db << "SELECT peasants, gold, grain, wood, steel, bronze, stone, leather, mana, charcoal, iron, ironwork, fancy_ironwork "
                      "FROM fiefdoms WHERE id = ?;" << fiefdom.id
                   >> [&](int p, double g, double gr, double w, double s,
                          double b, double st, double l, double m, double ch, double ir, double iw, double fiw) {
                       cur_peasants = p; cur_gold = g; cur_grain = gr;
                       cur_wood = w; cur_steel = s; cur_bronze = b;
                       cur_stone = st; cur_leather = l; cur_mana = m;
                       cur_charcoal = ch; cur_iron = ir; cur_ironwork = iw;
                       cur_fancy_ironwork = fiw;
                   };

                auto get_cur = [&](const std::string& r) -> double* {
                    if (r == "gold") return &cur_gold;
                    if (r == "grain") return &cur_grain;
                    if (r == "wood") return &cur_wood;
                    if (r == "steel") return &cur_steel;
                    if (r == "bronze") return &cur_bronze;
                    if (r == "stone") return &cur_stone;
                    if (r == "leather") return &cur_leather;
                    if (r == "mana") return &cur_mana;
                    if (r == "charcoal") return &cur_charcoal;
                    if (r == "iron") return &cur_iron;
                    if (r == "ironwork") return &cur_ironwork;
                    if (r == "fancy_ironwork") return &cur_fancy_ironwork;
                    return nullptr;
                };

                // Flat per-day amount for a production/input spec over the
                // elapsed period. All production and consumption share the same
                // 1-day period; fractional elapsed days are used directly (no
                // flooring) so any elapsed time beyond a tiny minimum computes
                // a proportional amount.
                auto compute_amount = [&](const json& spec) -> double {
                    double amount = 0.0;
                    if (spec.is_number()) {
                        amount = spec.get<double>();
                    } else {
                        amount = spec.value("amount", 0.0);
                    }
                    double days = time_factor / 24.0;
                    if (days <= 0) return 0.0;
                    return amount * days;
                };

                // Per-building production/input plan. Each building produces one
                // or more outputs; each output may have its own inputs, an unlock
                // level (min_level) and a player-controlled rate (0..1, default
                // 1.0). Rate 0 disables the output entirely (no inputs consumed).
                std::map<int, BuildingPlan> plans;

                for (const auto& building : fiefdom.buildings) {
                    if (building.level <= 0) continue;
                    for (const auto& type_obj : building_types) {
                        if (!type_obj.contains(building.name)) continue;
                        auto type_config = type_obj[building.name];
                        auto& plan = plans[building.id];

                        // Per-building player rate for a given output resource.
                        auto rate_for = [&](const std::string& res) -> double {
                            if (building.output_rates.is_object() && building.output_rates.contains(res) &&
                                building.output_rates[res].is_number()) {
                                double r = building.output_rates[res].get<double>();
                                if (r < 0.0) return 0.0;
                                if (r > 1.0) return 1.0;
                                return r;
                            }
                            return 1.0;
                        };

                        auto bm_it = modifier_map.find(building.id);

                        auto add_output = [&](const std::string& res, const json& amount_val,
                                              const json& inputs_obj, int min_level) {
                            if (building.level < min_level) return;
                            double rate = rate_for(res);
                            if (rate <= 0.0) return;
                            double total_amount = compute_amount(amount_val) * rate;
                            if (total_amount <= 0) return;
                            if (bm_it != modifier_map.end()) {
                                auto rm_it = bm_it->second.find(res);
                                if (rm_it != bm_it->second.end()) total_amount *= rm_it->second;
                            }
                            OutputPlan op;
                            op.resource = res;
                            op.amount = total_amount;
                            op.rate = rate;
                            if (inputs_obj.is_object()) {
                                for (auto& [ir, ispec] : inputs_obj.items()) {
                                    double required = compute_amount(ispec) * rate;
                                    if (required > 0) op.inputs[ir] += required;
                                }
                            }
                            plan.outputs.push_back(std::move(op));
                        };

                        if (type_config.contains("outputs") && type_config["outputs"].is_array()) {
                            // New multi-output schema: per-output inputs + min_level.
                            for (const auto& out : type_config["outputs"]) {
                                if (!out.is_object()) continue;
                                std::string res = out.value("resource", "");
                                if (res.empty() || !out.contains("amount")) continue;
                                json inputs_obj = json::object();
                                if (out.contains("inputs")) inputs_obj = out["inputs"];
                                int min_level = out.value("min_level", 1);
                                add_output(res, out["amount"], inputs_obj, min_level);
                            }
                        } else {
                            // Legacy flat schema: each production resource field is an
                            // output; building-level `inputs` apply to every output.
                            const std::vector<std::string> legacy_prod = {
                                "peasants", "gold", "grain", "wood", "steel", "bronze",
                                "stone", "leather", "mana", "charcoal", "iron", "ironwork", "fancy_ironwork"};
                            for (const auto& resource : legacy_prod) {
                                if (!type_config.contains(resource)) continue;
                                json inputs_obj = json::object();
                                if (type_config.contains("inputs")) inputs_obj = type_config["inputs"];
                                add_output(resource, type_config[resource], inputs_obj, 1);
                            }
                        }
                        break;
                    }
                }

                // Consumption entries: building inputs + daily costs + population costs
                struct ConsEntry {
                    std::string resource;
                    double amount;
                    int priority;
                    int building_id;       // 0 = not tied to a building input
                    std::string output_res; // output resource this input feeds ("" if not an input)
                    bool is_input;         // counts toward input satisfaction
                };
                std::vector<ConsEntry> entries;

                auto add_entry = [&](const std::string& res, double amount, int prio, int building_id,
                                     const std::string& output_res, bool is_input) {
                    if (amount > 0) entries.push_back({res, amount, prio, building_id, output_res, is_input});
                };

                for (const auto& building : fiefdom.buildings) {
                    if (building.level <= 0) continue;
                    json bld_cfg;
                    for (const auto& obj : building_types) {
                        if (obj.contains(building.name)) { bld_cfg = obj[building.name]; break; }
                    }
                    if (bld_cfg.is_null()) continue;
                    int prio = bld_cfg.value("priority", default_prio);

                    auto plan_it = plans.find(building.id);
                    if (plan_it != plans.end()) {
                        for (const auto& op : plan_it->second.outputs) {
                            for (auto& [res, req] : op.inputs) {
                                add_entry(res, req, prio, building.id, op.resource, true);
                            }
                        }
                    }
                    if (bld_cfg.contains("daily_cost")) {
                        auto costs = bld_cfg["daily_cost"];
                        for (auto& [res, rate] : costs.items()) {
                            add_entry(res, rate.get<double>() * days_elapsed, prio, 0, "", false);
                        }
                    }
                }

                for (auto& [pop_type, costs] : pop_costs.items()) {
                    int count = 0;
                    if (pop_type == "peasants") count = cur_peasants;
                    if (count <= 0) continue;
                    int prio = costs.value("priority", 1);
                    for (auto& [res, rate] : costs.items()) {
                        if (res == "priority") continue;
                        add_entry(res, rate.get<double>() * count * days_elapsed, prio, 0, "", false);
                    }
                }

                // 3. Combatant upkeep (stationed units consume ironwork etc.)
                {
                    auto& combatant_registry = Combatants::CombatantRegistry::getInstance();
                    for (const auto& combatant : fiefdom.stationed_combatants) {
                        auto combatant_opt = combatant_registry.getPlayerCombatant(combatant.combatant_config_id);
                        if (!combatant_opt) continue;
                        auto upkeep = (*combatant_opt)->getUpkeep(combatant.level);
                        if (upkeep.gold > 0) add_entry("gold", upkeep.gold * days_elapsed, upkeep_prio, 0, "", false);
                        if (upkeep.grain > 0) add_entry("grain", upkeep.grain * days_elapsed, upkeep_prio, 0, "", false);
                        if (upkeep.wood > 0) add_entry("wood", upkeep.wood * days_elapsed, upkeep_prio, 0, "", false);
                        if (upkeep.steel > 0) add_entry("steel", upkeep.steel * days_elapsed, upkeep_prio, 0, "", false);
                        if (upkeep.bronze > 0) add_entry("bronze", upkeep.bronze * days_elapsed, upkeep_prio, 0, "", false);
                        if (upkeep.stone > 0) add_entry("stone", upkeep.stone * days_elapsed, upkeep_prio, 0, "", false);
                        if (upkeep.leather > 0) add_entry("leather", upkeep.leather * days_elapsed, upkeep_prio, 0, "", false);
                        if (upkeep.charcoal > 0) add_entry("charcoal", upkeep.charcoal * days_elapsed, upkeep_prio, 0, "", false);
                        if (upkeep.iron > 0) add_entry("iron", upkeep.iron * days_elapsed, upkeep_prio, 0, "", false);
                        if (upkeep.ironwork > 0) add_entry("ironwork", upkeep.ironwork * days_elapsed, upkeep_prio, 0, "", false);
                    }
                }

                std::sort(entries.begin(), entries.end(),
                    [](const ConsEntry& a, const ConsEntry& b) { return a.priority < b.priority; });

                // Ledger
                json ledger_produced = json::object();
                json ledger_consumed = json::object();
                json ledger_imported = json::object();
                json ledger_exported = json::object();
                double import_spend = 0;
                double gold_consumed = 0;
                int64_t import_spend_pence = 0;
                int64_t export_gain_pence = 0;

                double morale_damage = 0;

                // Record how much of an input was actually supplied to the output
                // that consumes it (stock + imports).
                auto record_supplied = [&](const ConsEntry& entry, double amount) {
                    auto pit = plans.find(entry.building_id);
                    if (pit == plans.end()) return;
                    for (auto& op : pit->second.outputs) {
                        if (op.resource == entry.output_res) {
                            op.supplied[entry.resource] += amount;
                            return;
                        }
                    }
                };

                for (const auto& entry : entries) {
                    if (entry.resource == "gold") continue;
                    double* avail = get_cur(entry.resource);
                    if (!avail) continue;
                    double effective = std::min(*avail, entry.amount);
                    *avail -= effective;
                    ledger_consumed[entry.resource] = ledger_consumed.value(entry.resource, 0.0) + effective;
                    if (entry.is_input) {
                        record_supplied(entry, effective);
                    }
                    double unmet = entry.amount - effective;

                    if (unmet > 0.001) {
                        bool auto_import = true;
                        if (fiefdom.import_settings.is_object() &&
                            fiefdom.import_settings.contains(entry.resource)) {
                            auto_import = fiefdom.import_settings[entry.resource].get<bool>();
                        }
                        if (auto_import) {
                            json price_entry;
                            if (import_prices.contains(entry.resource)) {
                                price_entry = import_prices[entry.resource];
                            }
                            if (price_entry.is_object()) {
                                // Penny market (e.g. grain): imports are paid from
                                // the silver_pence wallet instead of gold.
                                int64_t pence_price = money_price_to_pence(price_entry);
                                if (pence_price > 0 && cur_silver_pence > 0) {
                                    double affordable = std::min(unmet, std::floor((double)cur_silver_pence / (double)pence_price));
                                    if (affordable >= 1.0) {
                                        int64_t cost_pence = static_cast<int64_t>(affordable) * pence_price;
                                        cur_silver_pence -= cost_pence;
                                        *avail += affordable;
                                        unmet -= affordable;
                                        import_spend_pence += cost_pence;
                                        ledger_imported[entry.resource] = ledger_imported.value(entry.resource, 0.0) + affordable;
                                        ledger_consumed[entry.resource] = ledger_consumed.value(entry.resource, 0.0) + affordable;
                                        if (entry.is_input) {
                                            record_supplied(entry, affordable);
                                        }
                                    }
                                }
                            } else {
                                double price = price_entry.is_number() ? price_entry.get<double>() : 2.0;
                                double affordable = std::min(unmet, std::floor(cur_gold / price));
                                if (affordable >= 1.0) {
                                    cur_gold -= affordable * price;
                                    *avail += affordable;
                                    unmet -= affordable;
                                    import_spend += affordable * price;
                                    ledger_imported[entry.resource] = ledger_imported.value(entry.resource, 0.0) + affordable;
                                    ledger_consumed[entry.resource] = ledger_consumed.value(entry.resource, 0.0) + affordable;
                                    if (entry.is_input) {
                                        record_supplied(entry, affordable);
                                    }
                                }
                            }
                        }
                        if (unmet > 0.001) {
                            morale_damage += unmet * unmet_penalty;
                        }
                    }
                }

                // Apply production gated by each output's own input satisfaction
                for (auto& [bld_id, plan] : plans) {
                    for (auto& op : plan.outputs) {
                        double ratio = 1.0;
                        if (!op.inputs.empty()) {
                            for (auto& [res, req] : op.inputs) {
                                double supplied = op.supplied.count(res) ? op.supplied[res] : 0.0;
                                double r = (req > 0) ? std::min(1.0, supplied / req) : 1.0;
                                ratio = std::min(ratio, r);
                            }
                        }
                        const std::string& res = op.resource;
                        double produced = op.amount * ratio;
                        if (produced <= 0) continue;
                        if (res == "peasants") {
                            cur_peasants += static_cast<int>(produced);
                        } else {
                            double* target = get_cur(res);
                            if (target) *target += produced;
                        }
                        ledger_produced[res] = ledger_produced.value(res, 0.0) + produced;

                        ProductionUpdate pu;
                        pu.resource_type = res;
                        pu.amount_produced = produced;
                        pu.source_type = "building";
                        pu.source_id = bld_id;
                        pu.fiefdom_id = fiefdom.id;
                        result.productions.push_back(pu);
                    }
                }

                // Sell excess above reserve -> gold or silver pence. The per-unit
                // sell price resolves with precedence: export_prices (explicit) →
                // export_sell_multipliers (per-resource ratio) → global
                // export_sell_multiplier (default 0.5 × import price).
                {
                    const std::vector<std::string> sellable = {"grain", "wood", "steel", "bronze", "stone", "leather", "mana", "charcoal", "iron", "ironwork", "fancy_ironwork"};
                    for (const auto& res : sellable) {
                        double reserve = 0;
                        if (fiefdom.reserves.is_object() && fiefdom.reserves.contains(res) && fiefdom.reserves[res].is_number()) {
                            reserve = fiefdom.reserves[res].get<double>();
                        } else if (default_reserves.contains(res) && default_reserves[res].is_number()) {
                            reserve = default_reserves[res].get<double>();
                        }
                        double* cur = get_cur(res);
                        if (!cur) continue;
                        if (*cur <= reserve + 0.001) continue;
                        double excess = *cur - reserve;

                        json import_price;
                        if (import_prices.contains(res)) {
                            import_price = import_prices[res];
                        }
                        // Money-form import price ⇒ penny market (silver_pence wallet).
                        bool pence_market = import_price.is_object();

                        double unit_value = 0.0;
                        if (export_prices.contains(res)) {
                            const json& ep = export_prices[res];
                            if (ep.is_object()) {
                                pence_market = true;
                                unit_value = static_cast<double>(money_price_to_pence(ep));
                            } else if (ep.is_number()) {
                                pence_market = false;
                                unit_value = ep.get<double>();
                            }
                        } else {
                            double ratio = export_sell_multiplier;
                            if (export_sell_multipliers.contains(res) && export_sell_multipliers[res].is_number()) {
                                ratio = export_sell_multipliers[res].get<double>();
                            }
                            if (pence_market) {
                                unit_value = static_cast<double>(money_price_to_pence(import_price)) * ratio;
                            } else {
                                double price = import_price.is_number() ? import_price.get<double>() : 2.0;
                                unit_value = price * ratio;
                            }
                        }

                        if (pence_market) {
                            int64_t pence_earned = static_cast<int64_t>(std::llround(excess * unit_value));
                            *cur = reserve;
                            cur_silver_pence += pence_earned;
                            export_gain_pence += pence_earned;
                            json entry = json::object();
                            entry["amount"] = excess;
                            entry["pence"] = pence_earned;
                            ledger_exported[res] = entry;
                        } else {
                            double gold_earned = excess * unit_value;
                            *cur = reserve;
                            cur_gold += gold_earned;
                            json entry = json::object();
                            entry["amount"] = excess;
                            entry["gold"] = gold_earned;
                            ledger_exported[res] = entry;
                        }
                    }
                }

                // Gold consumption (last — imports already deducted)
                {
                    double gold_needed = 0;
                    for (const auto& building : fiefdom.buildings) {
                        if (building.level <= 0) continue;
                        json bld_cfg;
                        for (const auto& obj : building_types) {
                            if (obj.contains(building.name)) { bld_cfg = obj[building.name]; break; }
                        }
                        if (bld_cfg.is_null() || !bld_cfg.contains("daily_cost")) continue;
                        auto costs = bld_cfg["daily_cost"];
                        if (!costs.contains("gold")) continue;
                        gold_needed += costs["gold"].get<double>() * days_elapsed;
                    }
                    if (gold_needed > 0) {
                        double effective = std::min(cur_gold, gold_needed);
                        cur_gold -= effective;
                        gold_consumed = effective;
                        double unmet = gold_needed - effective;
                        if (unmet > 0.001) morale_damage += unmet * unmet_penalty * 2;
                    }
                }

                // Apply morale damage
                if (morale_damage > 0.001) {
                    double new_morale = std::max(-1000.0, std::min(1000.0, fiefdom.morale - morale_damage));
                    db << "UPDATE fiefdoms SET morale = ? WHERE id = ?;" << new_morale << fiefdom.id;
                    fiefdom.morale = new_morale;
                }

                // Write updated resources
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
                wr("charcoal", cur_charcoal);
                wr("iron", cur_iron);
                wr("ironwork", cur_ironwork);
                wr("fancy_ironwork", cur_fancy_ironwork);
                if (cur_silver_pence < 0) cur_silver_pence = 0;
                db << "UPDATE fiefdoms SET silver_pence = ? WHERE id = ?;" << cur_silver_pence << fiefdom.id;
                if (cur_peasants != fiefdom.peasants) {
                    db << "UPDATE fiefdoms SET peasants = ? WHERE id = ?;" << cur_peasants << fiefdom.id;
                }

                // Economy report + advisor
                json report;
                report["elapsed_seconds"] = result.new_timestamp - last_update_time;
                report["produced"] = ledger_produced;
                report["consumed"] = ledger_consumed;
                report["imported"] = ledger_imported;
                report["exported"] = ledger_exported;
                double gold_produced = ledger_produced.value("gold", 0.0);
                double export_gold = 0;
                for (auto& [res, val] : ledger_exported.items()) {
                    if (val.is_object() && val.contains("gold")) export_gold += val["gold"].get<double>();
                }
                report["net_gold"] = gold_produced + export_gold - import_spend - gold_consumed;
                report["net_silver"] = export_gain_pence - import_spend_pence;
                report["recommendations"] = build_economy_recommendations(
                    config_cache, ledger_produced, ledger_consumed, ledger_imported,
                    ledger_exported, plans, building_types);
                result.economy_reports[fiefdom.id] = report;
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
// Advisor: generate economic recommendations from the per-fiefdom ledger
// ---------------------------------------------------------------------------

/// Builds a list of recommendation strings from the economy ledger and building
/// configs. Called once per fiefdom per economy tick.
nlohmann::json build_economy_recommendations(
    GameConfigCache& config_cache,
    const nlohmann::json& produced,
    const nlohmann::json& consumed,
    const nlohmann::json& imported,
    const nlohmann::json& exported,
    const std::map<int, BuildingPlan>& plans,
    const nlohmann::json& building_types)
{
    nlohmann::json recs = nlohmann::json::array();
    auto& db = Database::getInstance().gameDB();

    (void)produced;
    (void)consumed;

    // 1. Heavy imports: suggest a producer building when one exists
    for (auto it = imported.begin(); it != imported.end(); ++it) {
        const std::string& res = it.key();
        double amount = it.value().get<double>();
        if (amount < 1.0) continue;

        // Find a building type that produces this resource
        std::string producer_name;
        std::string producer_id;
        for (const auto& type_obj : building_types) {
            for (auto b_it = type_obj.begin(); b_it != type_obj.end(); ++b_it) {
                const std::string& b_id = b_it.key();
                const json& cfg = b_it.value();
                if (cfg.is_object() && cfg.contains(res) && cfg[res].is_object() && cfg[res].contains("amount")) {
                    producer_id = b_id;
                    producer_name = cfg.value("display_name", b_id);
                    break;
                }
            }
            if (!producer_name.empty()) break;
        }

        if (!producer_name.empty()) {
            recs.push_back("You imported " + std::to_string(static_cast<int>(std::ceil(amount))) + " " + res +
                           " this period. A " + producer_name + " would produce " + res + " locally.");
        } else {
            recs.push_back("You imported " + std::to_string(static_cast<int>(std::ceil(amount))) + " " + res +
                           " this period.");
        }
    }

    // 2. Reduced production due to unmet inputs
    for (const auto& [bld_id, plan] : plans) {
        double worst_ratio = 1.0;
        for (const auto& op : plan.outputs) {
            if (op.inputs.empty()) continue;
            double ratio = 1.0;
            for (const auto& [res, req] : op.inputs) {
                double supplied = op.supplied.count(res) ? op.supplied.at(res) : 0.0;
                double r = (req > 0) ? std::min(1.0, supplied / req) : 1.0;
                ratio = std::min(ratio, r);
            }
            worst_ratio = std::min(worst_ratio, ratio);
        }
        if (worst_ratio < 0.999) {
            int pct = static_cast<int>(std::round(worst_ratio * 100.0));
            std::string name;
            db << "SELECT name FROM fiefdom_buildings WHERE id = ?;" << bld_id
               >> [&](std::string n) { name = n; };
            recs.push_back("A " + name + " only ran at " + std::to_string(pct) +
                           "% capacity — check its input supply.");
        }
    }

    // 3. Exported surplus (any resource sold above reserve)
    for (auto it = exported.begin(); it != exported.end(); ++it) {
        const std::string& res = it.key();
        if (!it.value().is_object()) continue;
        double amount = it.value().value("amount", 0.0);
        if (amount < 1.0) continue;
        if (it.value().contains("pence")) {
            int64_t pence = it.value().value("pence", 0);
            recs.push_back("Your manor exported " + std::to_string(static_cast<int>(std::ceil(amount))) +
                           " " + res + " for " + std::to_string(pence) + " pence.");
        } else {
            double gold = it.value().value("gold", 0.0);
            recs.push_back("Your manor exported " + std::to_string(static_cast<int>(std::ceil(amount))) +
                           " " + res + " for " + std::to_string(static_cast<int>(std::ceil(gold))) + " gold.");
        }
    }

    return recs;
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
