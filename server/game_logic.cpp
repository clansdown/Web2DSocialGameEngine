#include "game_logic.hpp"
#include "GameConfigCache.hpp"
#include "MoraleCalculator.hpp"
#include "WaterNetwork.hpp"
#include "FiefdomFetcher.hpp"
#include "ActionHandler.hpp"
#include "ActionHandlers.hpp"
#include "combatants.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
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
                                double gold, double silver_pence, int grain, int wood, int steel,
                                int bronze, int stone, int leather, int mana, int charcoal,
                                int iron, int ironwork, int fancy_ironwork, int beams, int boards,
                                int wall_count, double morale, std::string import_settings_str, std::string reserves_str) {
            FiefdomData f;
            f.id = id;
            f.owner_id = owner_id;
            f.name = name;
            f.x = x;
            f.y = y;
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
            f.beams = beams;
            f.boards = boards;
            f.wall_count = wall_count;
            f.morale = morale;
            try { f.import_settings = nlohmann::json::parse(import_settings_str); }
            catch (...) { f.import_settings = nlohmann::json::object(); }
            try { f.reserves = nlohmann::json::parse(reserves_str); }
            catch (...) { f.reserves = nlohmann::json::object(); }
            fiefdoms.push_back(f);
        };

        if (!fiefdom_filter_id.empty()) {
            db << "SELECT id, owner_id, name, x, y, gold, silver_pence, grain, wood, steel, bronze, stone, leather, mana, charcoal, iron, ironwork, fancy_ironwork, beams, boards, wall_count, morale, import_settings, reserves FROM fiefdoms WHERE id = ?;"
               << std::stoi(fiefdom_filter_id)
               >> read_fiefdom;
        } else {
            db << "SELECT id, owner_id, name, x, y, gold, silver_pence, grain, wood, steel, bronze, stone, leather, mana, charcoal, iron, ironwork, fancy_ironwork, beams, boards, wall_count, morale, import_settings, reserves FROM fiefdoms;"
               >> read_fiefdom;
        }
        
            for (auto& fiefdom : fiefdoms) {
            fiefdom.buildings = FiefdomFetcher::fetchFiefdomBuildings(fiefdom.id);
            fiefdom.walls = FiefdomFetcher::fetchFiefdomWalls(fiefdom.id);
            fiefdom.officials = FiefdomFetcher::fetchFiefdomOfficials(fiefdom.id);
            fiefdom.heroes = FiefdomFetcher::fetchFiefdomHeroes(fiefdom.id);
            fiefdom.stationed_combatants = FiefdomFetcher::fetchStationedCombatants(fiefdom.id);

            double time_factor = result.time_hours_elapsed;

            // Water power: water-powered buildings only produce while powered
            // (river -> pond -> head race -> building -> tail race -> river).
            // Computed here so both the modifier map (a watermill's flour_milling
            // boost only applies when powered) and the production loop gate on it.
            std::unordered_map<int, bool> water_powered_ok;
            {
                FiefdomFetcher::ensureFiefdomRiver(fiefdom.id, config_cache.getManorRiver());
                auto river_vec = FiefdomFetcher::fetchRiverCells(fiefdom.id);
                std::set<std::pair<int, int>> river_set(river_vec.begin(), river_vec.end());
                auto wp = Water::computeWaterPower(building_types, fiefdom.buildings, river_set);
                for (const auto& [bld_id, ok] : wp.powered) {
                    if (ok) water_powered_ok[bld_id] = true;
                }
            }

            // Pre-compute building-to-building modifiers for this fiefdom
            auto modifier_map = computeBuildingModifiers(fiefdom.buildings, building_types, water_powered_ok);

            for (auto& building : fiefdom.buildings) {
                if (building.construction_start_ts > 0) {
                    auto config_opt = Validation::getBuildingConfig(config_cache, building.name);
                    if (config_opt) {
                        auto config = *config_opt;
                        int construction_seconds = 0;

                        auto ct_array = Validation::getBuildingArrayField(config_cache, building.name, building.pond_type, "construction_times");
                        if (!ct_array.empty()) {
                            construction_seconds = getIntForLevel(ct_array, building.level, 0);
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
                                        prerequisites_met = Validation::checkFiefdomPrerequisites(config_cache, fiefdom.id, *prerequisites_opt);
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
                                        // The manor house's level drives the fiefdom's manor_level,
                                        // which gates building-type prerequisites ({"manor_level": N}).
                                        if (building.name == "home_base") {
                                            db << "UPDATE fiefdoms SET manor_level = ? WHERE id = ?;"
                                               << new_level << fiefdom.id;
                                            fiefdom.manor_level = new_level;
                                        }
                                        result.completed_trainings.push_back({building.name, new_level});
                                    }
                                } else {
                                    nlohmann::json refund;
                                    std::string cost_fields[] = {"gold_cost", "silver_pence_cost", "wood_cost", "stone_cost", "steel_cost",
                                                                 "bronze_cost", "grain_cost", "leather_cost", "mana_cost",
                                                                 "charcoal_cost", "iron_cost", "ironwork_cost", "fancy_ironwork_cost",
                                                                 "beams_cost", "boards_cost"};
                                    std::string resource_fields[] = {"gold", "silver_pence", "wood", "stone", "steel",
                                                                     "bronze", "grain", "leather", "mana",
                                                                     "charcoal", "iron", "ironwork", "fancy_ironwork",
                                                                     "beams", "boards"};
                                    
                                    for (size_t i = 0; i < 15; i++) {
                                        const auto& cost_key = cost_fields[i];
                                        const auto& resource_key = resource_fields[i];
                                        
                                        auto costs = Validation::getBuildingArrayField(config_cache, building.name, building.pond_type, cost_key);
                                        if (costs.is_array() && !costs.empty()) {
                                            // Refund the cost actually paid for the failed step:
                                            // create paid costs[0], upgrade from L charged costs[L].
                                            int level_index = old_level;
                                            if (level_index >= 0 && level_index < static_cast<int>(costs.size())) {
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

            // ---- Economy: production (dependency graph) -> upkeep -> import -> exports ----
            {
                auto economy_cfg = config_cache.getEconomyConfig();
                double unmet_penalty = economy_cfg.value("unmet_need_morale_penalty", 0.5);
                double export_sell_multiplier = economy_cfg.value("export_sell_multiplier", 0.5);
                int default_prio = economy_cfg.value("default_priority", 50);
                auto import_prices = economy_cfg.value("import_prices", json::object());
                auto export_prices = economy_cfg.value("export_prices", json::object());
                auto export_sell_multipliers = economy_cfg.value("export_sell_multipliers", json::object());
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
                // Fractional-capable (REAL) to support half-penny prices (e.g.
                // boards at 0.5d/1.5d).
                double cur_silver_pence = fiefdom.silver_pence;

                // Convert a money-form import price ({gold, shillings, pence}) to
                // pence (fractional-capable). Returns 0 for plain-number
                // (gold-denominated) prices.
                auto money_price_to_pence = [&](const json& price) -> double {
                    if (!price.is_object()) return 0.0;
                    double gold = price.value("gold", 0.0);
                    double shillings = price.value("shillings", 0.0);
                    double pence = price.value("pence", 0.0);
                    return gold * pence_per_gold + shillings * pence_per_shilling + pence;
                };

                // Current resource stock (gold is fractional-capable)
                double cur_gold = 0, cur_grain = 0, cur_wood = 0, cur_steel = 0;
                double cur_bronze = 0, cur_stone = 0, cur_leather = 0, cur_mana = 0;
                double cur_charcoal = 0, cur_iron = 0, cur_ironwork = 0, cur_fancy_ironwork = 0;
                double cur_beams = 0, cur_boards = 0;
                db << "SELECT gold, grain, wood, steel, bronze, stone, leather, mana, charcoal, iron, ironwork, fancy_ironwork, beams, boards "
                      "FROM fiefdoms WHERE id = ?;" << fiefdom.id
                   >> [&](double g, double gr, double w, double s,
                          double b, double st, double l, double m, double ch, double ir, double iw, double fiw,
                          double bm, double bd) {
                       cur_gold = g; cur_grain = gr;
                       cur_wood = w; cur_steel = s; cur_bronze = b;
                       cur_stone = st; cur_leather = l; cur_mana = m;
                       cur_charcoal = ch; cur_iron = ir; cur_ironwork = iw;
                       cur_fancy_ironwork = fiw;
                       cur_beams = bm; cur_boards = bd;
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
                    if (r == "beams") return &cur_beams;
                    if (r == "boards") return &cur_boards;
                    return nullptr;
                };

                // Per-day amount for a production/input spec over the elapsed
                // period. All production and consumption share the same 1-day
                // period; fractional elapsed days are used directly (no
                // flooring) so any elapsed time beyond a tiny minimum computes
                // a proportional amount.
                //
                // The spec's value may be:
                //   - a plain number (gold-denominated for the "gold" resource)
                //   - { "amount": <value> } (legacy wrapper)
                //   - a money object { "gold", "shillings", "pence" } — normalized
                //     to gold, matching the cost/import money-object convention
                //   - a level-indexed array of numbers or money objects — resolved
                //     by the building's level (index level-1, linear extrapolation)
                auto to_gold_amount = [&](const json& v) -> double {
                    if (v.is_object()) {
                        return v.value("gold", 0.0)
                             + v.value("shillings", 0.0) / shillings_per_pound
                             + v.value("pence", 0.0) / pence_per_gold;
                    }
                    return v.get<double>();
                };
                auto compute_amount = [&](const json& spec, int level) -> double {
                    double days = time_factor / 24.0;
                    if (days <= 0) return 0.0;
                    json raw;
                    if (spec.is_number()) {
                        raw = spec;
                    } else if (spec.is_object()) {
                        raw = spec.value("amount", json(0.0));
                    } else {
                        return 0.0;
                    }
                    if (raw.is_array()) {
                        if (raw.empty()) return 0.0;
                        int idx = std::max(0, level - 1);
                        int max_index = static_cast<int>(raw.size()) - 1;
                        if (idx <= max_index) {
                            return to_gold_amount(raw[idx]) * days;
                        }
                        if (max_index >= 1) {
                            double last = to_gold_amount(raw[max_index]);
                            double prev = to_gold_amount(raw[max_index - 1]);
                            return (last + (last - prev) * (idx - max_index)) * days;
                        }
                        return to_gold_amount(raw[0]) * days;
                    }
                    return to_gold_amount(raw) * days;
                };

                // Per-building production/input plan. Each building produces one
                // or more outputs; each output may have its own inputs, an unlock
                // level (min_level) and a player-controlled rate (0..1, default
                // 1.0). Rate 0 disables the output entirely (no inputs consumed).
                std::map<int, BuildingPlan> plans;

                // Road-network morale: buildings connected to roads near a
                // morale-source (e.g. chapel/miller) get a production boost.
                // multiplier = 1 + morale_points * morale_production_multiplier.
                std::unordered_map<int, double> road_morale_multipliers;
                {
                    auto road_points = Morale::computeRoadMoralePoints(building_types, fiefdom.buildings);
                    double morale_per_point = economy_cfg.value("morale_production_multiplier", 0.0);
                    if (morale_per_point > 0.0 && !road_points.empty()) {
                        for (const auto& [bld_id, points] : road_points) {
                            if (points > 0.0) {
                                road_morale_multipliers[bld_id] = 1.0 + points * morale_per_point;
                            }
                        }
                    }
                }

                // Water power gating is handled by `water_powered_ok`, computed
                // once per fiefdom before the modifier map (see above).

                for (const auto& building : fiefdom.buildings) {
                    if (building.level <= 0) continue;
                    for (const auto& type_obj : building_types) {
                        if (!type_obj.contains(building.name)) continue;
                        auto type_config = type_obj[building.name];

                        // Unpowered water-powered buildings produce nothing (they
                        // still pay daily_cost like any other building).
                        if (type_config.value("water_powered", false) && !water_powered_ok.count(building.id)) {
                            break;
                        }

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
                            double total_amount = compute_amount(amount_val, building.level) * rate;
                            if (total_amount <= 0) return;
                            if (bm_it != modifier_map.end()) {
                                auto rm_it = bm_it->second.find(res);
                                if (rm_it != bm_it->second.end()) total_amount *= rm_it->second;
                            }
                            // Road-network morale boosts all outputs of a
                            // connected building (multiplier >= 1).
                            auto mm_it = road_morale_multipliers.find(building.id);
                            if (mm_it != road_morale_multipliers.end()) {
                                total_amount *= mm_it->second;
                            }
                            OutputPlan op;
                            op.resource = res;
                            op.amount = total_amount;
                            op.rate = rate;
                            if (inputs_obj.is_object()) {
                                for (auto& [ir, ispec] : inputs_obj.items()) {
                                    double required = compute_amount(ispec, building.level) * rate;
                                    if (required > 0) op.inputs[ir] += required;
                                }
                            }
                            plan.outputs.push_back(std::move(op));
                        };

                        if (type_config.contains("outputs") && type_config["outputs"].is_array()) {
                            // The `outputs` array is the canonical production schema:
                            // per-output inputs + min_level. The legacy flat-schema
                            // path was removed; every production building defines an
                            // `outputs` array (config lint enforces this).
                            for (const auto& out : type_config["outputs"]) {
                                if (!out.is_object()) continue;
                                std::string res = out.value("resource", "");
                                if (res.empty() || !out.contains("amount")) continue;
                                json inputs_obj = json::object();
                                if (out.contains("inputs")) inputs_obj = out["inputs"];
                                int min_level = out.value("min_level", 1);
                                add_output(res, out["amount"], inputs_obj, min_level);
                            }
                        }
                        break;
                    }
                }

                // Ledger
                json ledger_produced = json::object();
                json ledger_consumed = json::object();
                json ledger_imported = json::object();
                json ledger_exported = json::object();
                double import_spend = 0;
                double gold_consumed = 0;
                double import_spend_pence = 0;
                double export_gain_pence = 0;

                double morale_damage = 0;

                // Consumes a need (production input or upkeep) from stock,
                // importing the deficit if the resource's import is enabled and
                // affordable (gold for gold-market, silver_pence for penny-market).
                // Records consumed/imported amounts and morale damage for unmet
                // need. Returns the amount actually supplied (stock + imports).
                auto supply_need = [&](const std::string& res, double need) -> double {
                    if (res == "gold" || need <= 0.001) return 0.0;
                    double* cur = get_cur(res);
                    if (!cur) {
                        if (need > 0.001) morale_damage += need * unmet_penalty;
                        return 0.0;
                    }
                    double effective = std::min(*cur, need);
                    *cur -= effective;
                    ledger_consumed[res] = ledger_consumed.value(res, 0.0) + effective;
                    double unmet = need - effective;
                    double supplied = effective;
                    if (unmet > 0.001) {
                        bool auto_import = true;
                        if (fiefdom.import_settings.is_object() &&
                            fiefdom.import_settings.contains(res)) {
                            auto_import = fiefdom.import_settings[res].get<bool>();
                        }
                        if (auto_import) {
                            json price_entry;
                            if (import_prices.contains(res)) price_entry = import_prices[res];
                            if (price_entry.is_object()) {
                                // Penny market (e.g. grain): imports are paid from
                                // the fungible silver+gold wallet (gold converts to
                                // pence at pence_per_gold; silver spent first).
                                double pence_price = money_price_to_pence(price_entry);
                                double total_pence = cur_silver_pence + cur_gold * pence_per_gold;
                                if (pence_price > 0 && total_pence > 0) {
                                    double affordable = std::min(unmet, std::floor(total_pence / pence_price));
                                    if (affordable >= 1.0) {
                                        double cost_pence = affordable * pence_price;
                                        double from_silver = std::min(cur_silver_pence, cost_pence);
                                        cur_silver_pence -= from_silver;
                                        cur_gold -= (cost_pence - from_silver) / pence_per_gold;
                                        *cur += affordable;
                                        unmet -= affordable;
                                        supplied += affordable;
                                        import_spend_pence += cost_pence;
                                        ledger_imported[res] = ledger_imported.value(res, 0.0) + affordable;
                                        ledger_consumed[res] = ledger_consumed.value(res, 0.0) + affordable;
                                    }
                                }
                            } else {
                                double price = price_entry.is_number() ? price_entry.get<double>() : 2.0;
                                double affordable = std::min(unmet, std::floor(cur_gold / price));
                                if (affordable >= 1.0) {
                                    cur_gold -= affordable * price;
                                    *cur += affordable;
                                    unmet -= affordable;
                                    supplied += affordable;
                                    import_spend += affordable * price;
                                    ledger_imported[res] = ledger_imported.value(res, 0.0) + affordable;
                                    ledger_consumed[res] = ledger_consumed.value(res, 0.0) + affordable;
                                }
                            }
                        }
                        if (unmet > 0.001) morale_damage += unmet * unmet_penalty;
                    }
                    return supplied;
                };

                // Building priorities (used as a deterministic tiebreak when
                // several buildings are ready at the same time).
                std::map<int, int> building_prio;
                for (const auto& building : fiefdom.buildings) {
                    if (building.level <= 0) continue;
                    json bld_cfg;
                    for (const auto& obj : building_types) {
                        if (obj.contains(building.name)) { bld_cfg = obj[building.name]; break; }
                    }
                    if (!bld_cfg.is_null()) building_prio[building.id] = bld_cfg.value("priority", default_prio);
                }

                // Dependency graph over producing buildings. Edge A -> B means A
                // produces a resource that B consumes as a production input, so A
                // must run before B. daily_cost / population / combatant upkeep are
                // NOT edges — they are consumed in the upkeep phase after all
                // production. Resource flows are acyclic, so a topological order
                // always exists.
                std::map<std::string, std::set<int>> producers;
                for (const auto& [bid, plan] : plans) {
                    for (const auto& op : plan.outputs) producers[op.resource].insert(bid);
                }
                std::map<int, std::set<int>> deps;
                std::map<int, int> indeg;
                for (const auto& [bid, plan] : plans) { deps[bid] = {}; indeg[bid] = 0; }
                for (const auto& [bid, plan] : plans) {
                    std::set<int> upstream;
                    for (const auto& op : plan.outputs) {
                        for (const auto& [res, req] : op.inputs) {
                            if (req <= 0.001) continue;
                            auto pit = producers.find(res);
                            if (pit == producers.end()) continue;
                            for (int prod : pit->second) if (prod != bid) upstream.insert(prod);
                        }
                    }
                    deps[bid] = upstream;
                    indeg[bid] = static_cast<int>(upstream.size());
                }

                // Kahn's topological order with priority (then building id) tiebreak.
                std::vector<int> order;
                {
                    std::set<int> remaining;
                    for (const auto& [bid, plan] : plans) remaining.insert(bid);
                    while (!remaining.empty()) {
                        int chosen = -1;
                        int chosen_prio = 0;
                        for (int bid : remaining) {
                            if (indeg[bid] != 0) continue;
                            int p = building_prio.count(bid) ? building_prio[bid] : default_prio;
                            if (chosen == -1 || p < chosen_prio || (p == chosen_prio && bid < chosen)) {
                                chosen = bid;
                                chosen_prio = p;
                            }
                        }
                        if (chosen == -1) {
                            // Cycle (shouldn't happen — resources are acyclic).
                            // Break to avoid an infinite loop; the remaining
                            // buildings simply do not produce this tick.
                            break;
                        }
                        order.push_back(chosen);
                        remaining.erase(chosen);
                        for (int bid : remaining) {
                            if (deps[bid].count(chosen)) indeg[bid]--;
                        }
                    }
                }

                // Produce phase: process buildings in dependency order so a
                // building's inputs are available (from on-hand stock, upstream
                // production this tick, or imports) before it runs.
                for (int bid : order) {
                    auto pit = plans.find(bid);
                    if (pit == plans.end()) continue;
                    // Consume production inputs and record what was supplied.
                    for (auto& op : pit->second.outputs) {
                        for (const auto& [res, req] : op.inputs) {
                            if (req <= 0.001) continue;
                            op.supplied[res] = supply_need(res, req);
                        }
                    }
                    // Produce each output gated by its own input satisfaction.
                    for (auto& op : pit->second.outputs) {
                        double ratio = 1.0;
                        if (!op.inputs.empty()) {
                            for (const auto& [res, req] : op.inputs) {
                                double supplied = op.supplied.count(res) ? op.supplied[res] : 0.0;
                                double r = (req > 0) ? std::min(1.0, supplied / req) : 1.0;
                                ratio = std::min(ratio, r);
                            }
                        }
                        const std::string& res = op.resource;
                        double produced = op.amount * ratio;
                        if (produced <= 0) continue;
                        double* target = get_cur(res);
                        if (target) *target += produced;
                        ledger_produced[res] = ledger_produced.value(res, 0.0) + produced;

                        ProductionUpdate pu;
                        pu.resource_type = res;
                        pu.amount_produced = produced;
                        pu.source_type = "building";
                        pu.source_id = bid;
                        pu.fiefdom_id = fiefdom.id;
                        result.productions.push_back(pu);
                    }
                }

                // Upkeep phase: consume daily_cost, population costs, and
                // combatant upkeep from stock AFTER all production, so a
                // building's own (or any building's) output can feed it today.
                for (const auto& building : fiefdom.buildings) {
                    if (building.level <= 0) continue;
                    json bld_cfg;
                    for (const auto& obj : building_types) {
                        if (obj.contains(building.name)) { bld_cfg = obj[building.name]; break; }
                    }
                    if (bld_cfg.is_null() || !bld_cfg.contains("daily_cost")) continue;
                    for (auto& [res, rate] : bld_cfg["daily_cost"].items()) {
                        supply_need(res, rate.get<double>() * days_elapsed);
                    }
                }
                {
                    auto& combatant_registry = Combatants::CombatantRegistry::getInstance();
                    for (const auto& combatant : fiefdom.stationed_combatants) {
                        auto combatant_opt = combatant_registry.getPlayerCombatant(combatant.combatant_config_id);
                        if (!combatant_opt) continue;
                        auto upkeep = (*combatant_opt)->getUpkeep(combatant.level);
                        if (upkeep.gold > 0) supply_need("gold", upkeep.gold * days_elapsed);
                        if (upkeep.grain > 0) supply_need("grain", upkeep.grain * days_elapsed);
                        if (upkeep.wood > 0) supply_need("wood", upkeep.wood * days_elapsed);
                        if (upkeep.steel > 0) supply_need("steel", upkeep.steel * days_elapsed);
                        if (upkeep.bronze > 0) supply_need("bronze", upkeep.bronze * days_elapsed);
                        if (upkeep.stone > 0) supply_need("stone", upkeep.stone * days_elapsed);
                        if (upkeep.leather > 0) supply_need("leather", upkeep.leather * days_elapsed);
                        if (upkeep.charcoal > 0) supply_need("charcoal", upkeep.charcoal * days_elapsed);
                        if (upkeep.iron > 0) supply_need("iron", upkeep.iron * days_elapsed);
                        if (upkeep.ironwork > 0) supply_need("ironwork", upkeep.ironwork * days_elapsed);
                    }
                }

                // Sell excess above reserve -> gold or silver pence. The per-unit
                // sell price resolves with precedence: export_prices (explicit) →
                // export_sell_multipliers (per-resource ratio) → global
                // export_sell_multiplier (default 0.5 × import price).
                {
                    const std::vector<std::string> sellable = {"grain", "wood", "steel", "bronze", "stone", "leather", "mana", "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"};
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
                            double pence_earned = excess * unit_value;
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
                wr("beams", cur_beams);
                wr("boards", cur_boards);
                if (cur_silver_pence < 0) cur_silver_pence = 0;
                db << "UPDATE fiefdoms SET silver_pence = ? WHERE id = ?;" << cur_silver_pence << fiefdom.id;

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
    const json& building_types,
    const std::unordered_map<int, bool>& water_powered_ok
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

        // Unpowered water-powered sources (e.g. a watermill) contribute no
        // modifier until their water network is powered.
        if (type_config.value("water_powered", false) && !water_powered_ok.count(building.id)) {
            continue;
        }

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

        // Chain-aware target resolution: a modifier targeting a building boosts
        // any building whose stage chain includes the target (a sawyer/hewer
        // boosts the whole woodcutter chain: woodcutter/coppicer/timber_hauler).
        auto chain_of = [&](const std::string& name) -> std::vector<std::string> {
            std::vector<std::string> chain;
            std::string cur = name;
            std::unordered_set<std::string> seen;
            while (!cur.empty() && !seen.count(cur)) {
                seen.insert(cur);
                json cfg;
                for (const auto& obj : building_types) {
                    if (obj.contains(cur)) { cfg = obj[cur]; break; }
                }
                if (cfg.is_null()) break;
                chain.push_back(cur);
                std::string from = cfg.value("built_from", "");
                if (from.empty()) break;
                cur = from;
            }
            std::reverse(chain.begin(), chain.end());
            return chain;
        };
        auto counts_as = [&](const std::string& bname, const std::string& target) -> bool {
            for (const auto& c : chain_of(bname)) {
                if (c == target) return true;
            }
            // A `class` group is a definitive grouping independent of the
            // built_from chain (e.g. the "peasant" class covers villein/
            // freeholder/yeoman; flour_milling targets that class).
            for (const auto& type_obj : building_types) {
                if (type_obj.contains(bname)) {
                    const auto& tc = type_obj[bname];
                    if (tc.is_object() && tc.contains("class") && tc["class"].is_string() &&
                        tc["class"].get<std::string>() == target) {
                        return true;
                    }
                    break;
                }
            }
            return false;
        };

        std::vector<int> all_targets;
        for (const auto& building : buildings) {
            if (building.level > 0 && counts_as(building.name, target_building)) {
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
