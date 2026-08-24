#include "ActionHandlers.hpp"
#include "FiefdomFetcher.hpp"
#include "Database.hpp"
#include "GameConfigCache.hpp"
#include "GridCollision.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <set>

using json = nlohmann::json;

namespace GameLogic {

// === BuildActionHandler Implementation ===

ActionResult BuildActionHandler::validate(const json& payload, const ActionContext& ctx) {
    ActionResult result;
    
    if (!payload.contains("fiefdom_id")) {
        result.status = ActionStatus::FAIL;
        result.error_code = "fiefdom_id_required";
        result.error_message = "fiefdom_id is required";
        return result;
    }
    if (!payload.contains("building_type")) {
        result.status = ActionStatus::FAIL;
        result.error_code = "building_type_required";
        result.error_message = "building_type is required";
        return result;
    }
    
    int fiefdom_id = payload["fiefdom_id"];
    std::string building_type = payload["building_type"];
    
    if (!Validation::userOwnsFiefdom(ctx, fiefdom_id)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "not_owner";
        result.error_message = "User does not own this fiefdom";
        return result;
    }
    
    if (!Validation::buildingTypeExists(*ctx.config_cache, building_type)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "unknown_building";
        result.error_message = "Unknown building type: " + building_type;
        return result;
    }

    auto config_opt = Validation::getBuildingConfig(*ctx.config_cache, building_type);
    if (!config_opt) {
        result.status = ActionStatus::FAIL;
        result.error_code = "invalid_config";
        result.error_message = "Building configuration not found";
        return result;
    }

    auto config = *config_opt;
    std::string display_name = config.value("display_name", building_type);

    if (building_type == "home_base") {
        if (Validation::hasCompletedHomeBase(fiefdom_id)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "home_base_exists";
            result.error_message = "A " + display_name + " (home_base) already exists";
            return result;
        }
    } else {
        if (!Validation::hasCompletedHomeBase(fiefdom_id)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "home_base_required";
            result.error_message = "You must build a " + display_name + " (home_base) before other buildings";
            return result;
        }
    }

    if (config.contains("max_per_fiefdom") && config["max_per_fiefdom"].is_number()) {
        int max_count = config["max_per_fiefdom"].get<int>();
        if (max_count >= 1 && Validation::countBuildingsByType(*ctx.config_cache, fiefdom_id, building_type, 1) >= max_count) {
            result.status = ActionStatus::FAIL;
            result.error_code = "max_per_fiefdom_reached";
            result.error_message = "Only " + std::to_string(max_count) + " " + display_name + "(s) may be built in a fiefdom";
            return result;
        }
    }

    if (config.contains("prerequisites") && config["prerequisites"].is_array()) {
        auto prerequisites_opt = Validation::getPrerequisitesForLevel(*ctx.config_cache, building_type, 1);
        if (prerequisites_opt && !prerequisites_opt->empty()) {
            if (!Validation::checkFiefdomPrerequisites(*ctx.config_cache, fiefdom_id, *prerequisites_opt)) {
                result.status = ActionStatus::FAIL;
                result.error_code = "prerequisites_not_met";
                result.error_message = "Building prerequisites not satisfied";
                return result;
            }
        }
    }

    // Check building dependencies for the target level (level 1 = new build)
    {
        nlohmann::json deps = Validation::getDependenciesForLevel(*ctx.config_cache, building_type, 1);
        if (!deps.empty()) {
            auto all_buildings = Validation::getFiefdomAllBuildings(fiefdom_id);
            auto dep_result = Validation::checkBuildingDependencies(*ctx.config_cache, fiefdom_id, all_buildings, deps);
            if (!dep_result.first) {
                result.status = ActionStatus::FAIL;
                result.error_code = "dependencies_not_met";
                result.error_message = dep_result.second;
                return result;
            }
        }
    }

    // Arable land: the building must fit within the manor's remaining arable acres.
    {
        int required_acres = ctx.config_cache->getBuildingArableAcres(building_type);
        if (required_acres > 0) {
            double available = Validation::getAvailableArableAcres(*ctx.config_cache, fiefdom_id);
            if (available + 0.0001 < required_acres) {
                result.status = ActionStatus::FAIL;
                result.error_code = "insufficient_arable_land";
                result.error_message = "Not enough arable land to build a " + display_name;
                return result;
            }
        }
    }

    // Forest land: the wood producers (woodcutter chain) claim forest acres,
    // an off-map resource scaling with manor level like arable land.
    {
        int required_acres = ctx.config_cache->getBuildingForestAcres(building_type);
        if (required_acres > 0) {
            double available = Validation::getAvailableForestAcres(*ctx.config_cache, fiefdom_id);
            if (available + 0.0001 < required_acres) {
                result.status = ActionStatus::FAIL;
                result.error_code = "insufficient_forest_land";
                result.error_message = "Not enough forest land to build a " + display_name;
                return result;
            }
        }
    }

    if (!payload.contains("x") || !payload.contains("y")) {
        result.status = ActionStatus::FAIL;
        result.error_code = "coordinates_required";
        result.error_message = "x and y coordinates are required for building placement";
        return result;
    }

    int x = payload["x"];
    int y = payload["y"];

    if (!Validation::canBuildBuildingHere(*ctx.config_cache, building_type, fiefdom_id, x, y)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "invalid_location";
        result.error_message = "Cannot build at specified location";
        return result;
    }

    // Buildings may not be placed on the river. The mill pond must be adjacent
    // to (touching) the river, never overlapping it.
    {
        auto river_cells = FiefdomFetcher::fetchRiverCells(fiefdom_id);
        if (!river_cells.empty()) {
            int bw = config.value("width", 1);
            int bh = config.value("height", 1);
            GameLogic::GridCollision::Rect bld_rect(x, y, bw, bh);
            for (const auto& [rx, ry] : river_cells) {
                GameLogic::GridCollision::Rect river_rect(rx, ry, 1, 1);
                if (river_rect.overlaps(bld_rect)) {
                    result.status = ActionStatus::FAIL;
                    result.error_code = "invalid_location";
                    result.error_message = "Cannot build on the river";
                    return result;
                }
            }
        }
    }

    result.status = ActionStatus::OK;
    result.error_message = "OK";
    return result;
}

ActionResult BuildActionHandler::execute(const json& payload, const ActionContext& ctx) {
    ActionResult result;
    
    auto validate_result = validate(payload, ctx);
    if (validate_result.status != ActionStatus::OK) {
        return validate_result;
    }
    
    int fiefdom_id = payload["fiefdom_id"];
    std::string building_type = payload["building_type"];
    int x = payload.value("x", 0);
    int y = payload.value("y", 0);
    
    auto config = Validation::getBuildingConfig(*ctx.config_cache, building_type);
    if (!config) {
        result.status = ActionStatus::FAIL;
        result.error_code = "invalid_config";
        return result;
    }
    
    int64_t now = Validation::getCurrentTimestamp();
    
    try {
        Validation::TransactionGuard tx(Database::getInstance().gameDB());
        
        json costs;
        if (config->contains("gold_cost")) costs["gold"] = (*config)["gold_cost"][0];
        if (config->contains("wood_cost")) costs["wood"] = (*config)["wood_cost"][0];
        if (config->contains("stone_cost")) costs["stone"] = (*config)["stone_cost"][0];
        if (config->contains("silver_pence_cost")) costs["silver_pence"] = (*config)["silver_pence_cost"][0];
        if (config->contains("charcoal_cost")) costs["charcoal"] = (*config)["charcoal_cost"][0];
        if (config->contains("iron_cost")) costs["iron"] = (*config)["iron_cost"][0];
        if (config->contains("ironwork_cost")) costs["ironwork"] = (*config)["ironwork_cost"][0];
        if (config->contains("fancy_ironwork_cost")) costs["fancy_ironwork"] = (*config)["fancy_ironwork_cost"][0];
        if (config->contains("beams_cost")) costs["beams"] = (*config)["beams_cost"][0];
        if (config->contains("boards_cost")) costs["boards"] = (*config)["boards_cost"][0];
        
        auto deduct_result = Validation::deductResources(*ctx.config_cache, fiefdom_id, costs, result);
        if (deduct_result.status != ActionStatus::OK) {
            return deduct_result;
        }

        // Instant buildings (construction_times[0] == 0, e.g. roads) are created
        // at level 1 with no construction timer — the economy's completion gate
        // only fires for construction_seconds > 0, so a 0 build time would
        // otherwise leave the building stuck at level 0 forever.
        bool instant = false;
        if (config->contains("construction_times") && (*config)["construction_times"].is_array() &&
            !(*config)["construction_times"].empty()) {
            instant = ((*config)["construction_times"][0].get<double>() == 0);
        }
        int create_level = instant ? 1 : 0;
        int64_t construction_start = instant ? 0 : now;

        if (!FiefdomFetcher::createBuilding(fiefdom_id, building_type, create_level, construction_start, 0, "", x, y)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "database_error";
            result.error_message = "Failed to create building";
            return result;
        }

        result.result["building_type"] = building_type;
        result.result["fiefdom_id"] = fiefdom_id;
        result.result["x"] = x;
        result.result["y"] = y;
        result.result["construction_start_ts"] = construction_start;
        result.result["level"] = create_level;
        
        tx.commit();
        result.status = ActionStatus::OK;
        result.action_timestamp = now;
        return result;
        
    } catch (const std::exception& e) {
        result.status = ActionStatus::FAIL;
        result.error_code = "database_error";
        result.error_message = std::string(e.what());
        return result;
    }
}

class DemolishActionHandler : public ActionHandler {
public:
    ActionResult validate(const json& payload, const ActionContext& ctx) {
        ActionResult result;

        if (!payload.contains("building_id")) {
            result.status = ActionStatus::FAIL;
            result.error_code = "building_id_required";
            result.error_message = "building_id is required";
            return result;
        }

        int building_id = payload["building_id"];

        if (!Validation::userOwnsBuilding(building_id, ctx)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "not_owner";
            result.error_message = "User does not own this building";
            return result;
        }

        std::string building_name;
        auto& db = Database::getInstance().gameDB();
        db << "SELECT name FROM fiefdom_buildings WHERE id = ?;"
           << building_id
           >> [&](std::string name) { building_name = name; };

        if (building_name == "home_base") {
            result.status = ActionStatus::FAIL;
            result.error_code = "home_base_immutable";
            result.error_message = "Manor House (home_base) cannot be demolished";
            return result;
        }

        result.status = ActionStatus::OK;
        return result;
    }

    ActionResult execute(const json& payload, const ActionContext& ctx) {
        ActionResult result;

        auto validate_result = validate(payload, ctx);
        if (validate_result.status != ActionStatus::OK) return validate_result;

        int building_id = payload["building_id"];
        std::string building_name;
        int level;
        std::string pond_type;
        int fiefdom_id = ctx.requesting_fiefdom_id;

        auto& db = Database::getInstance().gameDB();
        db << "SELECT name, level, pond_type FROM fiefdom_buildings WHERE id = ?;"
           << building_id
           >> [&](std::string name, int lvl, std::string pt) {
               building_name = name;
               level = lvl;
               pond_type = pt;
           };

        try {
            Validation::TransactionGuard tx(Database::getInstance().gameDB());

            auto cumulative = Validation::calculateCumulativeCost(*ctx.config_cache, building_name, pond_type, level);
            nlohmann::json refund;

            for (auto& [key, value] : cumulative.items()) {
                int refund_amount = static_cast<int>(value.get<int>() * 0.8);
                refund[key] = refund_amount;
            }

            Validation::refundResources(fiefdom_id, refund, result);
            if (!Validation::deleteBuilding(building_id)) {
                result.status = ActionStatus::FAIL;
                result.error_code = "database_error";
                result.error_message = "Failed to delete building";
                return result;
            }

            result.result["building_id"] = building_id;
            result.result["refund"] = refund;
            result.action_timestamp = Validation::getCurrentTimestamp();

            tx.commit();
            result.status = ActionStatus::OK;
            return result;
        } catch (const std::exception& e) {
            result.status = ActionStatus::FAIL;
            result.error_code = "database_error";
            result.error_message = std::string(e.what());
            return result;
        }
    }

    std::string getDescription() const {
        return "Demolish a building (80% refund of cumulative costs)";
    }
};

class MoveBuildingActionHandler : public ActionHandler {
public:
    ActionResult validate(const json& payload, const ActionContext& ctx) {
        ActionResult result;

        if (!payload.contains("building_id")) {
            result.status = ActionStatus::FAIL;
            result.error_code = "building_id_required";
            result.error_message = "building_id is required";
            return result;
        }

        if (!payload.contains("x") || !payload.contains("y")) {
            result.status = ActionStatus::FAIL;
            result.error_code = "coordinates_required";
            result.error_message = "x and y coordinates are required";
            return result;
        }

        int building_id = payload["building_id"];
        int x = payload["x"];
        int y = payload["y"];

        if (!Validation::userOwnsBuilding(building_id, ctx)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "not_owner";
            result.error_message = "User does not own this building";
            return result;
        }

        std::string building_name;
        int level;
        auto& db = Database::getInstance().gameDB();
        db << "SELECT name, level FROM fiefdom_buildings WHERE id = ?;"
           << building_id
           >> [&](std::string name, int lvl) {
               building_name = name;
               level = lvl;
           };

        if (building_name == "home_base") {
            result.status = ActionStatus::FAIL;
            result.error_code = "home_base_immutable";
            result.error_message = "Manor House (home_base) cannot be moved";
            return result;
        }

        if (level <= 0) {
            result.status = ActionStatus::FAIL;
            result.error_code = "cannot_move_under_construction";
            result.error_message = "Cannot move building under construction";
            return result;
        }

        auto placement = GridCollision::checkPlacement(*ctx.config_cache, ctx.requesting_fiefdom_id, building_name, x, y, false, building_id);
        if (!placement.valid) {
            result.status = ActionStatus::FAIL;
            result.error_code = "move_location_invalid";
            result.error_message = placement.error_message;
            return result;
        }

        result.status = ActionStatus::OK;
        return result;
    }

    ActionResult execute(const json& payload, const ActionContext& ctx) {
        ActionResult result;

        auto validate_result = validate(payload, ctx);
        if (validate_result.status != ActionStatus::OK) return validate_result;

        int building_id = payload["building_id"];
        int x = payload["x"];
        int y = payload["y"];
        std::string building_name;
        int level;

        auto& db = Database::getInstance().gameDB();
        db << "SELECT name, level FROM fiefdom_buildings WHERE id = ?;"
           << building_id
           >> [&](std::string name, int lvl) {
               building_name = name;
               level = lvl;
           };

        try {
            Validation::TransactionGuard tx(Database::getInstance().gameDB());

            nlohmann::json cost;
            std::string cost_fields[] = {"gold_cost", "silver_pence_cost", "wood_cost", "stone_cost", "steel_cost", "bronze_cost", "grain_cost", "leather_cost", "mana_cost",
                                         "beams_cost", "boards_cost"};
            std::string resource_fields[] = {"gold", "silver_pence", "wood", "stone", "steel", "bronze", "grain", "leather", "mana",
                                             "beams", "boards"};

            auto config_opt = Validation::getBuildingConfig(*ctx.config_cache, building_name);
            if (config_opt) {
                auto config = *config_opt;
                for (size_t i = 0; i < 11; i++) {
                    std::string field = cost_fields[i];
                    if (config.contains(field) && config[field].is_array()) {
                        auto costs = config[field];
                        int level_index = level - 1;
                        if (level_index >= 0 && level_index < costs.size()) {
                            double full_cost = costs[level_index].get<double>();
                            cost[resource_fields[i]] = full_cost / 10;
                        }
                    }
                }
            }

            auto deduct_result = Validation::deductResources(*ctx.config_cache, ctx.requesting_fiefdom_id, cost, result);
            if (deduct_result.status != ActionStatus::OK) return deduct_result;

            if (!Validation::updateBuildingPosition(building_id, x, y)) {
                result.status = ActionStatus::FAIL;
                result.error_code = "database_error";
                result.error_message = "Failed to move building";
                return result;
            }

            result.result["building_id"] = building_id;
            result.result["new_x"] = x;
            result.result["new_y"] = y;
            result.result["cost"] = cost;
            result.action_timestamp = Validation::getCurrentTimestamp();

            tx.commit();
            result.status = ActionStatus::OK;
            return result;
        } catch (const std::exception& e) {
            result.status = ActionStatus::FAIL;
            result.error_code = "database_error";
            result.error_message = std::string(e.what());
            return result;
        }
    }

    std::string getDescription() const {
        return "Move a building (10% of current level cost)";
    }
};

// === TrainTroopsActionHandler (Stub) ===

ActionResult TrainTroopsActionHandler::validate(const json& payload, const ActionContext& ctx) {
    ActionResult result;
    
    if (!payload.contains("fiefdom_id") || !payload.contains("combatant_type")) {
        result.status = ActionStatus::FAIL;
        result.error_code = "missing_fields";
        result.error_message = "fiefdom_id and combatant_type are required";
        return result;
    }
    
    if (!Validation::userOwnsFiefdom(ctx, payload["fiefdom_id"])) {
        result.status = ActionStatus::FAIL;
        result.error_code = "not_owner";
        result.error_message = "User does not own this fiefdom";
        return result;
    }
    
    result.status = ActionStatus::OK;
    result.error_message = "OK";
    return result;
}

ActionResult TrainTroopsActionHandler::execute(const json& payload, const ActionContext& ctx) {
    ActionResult result;
    result.status = ActionStatus::FAIL;
    result.error_code = "not_implemented";
    result.error_message = "Training troops not yet implemented";
    return result;
}

// === ResearchMagicActionHandler (Stub) ===

ActionResult ResearchMagicActionHandler::validate(const json& payload, const ActionContext& ctx) {
    ActionResult result;
    result.status = ActionStatus::FAIL;
    result.error_code = "not_implemented";
    result.error_message = "Magic research not yet implemented";
    return result;
}

ActionResult ResearchMagicActionHandler::execute(const json& payload, const ActionContext& ctx) {
    ActionResult result;
    result.status = ActionStatus::FAIL;
    result.error_code = "not_implemented";
    result.error_message = "Magic research not yet implemented";
    return result;
}

// === ResearchTechActionHandler (Stub) ===

ActionResult ResearchTechActionHandler::validate(const json& payload, const ActionContext& ctx) {
    ActionResult result;
    result.status = ActionStatus::FAIL;
    result.error_code = "not_implemented";
    result.error_message = "Technology research not yet implemented";
    return result;
}

ActionResult ResearchTechActionHandler::execute(const json& payload, const ActionContext& ctx) {
    ActionResult result;
    result.status = ActionStatus::FAIL;
    result.error_code = "not_implemented";
    result.error_message = "Technology research not yet implemented";
    return result;
}

// === Validation Helpers ===

namespace Validation {

bool userOwnsFiefdom(const ActionContext& ctx, int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    int count = 0;
    db << "SELECT COUNT(*) FROM fiefdoms WHERE id = ? AND owner_id = ?;"
       << fiefdom_id << ctx.requesting_character_id
       >> [&](int c) { count = c; };
    return count > 0;
}

bool fiefdomExists(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    int count = 0;
    db << "SELECT COUNT(*) FROM fiefdoms WHERE id = ?;" << fiefdom_id
       >> [&](int c) { count = c; };
    return count > 0;
}

bool hasEnoughResources(GameConfigCache& cache, int fiefdom_id, const json& costs) {
    auto& db = Database::getInstance().gameDB();
    double gold = 0;
    double silver_pence = 0;
    int wood = 0, stone = 0, steel = 0, bronze = 0, grain = 0, leather = 0, mana = 0;
    int charcoal = 0, iron = 0, ironwork = 0, fancy_ironwork = 0, beams = 0, boards = 0;
    std::string import_settings_str;
    db << "SELECT gold, silver_pence, wood, stone, steel, bronze, grain, leather, mana, charcoal, iron, ironwork, fancy_ironwork, beams, boards, import_settings FROM fiefdoms WHERE id = ?;"
       << fiefdom_id
       >> [&](double g, double sp, int w, int st, int stl, int b, int gr, int l, int m,
              int ch, int ir, int iw, int fiw, int bm, int bd, std::string imp) {
           gold = g; silver_pence = sp; wood = w; stone = st; steel = stl;
           bronze = b; grain = gr; leather = l; mana = m;
           charcoal = ch; iron = ir; ironwork = iw; fancy_ironwork = fiw;
           beams = bm; boards = bd; import_settings_str = imp;
       };

    json import_settings = json::object();
    try { import_settings = json::parse(import_settings_str); } catch (...) { import_settings = json::object(); }

    auto economy_cfg = cache.getEconomyConfig();
    json import_prices = economy_cfg.value("import_prices", json::object());
    auto currency_cfg = economy_cfg.value("currency", json::object());
    double pence_per_shilling = currency_cfg.value("pence_per_shilling", 12.0);
    double shillings_per_pound = currency_cfg.value("shillings_per_pound", 20.0);
    double pence_per_gold = currency_cfg.value("pence_per_gold", pence_per_shilling * shillings_per_pound);

    auto money_price_to_pence = [&](const json& price) -> double {
        if (!price.is_object()) return 0.0;
        return price.value("gold", 0.0) * pence_per_gold
             + price.value("shillings", 0.0) * pence_per_shilling
             + price.value("pence", 0.0);
    };

    std::map<std::string, double> stock = {
        {"wood", (double)wood}, {"stone", (double)stone}, {"steel", (double)steel},
        {"bronze", (double)bronze}, {"grain", (double)grain}, {"leather", (double)leather},
        {"mana", (double)mana}, {"charcoal", (double)charcoal}, {"iron", (double)iron},
        {"ironwork", (double)ironwork}, {"fancy_ironwork", (double)fancy_ironwork},
        {"beams", (double)beams}, {"boards", (double)boards},
    };

    auto auto_import = [&](const std::string& res) -> bool {
        if (import_settings.is_object() && import_settings.contains(res)) {
            return import_settings[res].get<bool>();
        }
        return true;
    };

    double gold_demand = 0.0;
    double silver_demand = 0.0;

    for (auto& [res, cost] : costs.items()) {
        double amount = cost.get<double>();
        if (amount <= 0.0) continue;
        if (res == "gold") { gold_demand += amount; continue; }
        if (res == "silver_pence") { silver_demand += amount; continue; }
        auto it = stock.find(res);
        if (it == stock.end()) return false;
        double shortfall = amount - it->second;
        if (shortfall <= 0.0) continue;
        // Cover the shortfall with money (auto-import) if enabled and priced.
        if (auto_import(res) && import_prices.contains(res)) {
            const json& price = import_prices[res];
            if (price.is_object()) {
                silver_demand += shortfall * money_price_to_pence(price);
            } else if (price.is_number()) {
                gold_demand += shortfall * price.get<double>();
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    // Money is fungible: gold and silver_pence are one wallet at pence_per_gold.
    double total_gold_demand = gold_demand + silver_demand / pence_per_gold;
    double total_gold_wealth = gold + silver_pence / pence_per_gold;
    return total_gold_demand <= total_gold_wealth + 1e-9;
}

ActionResult deductResources(GameConfigCache& cache, int fiefdom_id, const json& costs, ActionResult& result) {
    ActionResult r;
    r.status = ActionStatus::OK;
    
    if (costs.empty()) return r;
    
    auto& db = Database::getInstance().gameDB();
    
    double gold = 0;
    double silver_pence = 0;
    int wood = 0, stone = 0, steel = 0, bronze = 0, grain = 0, leather = 0, mana = 0;
    int charcoal = 0, iron = 0, ironwork = 0, fancy_ironwork = 0, beams = 0, boards = 0;
    std::string import_settings_str;
    db << "SELECT gold, silver_pence, wood, stone, steel, bronze, grain, leather, mana, charcoal, iron, ironwork, fancy_ironwork, beams, boards, import_settings FROM fiefdoms WHERE id = ?;"
       << fiefdom_id
       >> [&](double g, double sp, int w, int st, int stl, int b, int gr, int l, int m,
              int ch, int ir, int iw, int fiw, int bm, int bd, std::string imp) {
           gold = g; silver_pence = sp; wood = w; stone = st; steel = stl;
           bronze = b; grain = gr; leather = l; mana = m;
           charcoal = ch; iron = ir; ironwork = iw; fancy_ironwork = fiw;
           beams = bm; boards = bd; import_settings_str = imp;
       };

    json import_settings = json::object();
    try { import_settings = json::parse(import_settings_str); } catch (...) { import_settings = json::object(); }

    auto economy_cfg = cache.getEconomyConfig();
    json import_prices = economy_cfg.value("import_prices", json::object());
    auto currency_cfg = economy_cfg.value("currency", json::object());
    double pence_per_shilling = currency_cfg.value("pence_per_shilling", 12.0);
    double shillings_per_pound = currency_cfg.value("shillings_per_pound", 20.0);
    double pence_per_gold = currency_cfg.value("pence_per_gold", pence_per_shilling * shillings_per_pound);

    auto money_price_to_pence = [&](const json& price) -> double {
        if (!price.is_object()) return 0.0;
        return price.value("gold", 0.0) * pence_per_gold
             + price.value("shillings", 0.0) * pence_per_shilling
             + price.value("pence", 0.0);
    };

    // Fungible money spending: gold and silver_pence are one wallet at
    // pence_per_gold; on shortfall we convert across currencies.
    auto spend_gold = [&](double gold_cost) {
        if (gold_cost <= 0.0) return;
        if (gold >= gold_cost) { gold -= gold_cost; return; }
        double shortfall = gold_cost - gold;
        gold = 0.0;
        silver_pence -= shortfall * pence_per_gold;
    };
    auto spend_silver = [&](double pence_cost) {
        if (pence_cost <= 0.0) return;
        if (silver_pence >= pence_cost) { silver_pence -= pence_cost; return; }
        double shortfall = pence_cost - silver_pence;
        silver_pence = 0.0;
        gold -= shortfall / pence_per_gold;
    };

    std::map<std::string, double> stock = {
        {"wood", (double)wood}, {"stone", (double)stone}, {"steel", (double)steel},
        {"bronze", (double)bronze}, {"grain", (double)grain}, {"leather", (double)leather},
        {"mana", (double)mana}, {"charcoal", (double)charcoal}, {"iron", (double)iron},
        {"ironwork", (double)ironwork}, {"fancy_ironwork", (double)fancy_ironwork},
        {"beams", (double)beams}, {"boards", (double)boards},
    };

    for (auto& [res, cost] : costs.items()) {
        double amount = cost.get<double>();
        if (amount <= 0.0) continue;
        if (res == "gold") { spend_gold(amount); continue; }
        if (res == "silver_pence") { spend_silver(amount); continue; }
        auto it = stock.find(res);
        if (it == stock.end()) continue;
        double from_stock = std::min(it->second, amount);
        it->second -= from_stock;
        double shortfall = amount - from_stock;
        if (shortfall <= 0.0) continue;
        // Import the shortfall with money (fungible). hasEnoughResources already
        // verified affordability; here we just spend it.
        const json& price = import_prices.value(res, json());
        if (price.is_object()) {
            spend_silver(shortfall * money_price_to_pence(price));
        } else if (price.is_number()) {
            spend_gold(shortfall * price.get<double>());
        }
    }

    wood = (int)stock["wood"]; stone = (int)stock["stone"]; steel = (int)stock["steel"];
    bronze = (int)stock["bronze"]; grain = (int)stock["grain"]; leather = (int)stock["leather"];
    mana = (int)stock["mana"]; charcoal = (int)stock["charcoal"]; iron = (int)stock["iron"];
    ironwork = (int)stock["ironwork"]; fancy_ironwork = (int)stock["fancy_ironwork"];
    beams = (int)stock["beams"]; boards = (int)stock["boards"];

    std::string resource_fields[] = {"gold", "silver_pence", "wood", "stone", "steel", "bronze", "grain", "leather", "mana",
                                     "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"};
    double* gold_ptr = &gold;
    double* silver_ptr = &silver_pence;
    int* resource_ptrs[] = {&wood, &stone, &steel, &bronze, &grain, &leather, &mana,
                            &charcoal, &iron, &ironwork, &fancy_ironwork, &beams, &boards};

    for (size_t i = 0; i < 15; i++) {
        if (costs.contains(resource_fields[i])) {
            double amount = costs[resource_fields[i]].get<double>();
            double before;
            double after;
            if (i == 0) {
                before = *gold_ptr + amount;
                after = *gold_ptr;
            } else if (i == 1) {
                before = *silver_ptr + amount;
                after = *silver_ptr;
            } else {
                before = *resource_ptrs[i - 2] + static_cast<int>(amount);
                after = *resource_ptrs[i - 2];
            }
            DiffValue diff;
            diff.field = resource_fields[i];
            diff.source_type = "fiefdom";
            diff.source_id = fiefdom_id;
            diff.entity_key = "fiefdom_id";
            diff.from_value = before;
            diff.to_value = after;
            result.side_effects.push_back(diff);
        }
    }
    
    db << "UPDATE fiefdoms SET gold = ?, silver_pence = ?, wood = ?, stone = ?, steel = ?, bronze = ?, grain = ?, leather = ?, mana = ?, charcoal = ?, iron = ?, ironwork = ?, fancy_ironwork = ?, beams = ?, boards = ? WHERE id = ?;"
       << gold << silver_pence << wood << stone << steel << bronze << grain << leather << mana
       << charcoal << iron << ironwork << fancy_ironwork << beams << boards << fiefdom_id;
    
    return r;
}

bool buildingTypeExists(GameConfigCache& cache, const std::string& building_type) {
    auto types = cache.getFiefdomBuildingTypes();
    for (const auto& type_obj : types) {
        if (type_obj.contains(building_type)) return true;
    }
    return false;
}

std::optional<json> getBuildingConfig(GameConfigCache& cache, const std::string& building_type) {
    auto types = cache.getFiefdomBuildingTypes();
    for (const auto& type_obj : types) {
        if (type_obj.contains(building_type)) return type_obj[building_type];
    }
    return std::nullopt;
}

// Returns the array field (cost/construction arrays) for a building. Mill ponds
// resolve arrays from their current pond type (`pond_types[pond_type]`); all
// other buildings use the top-level config.
nlohmann::json getBuildingArrayField(GameConfigCache& cache, const std::string& building_name,
                                     const std::string& pond_type, const std::string& field) {
    auto config_opt = getBuildingConfig(cache, building_name);
    if (!config_opt) return nlohmann::json::array();
    auto config = *config_opt;
    const nlohmann::json* src = &config;
    if (building_name == "mill_pond" && config.contains("pond_types") && config["pond_types"].is_array()) {
        const nlohmann::json* found = nullptr;
        for (const auto& pt : config["pond_types"]) {
            if (pt.is_object() && pt.value("id", "") == pond_type) { found = &pt; break; }
        }
        if (found) src = found;
    }
    if (src->contains(field) && (*src)[field].is_array()) return (*src)[field];
    return nlohmann::json::array();
}

// Returns the next level's cost for a building, keyed by resource name (gold,
// wood, stone, ...). Pond buildings resolve costs from their pond type.
nlohmann::json getNextLevelCost(GameConfigCache& cache, const std::string& building_name,
                                const std::string& pond_type, int current_level) {
    nlohmann::json next_cost;
    std::string cost_fields[] = {"gold_cost", "silver_pence_cost", "wood_cost", "stone_cost", "steel_cost", "bronze_cost", "grain_cost", "leather_cost", "mana_cost",
                                 "charcoal_cost", "iron_cost", "ironwork_cost", "fancy_ironwork_cost", "beams_cost", "boards_cost"};
    std::string resource_fields[] = {"gold", "silver_pence", "wood", "stone", "steel", "bronze", "grain", "leather", "mana",
                                     "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"};
    for (size_t i = 0; i < 15; i++) {
        auto costs = getBuildingArrayField(cache, building_name, pond_type, cost_fields[i]);
        if (costs.is_array() && current_level > 0 && current_level < static_cast<int>(costs.size())) {
            double amount = costs[current_level].get<double>();
            if (amount > 0) next_cost[resource_fields[i]] = amount;
        }
    }
    return next_cost;
}

// Returns the max level for a building. Mill ponds use their current pond type's
// max_level (falling back to the first type when the stored type is blank).
int getBuildingMaxLevel(GameConfigCache& cache, const std::string& building_name,
                        const std::string& pond_type) {
    auto config_opt = getBuildingConfig(cache, building_name);
    if (!config_opt) return 1;
    auto config = *config_opt;
    if (building_name == "mill_pond" && config.contains("pond_types") && config["pond_types"].is_array()) {
        for (const auto& pt : config["pond_types"]) {
            if (pt.is_object() && pt.value("id", "") == pond_type) {
                return pt.value("max_level", config.value("max_level", 1));
            }
        }
        if (!config["pond_types"].empty() && config["pond_types"][0].is_object()) {
            return config["pond_types"][0].value("max_level", config.value("max_level", 1));
        }
    }
    return config.value("max_level", 1);
}

bool canBuildBuildingHere(GameConfigCache& cache, const std::string& building_type, int fiefdom_id, int x, int y) {
    bool isHomeBase = (building_type == "home_base");
    auto result = GridCollision::checkPlacement(cache, fiefdom_id, building_type, x, y, isHomeBase);
    return result.valid;
}

bool hasCompletedHomeBase(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    int count = 0;
    db << "SELECT COUNT(*) FROM fiefdom_buildings WHERE fiefdom_id = ? AND name = 'home_base' AND level > 0;"
       << fiefdom_id
       >> [&](int c) { count = c; };
    return count > 0;
}

int64_t getCurrentTimestamp() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

std::optional<json> getWallConfig(GameConfigCache& cache) {
    if (!cache.isLoaded()) return std::nullopt;
    auto config = cache.getAllConfigs();
    if (config.contains("wall_config")) return config["wall_config"];
    return std::nullopt;
}

bool validWallPlacement(GameConfigCache& cache, int fiefdom_id, const json& payload) {
    auto wall_config_opt = getWallConfig(cache);
    if (!wall_config_opt) return false;
    auto wall_config = *wall_config_opt;
    
    auto& db = Database::getInstance().gameDB();
    int wall_count = 0;
    db << "SELECT wall_count FROM fiefdoms WHERE id = ?;" << fiefdom_id
       >> [&](int c) { wall_count = c; };
    
    int max_walls = wall_config.value("max_wall_count", 100);
    if (wall_count >= max_walls) return false;
    
    return true;
}

TransactionGuard::TransactionGuard(sqlite::database& db) : db_(db) {
    db << "BEGIN TRANSACTION;";
}

TransactionGuard::~TransactionGuard() {
    if (!committed_) {
        db_ << "ROLLBACK;";
    }
}

void TransactionGuard::commit() {
    db_ << "COMMIT;";
    committed_ = true;
}

nlohmann::json calculateCumulativeCost(GameConfigCache& cache, const std::string& building_type, int current_level) {
    return calculateCumulativeCost(cache, building_type, "", current_level);
}

nlohmann::json calculateCumulativeCost(GameConfigCache& cache, const std::string& building_type, const std::string& pond_type, int current_level) {
    auto config_opt = getBuildingConfig(cache, building_type);
    if (!config_opt) return nlohmann::json::object();

    auto config = *config_opt;
    nlohmann::json cumulative;
            std::string cost_fields[] = {"gold_cost", "silver_pence_cost", "wood_cost", "stone_cost", "steel_cost", "bronze_cost", "grain_cost", "leather_cost", "mana_cost",
                                         "charcoal_cost", "iron_cost", "ironwork_cost", "fancy_ironwork_cost", "beams_cost", "boards_cost"};
            std::string resource_fields[] = {"gold", "silver_pence", "wood", "stone", "steel", "bronze", "grain", "leather", "mana",
                                             "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"};
                for (size_t i = 0; i < 15; i++) {
        auto costs = getBuildingArrayField(cache, building_type, pond_type, cost_fields[i]);
        if (costs.is_array()) {
            double total = 0;
            for (int j = 0; j < current_level && j < static_cast<int>(costs.size()); j++) {
                total += costs[j].get<double>();
            }
            if (total > 0) cumulative[resource_fields[i]] = total;
        }
    }

    return cumulative;
}

bool userOwnsBuilding(int building_id, const ActionContext& ctx) {
    auto& db = Database::getInstance().gameDB();
    int fiefdom_id = 0;

    db << "SELECT fiefdom_id FROM fiefdom_buildings WHERE id = ?;"
       << building_id
       >> [&](int fid) { fiefdom_id = fid; };

    if (fiefdom_id == 0) return false;

    return userOwnsFiefdom(ctx, fiefdom_id);
}

ActionResult refundResources(int fiefdom_id, const nlohmann::json& amounts, ActionResult& result) {
    auto& db = Database::getInstance().gameDB();

    double gold = 0;
    double silver_pence = 0;
    int wood = 0, stone = 0, steel = 0, bronze = 0, grain = 0, leather = 0, mana = 0;
    int charcoal = 0, iron = 0, ironwork = 0, fancy_ironwork = 0, beams = 0, boards = 0;
    db << "SELECT gold, silver_pence, wood, stone, steel, bronze, grain, leather, mana, charcoal, iron, ironwork, fancy_ironwork, beams, boards FROM fiefdoms WHERE id = ?;"
       << fiefdom_id
       >> [&](double g, double sp, int w, int st, int stl, int b, int gr, int l, int m,
              int ch, int ir, int iw, int fiw, int bm, int bd) {
           gold = g; silver_pence = sp; wood = w; stone = st; steel = stl;
           bronze = b; grain = gr; leather = l; mana = m;
           charcoal = ch; iron = ir; ironwork = iw; fancy_ironwork = fiw;
           beams = bm; boards = bd;
       };

    std::string resource_fields[] = {"gold", "silver_pence", "wood", "stone", "steel", "bronze", "grain", "leather", "mana",
                                     "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"};
    double* gold_ptr = &gold;
    double* silver_ptr = &silver_pence;
    int* resource_ptrs[] = {&wood, &stone, &steel, &bronze, &grain, &leather, &mana,
                            &charcoal, &iron, &ironwork, &fancy_ironwork, &beams, &boards};

    for (size_t i = 0; i < 15; i++) {
        if (amounts.contains(resource_fields[i])) {
            double refund = amounts[resource_fields[i]].get<double>();
            double before;
            double after;
            if (i == 0) {
                before = *gold_ptr;
                *gold_ptr += refund;
                after = *gold_ptr;
            } else if (i == 1) {
                before = *silver_ptr;
                *silver_ptr += refund;
                after = *silver_ptr;
            } else {
                before = *resource_ptrs[i - 2];
                *resource_ptrs[i - 2] += static_cast<int>(refund);
                after = *resource_ptrs[i - 2];
            }

            DiffValue diff;
            diff.field = resource_fields[i];
            diff.source_type = "fiefdom";
            diff.source_id = fiefdom_id;
            diff.entity_key = "fiefdom_id";
            diff.from_value = before;
            diff.to_value = after;
            result.side_effects.push_back(diff);
        }
    }

    db << "UPDATE fiefdoms SET gold = ?, silver_pence = ?, wood = ?, stone = ?, steel = ?, bronze = ?, grain = ?, leather = ?, mana = ?, charcoal = ?, iron = ?, ironwork = ?, fancy_ironwork = ?, beams = ?, boards = ? WHERE id = ?;"
       << gold << silver_pence << wood << stone << steel << bronze << grain << leather << mana
       << charcoal << iron << ironwork << fancy_ironwork << beams << boards << fiefdom_id;

    return result;
}

bool deleteBuilding(int building_id) {
    auto& db = Database::getInstance().gameDB();
    try {
        db << "DELETE FROM fiefdom_buildings WHERE id = ?;" << building_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to delete building: " << e.what() << std::endl;
        return false;
    }
}

bool updateBuildingPosition(int building_id, int x, int y) {
    auto& db = Database::getInstance().gameDB();
    try {
        db << "UPDATE fiefdom_buildings SET x = ?, y = ? WHERE id = ?;" << x << y << building_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to update building position: " << e.what() << std::endl;
        return false;
    }
}

std::optional<json> getWallConfigByGeneration(GameConfigCache& cache, int generation) {
    auto config_opt = getWallConfig(cache);
    if (!config_opt) return std::nullopt;
    auto config = *config_opt;
    std::string gen_key = std::to_string(generation);
    if (config.contains("walls") && config["walls"].is_object()) {
        auto walls = config["walls"];
        if (walls.contains(gen_key)) return walls[gen_key];
    }
    return std::nullopt;
}

bool wallGenerationExists(int fiefdom_id, int generation) {
    auto& db = Database::getInstance().gameDB();
    int count = 0;
    db << "SELECT COUNT(*) FROM fiefdom_walls WHERE fiefdom_id = ? AND generation = ?;"
       << fiefdom_id << generation
       >> [&](int c) { count = c; };
    return count > 0;
}

bool hasWallGeneration(int fiefdom_id, int generation) {
    return wallGenerationExists(fiefdom_id, generation);
}

bool canAffordWall(GameConfigCache& cache, int fiefdom_id, int generation, int level) {
    auto config_opt = getWallConfigByGeneration(cache, generation);
    if (!config_opt) return false;

    std::string cost_fields[] = {"gold_cost", "stone_cost"};
    std::string resource_fields[] = {"gold", "stone"};

    auto& db = Database::getInstance().gameDB();
    double gold = 0;
    int stone = 0;
    db << "SELECT gold, stone FROM fiefdoms WHERE id = ?;"
       << fiefdom_id
       >> [&](double g, int s) { gold = g; stone = s; };

    double* gold_ptr = &gold;
    int* stone_ptr = &stone;
    double* resource_ptrs[] = {gold_ptr, nullptr};

    for (size_t i = 0; i < 2; i++) {
        if (config_opt->contains(cost_fields[i])) {
            auto costs = (*config_opt)[cost_fields[i]];
            if (costs.is_array() && level > 0 && level <= static_cast<int>(costs.size())) {
                double cost = costs[level - 1].get<double>();
                if (i == 0 && *gold_ptr < cost) return false;
                if (i == 1 && *stone_ptr < cost) return false;
            }
        }
    }
    return true;
}

int getWallHP(GameConfigCache& cache, int generation, int level) {
    auto config_opt = getWallConfigByGeneration(cache, generation);
    if (!config_opt) return 0;

    if (config_opt->contains("hp")) {
        auto hp_array = (*config_opt)["hp"];
        if (hp_array.is_array() && level > 0 && level <= static_cast<int>(hp_array.size())) {
            return hp_array[level - 1].get<int>();
        }
    }
    return 0;
}

double getWallMoraleBoost(GameConfigCache& cache, int generation, int level) {
    auto config_opt = getWallConfigByGeneration(cache, generation);
    if (!config_opt) return 0.0;

    if (config_opt->contains("morale_boost")) {
        auto morale_array = (*config_opt)["morale_boost"];
        if (morale_array.is_array() && level > 0 && level <= static_cast<int>(morale_array.size())) {
            return morale_array[level - 1].get<double>();
        }
    }
    return 0.0;
}

nlohmann::json calculateWallUpgradeCost(GameConfigCache& cache, int generation, int current_level) {
    auto config_opt = getWallConfigByGeneration(cache, generation);
    nlohmann::json cost;

    if (!config_opt) return cost;

    std::string cost_fields[] = {"gold_cost", "stone_cost"};
    std::string resource_fields[] = {"gold", "stone"};

    for (size_t i = 0; i < 2; i++) {
        if (config_opt->contains(cost_fields[i])) {
            auto costs = (*config_opt)[cost_fields[i]];
            if (costs.is_array() && current_level + 1 > 0 && (current_level + 1) <= static_cast<int>(costs.size())) {
                cost[resource_fields[i]] = costs[current_level].get<double>();
            }
        }
    }
    return cost;
}

nlohmann::json getDemolishRefund(GameConfigCache& cache, int building_id) {
    auto& db = Database::getInstance().gameDB();
    std::string building_name;
    int level;
    std::string pond_type;
    db << "SELECT name, level, pond_type FROM fiefdom_buildings WHERE id = ?;"
       << building_id
       >> [&](std::string name, int lvl, std::string pt) {
           building_name = name;
           level = lvl;
           pond_type = pt;
       };

    if (building_name.empty()) return nlohmann::json::object();

    auto cumulative = calculateCumulativeCost(cache, building_name, pond_type, level);
    nlohmann::json refund;

    for (auto& [key, value] : cumulative.items()) {
        int refund_amount = static_cast<int>(value.get<int>() * 0.8);
        refund[key] = refund_amount;
    }

    return refund;
}

} // namespace Validation

// === BuildWallActionHandler Implementation ===

ActionResult BuildWallActionHandler::validate(const json& payload, const ActionContext& ctx) {
    ActionResult result;

    if (!payload.contains("fiefdom_id")) {
        result.status = ActionStatus::FAIL;
        result.error_code = "fiefdom_id_required";
        result.error_message = "fiefdom_id is required";
        return result;
    }

    if (!payload.contains("wall_generation")) {
        result.status = ActionStatus::FAIL;
        result.error_code = "wall_generation_required";
        result.error_message = "wall_generation is required";
        return result;
    }

    int fiefdom_id = payload["fiefdom_id"];
    int wall_generation = payload["wall_generation"];

    if (!Validation::userOwnsFiefdom(ctx, fiefdom_id)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "not_owner";
        result.error_message = "User does not own this fiefdom";
        return result;
    }

    auto config_opt = Validation::getWallConfigByGeneration(*ctx.config_cache, wall_generation);
    if (!config_opt) {
        result.status = ActionStatus::FAIL;
        result.error_code = "generation_invalid";
        result.error_message = "Invalid wall generation: " + std::to_string(wall_generation);
        return result;
    }

    if (wall_generation > 1 && !Validation::hasWallGeneration(fiefdom_id, wall_generation - 1)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "generation_sequence_required";
        result.error_message = "Must build wall generation " + std::to_string(wall_generation - 1) + " first";
        return result;
    }

    if (Validation::hasWallGeneration(fiefdom_id, wall_generation)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "generation_exists";
        result.error_message = "Wall generation " + std::to_string(wall_generation) + " already exists";
        return result;
    }

    if (!Validation::canAffordWall(*ctx.config_cache, fiefdom_id, wall_generation, 1)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "insufficient_resources";
        result.error_message = "Not enough resources to build wall";
        return result;
    }

    result.status = ActionStatus::OK;
    return result;
}

ActionResult BuildWallActionHandler::execute(const json& payload, const ActionContext& ctx) {
    ActionResult result;

    auto validate_result = validate(payload, ctx);
    if (validate_result.status != ActionStatus::OK) return validate_result;

    int fiefdom_id = payload["fiefdom_id"];
    int wall_generation = payload["wall_generation"];
    int64_t now = Validation::getCurrentTimestamp();

    auto config_opt = Validation::getWallConfigByGeneration(*ctx.config_cache, wall_generation);
    auto config = *config_opt;

    try {
        Validation::TransactionGuard tx(Database::getInstance().gameDB());
        auto& db = Database::getInstance().gameDB();

        nlohmann::json cost;
        if (config.contains("gold_cost") && config["gold_cost"].is_array() && config["gold_cost"].size() > 0) {
            cost["gold"] = config["gold_cost"][0].get<int>();
        }
        if (config.contains("stone_cost") && config["stone_cost"].is_array() && config["stone_cost"].size() > 0) {
            cost["stone"] = config["stone_cost"][0].get<int>();
        }

        auto deduct_result = Validation::deductResources(*ctx.config_cache, fiefdom_id, cost, result);
        if (deduct_result.status != ActionStatus::OK) return deduct_result;

        std::vector<nlohmann::json> overlapping_buildings;
        db << "SELECT id, name, level, x, y FROM fiefdom_buildings WHERE fiefdom_id = ? AND level > 0;"
           << fiefdom_id
           >> [&](int id, std::string name, int level, int bx, int by) {
               nlohmann::json b;
               b["id"] = id;
               b["name"] = name;
               b["level"] = level;
               b["x"] = bx;
               b["y"] = by;
               overlapping_buildings.push_back(b);
           };

        nlohmann::json demolished_buildings;
        for (auto& building : overlapping_buildings) {
            int bx = building["x"].get<int>();
            int by = building["y"].get<int>();
            auto [bw, bh] = GridCollision::getBuildingDimensionsPair(*ctx.config_cache, building["name"]);

            if (GridCollision::overlapsWalls(*ctx.config_cache, fiefdom_id, wall_generation, bx, by, bw, bh)) {
                int building_id = building["id"].get<int>();
                auto refund = Validation::getDemolishRefund(*ctx.config_cache, building_id);

                Validation::refundResources(fiefdom_id, refund, result);

                nlohmann::json demo;
                demo["building_id"] = building_id;
                demo["building_type"] = building["name"];
                demo["refund"] = refund;
                demolished_buildings.push_back(demo);

                if (!Validation::deleteBuilding(building_id)) {
                    result.status = ActionStatus::FAIL;
                    result.error_code = "database_error";
                    result.error_message = "Failed to demolish overlapping building";
                    return result;
                }
            }
        }

        int initial_hp = Validation::getWallHP(*ctx.config_cache, wall_generation, 1);
        if (!FiefdomFetcher::createWall(fiefdom_id, wall_generation, 1, initial_hp, now)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "database_error";
            result.error_message = "Failed to create wall";
            return result;
        }

        int wall_id = 0;
        db << "SELECT id FROM fiefdom_walls WHERE fiefdom_id = ? AND generation = ?;"
           << fiefdom_id << wall_generation
           >> [&](int id) { wall_id = id; };

        result.result["wall_id"] = wall_id;
        result.result["generation"] = wall_generation;
        result.result["level"] = 1;
        result.result["hp"] = initial_hp;
        result.result["width"] = config.value("width", 0);
        result.result["length"] = config.value("length", 0);
        result.result["thickness"] = config.value("thickness", 0);
        result.result["cost"] = cost;
        result.result["demolished_buildings"] = demolished_buildings;
        result.action_timestamp = now;

        tx.commit();
        result.status = ActionStatus::OK;
        return result;
    } catch (const std::exception& e) {
        result.status = ActionStatus::FAIL;
        result.error_code = "database_error";
        result.error_message = std::string(e.what());
        return result;
    }
}

// === UpgradeActionHandler Implementation ===

ActionResult UpgradeActionHandler::validate(const json& payload, const ActionContext& ctx) {
    ActionResult result;

    if (!payload.contains("fiefdom_id")) {
        result.status = ActionStatus::FAIL;
        result.error_code = "fiefdom_id_required";
        result.error_message = "fiefdom_id is required";
        return result;
    }

    bool has_building_id = payload.contains("building_id");
    bool has_wall_id = payload.contains("wall_id");

    if (!has_building_id && !has_wall_id) {
        result.status = ActionStatus::FAIL;
        result.error_code = "upgrade_id_required";
        result.error_message = "Either building_id or wall_id is required";
        return result;
    }

    int fiefdom_id = payload["fiefdom_id"];

    if (!Validation::userOwnsFiefdom(ctx, fiefdom_id)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "not_owner";
        result.error_message = "User does not own this fiefdom";
        return result;
    }

    if (has_building_id) {
        int building_id = payload["building_id"];

        int owning_fiefdom = 0;
        auto& db = Database::getInstance().gameDB();
        db << "SELECT fiefdom_id FROM fiefdom_buildings WHERE id = ?;"
           << building_id
           >> [&](int fid) { owning_fiefdom = fid; };

        if (owning_fiefdom != fiefdom_id) {
            result.status = ActionStatus::FAIL;
            result.error_code = "not_owner";
            result.error_message = "User does not own this building";
            return result;
        }

        std::string building_name;
        int current_level;
        std::string pond_type;
        db << "SELECT name, level, pond_type FROM fiefdom_buildings WHERE id = ?;"
           << building_id
           >> [&](std::string name, int lvl, std::string pt) {
               building_name = name;
               current_level = lvl;
               pond_type = pt;
           };

        if (current_level == 0) {
            result.status = ActionStatus::FAIL;
            result.error_code = "upgrade_in_progress";
            result.error_message = "Building is already under construction";
            return result;
        }

        auto config_opt = Validation::getBuildingConfig(*ctx.config_cache, building_name);
        if (!config_opt) {
            result.status = ActionStatus::FAIL;
            result.error_code = "invalid_config";
            result.error_message = "Building configuration not found";
            return result;
        }

        int max_level = Validation::getBuildingMaxLevel(*ctx.config_cache, building_name, pond_type);
        if (current_level >= max_level) {
            result.status = ActionStatus::FAIL;
            result.error_code = "max_level_reached";
            result.error_message = "Building is at maximum level";
            return result;
        }

        if (config_opt->contains("prerequisites") && (*config_opt)["prerequisites"].is_array()) {
            int next_level = current_level + 1;
            auto prerequisites_opt = Validation::getPrerequisitesForLevel(*ctx.config_cache, building_name, next_level);
            if (prerequisites_opt && !prerequisites_opt->empty()) {
                if (!Validation::checkFiefdomPrerequisites(*ctx.config_cache, fiefdom_id, *prerequisites_opt)) {
                    result.status = ActionStatus::FAIL;
                    result.error_code = "prerequisites_not_met";
                    result.error_message = "Building prerequisites not satisfied for next level";
                    return result;
                }
            }
        }

        // Check building dependencies for the next level
        {
            int next_level = current_level + 1;
            nlohmann::json deps = Validation::getDependenciesForLevel(*ctx.config_cache, building_name, next_level);
            if (!deps.empty()) {
                auto all_buildings = Validation::getFiefdomAllBuildings(fiefdom_id);
                auto dep_result = Validation::checkBuildingDependencies(*ctx.config_cache, fiefdom_id, all_buildings, deps);
                if (!dep_result.first) {
                    result.status = ActionStatus::FAIL;
                    result.error_code = "dependencies_not_met";
                    result.error_message = dep_result.second;
                    return result;
                }
            }
        }

        nlohmann::json next_cost = Validation::getNextLevelCost(*ctx.config_cache, building_name, pond_type, current_level);

        if (!Validation::hasEnoughResources(*ctx.config_cache, fiefdom_id, next_cost)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "insufficient_resources";
            result.error_message = "Not enough resources to upgrade";
            return result;
        }
    }

    if (has_wall_id) {
        int wall_id = payload["wall_id"];

        int owning_fiefdom = 0;
        auto& db = Database::getInstance().gameDB();
        db << "SELECT fiefdom_id FROM fiefdom_walls WHERE id = ?;"
           << wall_id
           >> [&](int fid) { owning_fiefdom = fid; };

        if (owning_fiefdom != fiefdom_id) {
            result.status = ActionStatus::FAIL;
            result.error_code = "not_owner";
            result.error_message = "User does not own this wall";
            return result;
        }

        int generation;
        int current_level;
        db << "SELECT generation, level FROM fiefdom_walls WHERE id = ?;"
           << wall_id
           >> [&](int gen, int lvl) {
               generation = gen;
               current_level = lvl;
           };

        if (current_level == 0) {
            result.status = ActionStatus::FAIL;
            result.error_code = "upgrade_in_progress";
            result.error_message = "Wall is already under construction";
            return result;
        }

        auto config_opt = Validation::getWallConfigByGeneration(*ctx.config_cache, generation);
        if (!config_opt) {
            result.status = ActionStatus::FAIL;
            result.error_code = "invalid_config";
            result.error_message = "Wall configuration not found";
            return result;
        }

        int max_level = 0;
        if (config_opt->contains("hp")) {
            max_level = static_cast<int>((*config_opt)["hp"].size());
        }
        if (current_level >= max_level) {
            result.status = ActionStatus::FAIL;
            result.error_code = "max_level_reached";
            result.error_message = "Wall is at maximum level";
            return result;
        }

        auto cost = Validation::calculateWallUpgradeCost(*ctx.config_cache, generation, current_level);
        if (!Validation::hasEnoughResources(*ctx.config_cache, fiefdom_id, cost)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "insufficient_resources";
            result.error_message = "Not enough resources to upgrade";
            return result;
        }
    }

    result.status = ActionStatus::OK;
    return result;
}

ActionResult UpgradeActionHandler::execute(const json& payload, const ActionContext& ctx) {
    ActionResult result;

    auto validate_result = validate(payload, ctx);
    if (validate_result.status != ActionStatus::OK) return validate_result;

    int fiefdom_id = payload["fiefdom_id"];
    int64_t now = Validation::getCurrentTimestamp();

    try {
        Validation::TransactionGuard tx(Database::getInstance().gameDB());

        if (payload.contains("building_id")) {
            int building_id = payload["building_id"];

            std::string building_name;
            int current_level;
            std::string pond_type;
            auto& db = Database::getInstance().gameDB();
            db << "SELECT name, level, pond_type FROM fiefdom_buildings WHERE id = ?;"
               << building_id
               >> [&](std::string name, int lvl, std::string pt) {
                   building_name = name;
                   current_level = lvl;
                   pond_type = pt;
               };

            auto config_opt = Validation::getBuildingConfig(*ctx.config_cache, building_name);
            if (!config_opt) {
                result.status = ActionStatus::FAIL;
                result.error_code = "invalid_config";
                result.error_message = "Building configuration not found";
                return result;
            }

            nlohmann::json next_cost = Validation::getNextLevelCost(*ctx.config_cache, building_name, pond_type, current_level);

            auto deduct_result = Validation::deductResources(*ctx.config_cache, fiefdom_id, next_cost, result);
            if (deduct_result.status != ActionStatus::OK) return deduct_result;

            if (!FiefdomFetcher::updateBuildingConstructionStart(building_id, now, now)) {
                result.status = ActionStatus::FAIL;
                result.error_code = "database_error";
                result.error_message = "Failed to start upgrade";
                return result;
            }

            result.result["building_id"] = building_id;
            result.result["upgrade_to_level"] = current_level + 1;
            result.result["cost"] = next_cost;
        }

        if (payload.contains("wall_id")) {
            int wall_id = payload["wall_id"];

            int generation;
            int current_level;
            auto& db = Database::getInstance().gameDB();
            db << "SELECT generation, level FROM fiefdom_walls WHERE id = ?;"
               << wall_id
               >> [&](int gen, int lvl) {
                   generation = gen;
                   current_level = lvl;
               };

        auto cost = Validation::calculateWallUpgradeCost(*ctx.config_cache, generation, current_level);
            auto deduct_result = Validation::deductResources(*ctx.config_cache, fiefdom_id, cost, result);
            if (deduct_result.status != ActionStatus::OK) return deduct_result;

            int new_hp = Validation::getWallHP(*ctx.config_cache, generation, current_level + 1);
            if (!FiefdomFetcher::updateWallLevel(wall_id, current_level + 1, new_hp, now)) {
                result.status = ActionStatus::FAIL;
                result.error_code = "database_error";
                result.error_message = "Failed to start upgrade";
                return result;
            }

            result.result["wall_id"] = wall_id;
            result.result["upgrade_to_level"] = current_level + 1;
            result.result["new_hp"] = new_hp;
            result.result["cost"] = cost;
        }

        result.action_timestamp = now;
        tx.commit();
        result.status = ActionStatus::OK;
        return result;
    } catch (const std::exception& e) {
        result.status = ActionStatus::FAIL;
        result.error_code = "database_error";
        result.error_message = std::string(e.what());
        return result;
    }
}

// === UpgradePondTypeActionHandler Implementation ===
// Upgrades a mill pond from one type to the next (earthen -> timber -> stone),
// rebuilding it (level 0 + construction timer) as the new type. Requires the
// pond to be at its current type's max level first.

namespace {
// Shared helper: locates the pond_types array + current index + the next type
// object. Returns false when the request is invalid (returns the failure in
// `result`).
bool resolvePondTypeUpgrade(const json& payload, const ActionContext& ctx,
                            int& fiefdom_id, int& building_id,
                            const json*& next_type, int& next_index, ActionResult& result) {
    if (!payload.contains("building_id")) {
        result.status = ActionStatus::FAIL;
        result.error_code = "building_id_required";
        result.error_message = "building_id is required";
        return false;
    }
    building_id = payload["building_id"];

    if (!Validation::userOwnsBuilding(building_id, ctx)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "not_owner";
        result.error_message = "User does not own this building";
        return false;
    }

    auto& db = Database::getInstance().gameDB();
    std::string building_name;
    int current_level;
    std::string pond_type;
    db << "SELECT fiefdom_id, name, level, pond_type FROM fiefdom_buildings WHERE id = ?;"
       << building_id
       >> [&](int fid, std::string name, int lvl, std::string pt) {
           fiefdom_id = fid;
           building_name = name;
           current_level = lvl;
           pond_type = pt;
       };

    if (building_name != "mill_pond") {
        result.status = ActionStatus::FAIL;
        result.error_code = "not_mill_pond";
        result.error_message = "Only mill ponds can change type";
        return false;
    }
    if (current_level == 0) {
        result.status = ActionStatus::FAIL;
        result.error_code = "upgrade_in_progress";
        result.error_message = "Mill pond is already under construction";
        return false;
    }

    auto config_opt = Validation::getBuildingConfig(*ctx.config_cache, "mill_pond");
    if (!config_opt || !config_opt->contains("pond_types") || !(*config_opt)["pond_types"].is_array()) {
        result.status = ActionStatus::FAIL;
        result.error_code = "invalid_config";
        result.error_message = "Mill pond configuration not found";
        return false;
    }

    auto types = (*config_opt)["pond_types"];
    int current_idx = -1;
    for (size_t i = 0; i < types.size(); i++) {
        if (types[i].is_object() && types[i].value("id", "") == pond_type) { current_idx = static_cast<int>(i); break; }
    }
    if (current_idx < 0) current_idx = 0; // blank pond_type -> earthen
    if (current_idx >= static_cast<int>(types.size()) - 1) {
        result.status = ActionStatus::FAIL;
        result.error_code = "max_pond_type";
        result.error_message = "Mill pond is already at its highest type";
        return false;
    }

    auto cur_type = types[current_idx];
    int type_max_level = cur_type.is_object() ? cur_type.value("max_level", 1) : 1;
    if (current_level < type_max_level) {
        result.status = ActionStatus::FAIL;
        result.error_code = "type_level_required";
        result.error_message = "Mill pond must be at max level of its current type before upgrading its type";
        return false;
    }

    next_index = current_idx + 1;
    next_type = &types[next_index];
    return true;
}

// Resource-keyed level-1 cost for a pond type object.
json pondTypeLevel1Cost(const json& pt) {
    json cost;
    std::string cost_fields[] = {"gold_cost", "silver_pence_cost", "wood_cost", "stone_cost", "steel_cost", "bronze_cost", "grain_cost", "leather_cost", "mana_cost",
                                 "charcoal_cost", "iron_cost", "ironwork_cost", "fancy_ironwork_cost", "beams_cost", "boards_cost"};
    std::string resource_fields[] = {"gold", "silver_pence", "wood", "stone", "steel", "bronze", "grain", "leather", "mana",
                                     "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"};
    for (size_t i = 0; i < 15; i++) {
        if (pt.contains(cost_fields[i]) && pt[cost_fields[i]].is_array() && !pt[cost_fields[i]].empty()) {
            double amount = pt[cost_fields[i]][0].get<double>();
            if (amount > 0) cost[resource_fields[i]] = amount;
        }
    }
    return cost;
}
} // namespace

class UpgradePondTypeActionHandler : public ActionHandler {
public:
    ActionResult validate(const json& payload, const ActionContext& ctx) override {
        ActionResult result;
        int fiefdom_id, building_id, next_index;
        const json* next_type = nullptr;
        if (!resolvePondTypeUpgrade(payload, ctx, fiefdom_id, building_id, next_type, next_index, result)) {
            return result;
        }
        auto cost = pondTypeLevel1Cost(*next_type);
        if (!Validation::hasEnoughResources(*ctx.config_cache, fiefdom_id, cost)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "insufficient_resources";
            result.error_message = "Not enough resources to upgrade the mill pond type";
            return result;
        }
        result.status = ActionStatus::OK;
        return result;
    }

    ActionResult execute(const json& payload, const ActionContext& ctx) override {
        ActionResult result;
        int fiefdom_id, building_id, next_index;
        const json* next_type = nullptr;
        if (!resolvePondTypeUpgrade(payload, ctx, fiefdom_id, building_id, next_type, next_index, result)) {
            return result;
        }

        auto cost = pondTypeLevel1Cost(*next_type);
        if (!Validation::hasEnoughResources(*ctx.config_cache, fiefdom_id, cost)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "insufficient_resources";
            result.error_message = "Not enough resources to upgrade the mill pond type";
            return result;
        }

        int64_t now = Validation::getCurrentTimestamp();
        try {
            Validation::TransactionGuard tx(Database::getInstance().gameDB());

            auto deduct_result = Validation::deductResources(*ctx.config_cache, fiefdom_id, cost, result);
            if (deduct_result.status != ActionStatus::OK) return deduct_result;

            std::string next_id = next_type->value("id", "");
            // Instant next type (construction_times[0] == 0) completes immediately.
            bool instant = false;
            if (next_type->contains("construction_times") && (*next_type)["construction_times"].is_array() &&
                !(*next_type)["construction_times"].empty()) {
                instant = ((*next_type)["construction_times"][0].get<double>() == 0);
            }
            int new_level = instant ? 1 : 0;
            int64_t construction_start = instant ? 0 : now;

            if (!FiefdomFetcher::updateBuildingPondType(building_id, next_id, new_level, construction_start, now)) {
                result.status = ActionStatus::FAIL;
                result.error_code = "database_error";
                result.error_message = "Failed to upgrade mill pond type";
                return result;
            }

            result.result["building_id"] = building_id;
            result.result["pond_type"] = next_id;
            result.result["cost"] = cost;
            result.result["level"] = new_level;
            result.action_timestamp = now;

            tx.commit();
            result.status = ActionStatus::OK;
            return result;
        } catch (const std::exception& e) {
            result.status = ActionStatus::FAIL;
            result.error_code = "database_error";
            result.error_message = std::string(e.what());
            return result;
        }
    }

    std::string getDescription() const override {
        return "Upgrade a mill pond's type (earthen -> timber -> stone)";
    }
};

namespace {

// Returns the unique successor building id whose `built_from` matches the given
// building (or nullopt if the building has no stage upgrade).
std::optional<std::string> findStageSuccessor(GameConfigCache& cache, const std::string& building_name) {
    auto types = cache.getFiefdomBuildingTypes();
    for (const auto& type_obj : types) {
        for (auto it = type_obj.begin(); it != type_obj.end(); ++it) {
            if (it.value().is_object() && it.value().value("built_from", "") == building_name) {
                return it.key();
            }
        }
    }
    return std::nullopt;
}

// Conversion price = max(0, successor level-1 cost − 80% × old building's
// cumulative spent) per resource. The 80% matches the demolish refund, so the
// discount scales with how much was invested in the old building.
nlohmann::json computeConvertCost(GameConfigCache& cache, const std::string& old_name,
                                  const std::string& successor_name, const std::string& pond_type,
                                  int current_level) {
    nlohmann::json result;
    std::string cost_fields[] = {"gold_cost", "silver_pence_cost", "wood_cost", "stone_cost", "steel_cost",
                                 "bronze_cost", "grain_cost", "leather_cost", "mana_cost",
                                 "charcoal_cost", "iron_cost", "ironwork_cost", "fancy_ironwork_cost",
                                 "beams_cost", "boards_cost"};
    std::string resource_fields[] = {"gold", "silver_pence", "wood", "stone", "steel",
                                     "bronze", "grain", "leather", "mana",
                                     "charcoal", "iron", "ironwork", "fancy_ironwork",
                                     "beams", "boards"};
    auto old_cumulative = Validation::calculateCumulativeCost(cache, old_name, pond_type, current_level);
    for (size_t i = 0; i < 15; i++) {
        auto costs = Validation::getBuildingArrayField(cache, successor_name, "", cost_fields[i]);
        if (!costs.is_array() || costs.empty()) continue;
        double successor_lvl1 = costs[0].get<double>();
        if (successor_lvl1 <= 0) continue;
        double old_credit = old_cumulative.value(resource_fields[i], 0.0) * 0.8;
        double price = successor_lvl1 - old_credit;
        if (price > 0) result[resource_fields[i]] = price;
    }
    return result;
}

} // namespace

class ConvertBuildingActionHandler : public ActionHandler {
public:
    ActionResult validate(const json& payload, const ActionContext& ctx) override {
        ActionResult result;

        if (!payload.contains("fiefdom_id")) {
            result.status = ActionStatus::FAIL;
            result.error_code = "fiefdom_id_required";
            result.error_message = "fiefdom_id is required";
            return result;
        }
        if (!payload.contains("building_id")) {
            result.status = ActionStatus::FAIL;
            result.error_code = "building_id_required";
            result.error_message = "building_id is required";
            return result;
        }

        int fiefdom_id = payload["fiefdom_id"];
        int building_id = payload["building_id"];

        if (!Validation::userOwnsFiefdom(ctx, fiefdom_id)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "not_owner";
            result.error_message = "User does not own this fiefdom";
            return result;
        }

        auto& db = Database::getInstance().gameDB();
        std::string building_name;
        int current_level;
        std::string pond_type;
        int owning_fiefdom = 0;
        db << "SELECT fiefdom_id, name, level, pond_type FROM fiefdom_buildings WHERE id = ?;"
           << building_id
           >> [&](int fid, std::string name, int lvl, std::string pt) {
               owning_fiefdom = fid;
               building_name = name;
               current_level = lvl;
               pond_type = pt;
           };

        if (owning_fiefdom != fiefdom_id) {
            result.status = ActionStatus::FAIL;
            result.error_code = "not_owner";
            result.error_message = "User does not own this building";
            return result;
        }
        if (current_level == 0) {
            result.status = ActionStatus::FAIL;
            result.error_code = "upgrade_in_progress";
            result.error_message = "Building is under construction";
            return result;
        }

        auto successor_opt = findStageSuccessor(*ctx.config_cache, building_name);
        if (!successor_opt) {
            result.status = ActionStatus::FAIL;
            result.error_code = "no_stage_upgrade";
            result.error_message = "This building has no stage upgrade";
            return result;
        }
        std::string successor = *successor_opt;

        auto succ_cfg_opt = Validation::getBuildingConfig(*ctx.config_cache, successor);
        if (!succ_cfg_opt) {
            result.status = ActionStatus::FAIL;
            result.error_code = "invalid_config";
            result.error_message = "Successor building configuration not found";
            return result;
        }
        auto succ_cfg = *succ_cfg_opt;

        // max_per_fiefdom of the successor (exact-type count, excluding this row).
        if (succ_cfg.contains("max_per_fiefdom") && succ_cfg["max_per_fiefdom"].is_number()) {
            int max_count = succ_cfg["max_per_fiefdom"].get<int>();
            if (max_count >= 1) {
                int succ_count = 0;
                db << "SELECT COUNT(*) FROM fiefdom_buildings WHERE fiefdom_id = ? AND name = ? AND id != ? AND level > 0;"
                   << fiefdom_id << successor << building_id
                   >> [&](int c) { succ_count = c; };
                if (succ_count >= max_count) {
                    result.status = ActionStatus::FAIL;
                    result.error_code = "max_per_fiefdom_reached";
                    result.error_message = "Only " + std::to_string(max_count) + " " + successor + "(s) may exist in a fiefdom";
                    return result;
                }
            }
        }

        // Successor level-1 prerequisites + dependencies (chain-aware).
        auto prereqs_opt = Validation::getPrerequisitesForLevel(*ctx.config_cache, successor, 1);
        if (prereqs_opt && !prereqs_opt->empty()) {
            if (!Validation::checkFiefdomPrerequisites(*ctx.config_cache, fiefdom_id, *prereqs_opt)) {
                result.status = ActionStatus::FAIL;
                result.error_code = "prerequisites_not_met";
                result.error_message = "Successor building prerequisites not satisfied";
                return result;
            }
        }
        nlohmann::json deps = Validation::getDependenciesForLevel(*ctx.config_cache, successor, 1);
        if (!deps.empty()) {
            auto all_buildings = Validation::getFiefdomAllBuildings(fiefdom_id);
            auto dep_result = Validation::checkBuildingDependencies(*ctx.config_cache, fiefdom_id, all_buildings, deps);
            if (!dep_result.first) {
                result.status = ActionStatus::FAIL;
                result.error_code = "dependencies_not_met";
                result.error_message = dep_result.second;
                return result;
            }
        }

        // Arable land: converting may claim more acres than the current stage.
        // `getAvailableArableAcres` already reflects this building's current
        // acres (they are freed on conversion), so only the delta must fit.
        {
            int current_acres = ctx.config_cache->getBuildingArableAcres(building_name);
            int successor_acres = ctx.config_cache->getBuildingArableAcres(successor);
            int delta = successor_acres - current_acres;
            if (delta > 0) {
                double available = Validation::getAvailableArableAcres(*ctx.config_cache, fiefdom_id);
                if (available + 0.0001 < delta) {
                    result.status = ActionStatus::FAIL;
                    result.error_code = "insufficient_arable_land";
                    result.error_message = "Not enough arable land to upgrade this building";
                    return result;
                }
            }
        }

        // Forest land: same delta gating as arable for the woodcutter chain
        // (woodcutter 80 -> coppicer 60 frees acres; coppicer 60 -> timber
        // hauler 70 claims 10).
        {
            int current_acres = ctx.config_cache->getBuildingForestAcres(building_name);
            int successor_acres = ctx.config_cache->getBuildingForestAcres(successor);
            int delta = successor_acres - current_acres;
            if (delta > 0) {
                double available = Validation::getAvailableForestAcres(*ctx.config_cache, fiefdom_id);
                if (available + 0.0001 < delta) {
                    result.status = ActionStatus::FAIL;
                    result.error_code = "insufficient_forest_land";
                    result.error_message = "Not enough forest land to upgrade this building";
                    return result;
                }
            }
        }

        nlohmann::json convert_cost = computeConvertCost(*ctx.config_cache, building_name, successor, pond_type, current_level);
        if (!Validation::hasEnoughResources(*ctx.config_cache, fiefdom_id, convert_cost)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "insufficient_resources";
            result.error_message = "Not enough resources to convert this building";
            return result;
        }

        result.status = ActionStatus::OK;
        return result;
    }

    ActionResult execute(const json& payload, const ActionContext& ctx) override {
        ActionResult result;
        auto validate_result = validate(payload, ctx);
        if (validate_result.status != ActionStatus::OK) return validate_result;

        int fiefdom_id = payload["fiefdom_id"];
        int building_id = payload["building_id"];

        auto& db = Database::getInstance().gameDB();
        std::string building_name;
        int current_level;
        std::string pond_type;
        db << "SELECT name, level, pond_type FROM fiefdom_buildings WHERE id = ?;"
           << building_id
           >> [&](std::string name, int lvl, std::string pt) {
               building_name = name;
               current_level = lvl;
               pond_type = pt;
           };

        auto successor = findStageSuccessor(*ctx.config_cache, building_name);
        if (!successor) {
            result.status = ActionStatus::FAIL;
            result.error_code = "no_stage_upgrade";
            result.error_message = "This building has no stage upgrade";
            return result;
        }

        nlohmann::json convert_cost = computeConvertCost(*ctx.config_cache, building_name, *successor, pond_type, current_level);

        int64_t now = Validation::getCurrentTimestamp();
        try {
            Validation::TransactionGuard tx(Database::getInstance().gameDB());

            auto deduct_result = Validation::deductResources(*ctx.config_cache, fiefdom_id, convert_cost, result);
            if (deduct_result.status != ActionStatus::OK) return deduct_result;

            auto succ_cfg = Validation::getBuildingConfig(*ctx.config_cache, *successor);
            bool instant = false;
            if (succ_cfg && succ_cfg->contains("construction_times") && (*succ_cfg)["construction_times"].is_array() &&
                !(*succ_cfg)["construction_times"].empty()) {
                instant = ((*succ_cfg)["construction_times"][0].get<double>() == 0);
            }
            int new_level = instant ? 1 : 0;
            int64_t construction_start = instant ? 0 : now;

            if (!FiefdomFetcher::updateBuildingType(building_id, *successor, new_level, construction_start, now)) {
                result.status = ActionStatus::FAIL;
                result.error_code = "database_error";
                result.error_message = "Failed to convert building";
                return result;
            }

            result.result["building_id"] = building_id;
            result.result["building_type"] = *successor;
            result.result["level"] = new_level;
            result.result["construction_start_ts"] = construction_start;
            result.result["cost"] = convert_cost;
            result.action_timestamp = now;

            tx.commit();
            result.status = ActionStatus::OK;
            return result;
        } catch (const std::exception& e) {
            result.status = ActionStatus::FAIL;
            result.error_code = "database_error";
            result.error_message = std::string(e.what());
            return result;
        }
    }

    std::string getDescription() const override {
        return "Convert a building to its next stage (discount based on invested level)";
    }
};

void registerAllActionHandlers(ActionRegistry& registry) {
    registry.registerHandler("build",
        [](const json& p, const ActionContext& ctx) { BuildActionHandler h; return h.validate(p, ctx); },
        [](const json& p, const ActionContext& ctx) { BuildActionHandler h; return h.execute(p, ctx); },
        "Build structures");

    registry.registerHandler("demolish",
        [](const json& p, const ActionContext& ctx) { DemolishActionHandler h; return h.validate(p, ctx); },
        [](const json& p, const ActionContext& ctx) { DemolishActionHandler h; return h.execute(p, ctx); },
        "Demolish buildings (80% refund)");

    registry.registerHandler("move",
        [](const json& p, const ActionContext& ctx) { MoveBuildingActionHandler h; return h.validate(p, ctx); },
        [](const json& p, const ActionContext& ctx) { MoveBuildingActionHandler h; return h.execute(p, ctx); },
        "Move buildings (10% cost)");

    registry.registerHandler("build_wall",
        [](const json& p, const ActionContext& ctx) { BuildWallActionHandler h; return h.validate(p, ctx); },
        [](const json& p, const ActionContext& ctx) { BuildWallActionHandler h; return h.execute(p, ctx); },
        "Build/upgrade walls");

    registry.registerHandler("upgrade",
        [](const json& p, const ActionContext& ctx) { UpgradeActionHandler h; return h.validate(p, ctx); },
        [](const json& p, const ActionContext& ctx) { UpgradeActionHandler h; return h.execute(p, ctx); },
        "Upgrade buildings and walls");

    registry.registerHandler("upgrade_pond_type",
        [](const json& p, const ActionContext& ctx) { UpgradePondTypeActionHandler h; return h.validate(p, ctx); },
        [](const json& p, const ActionContext& ctx) { UpgradePondTypeActionHandler h; return h.execute(p, ctx); },
        "Upgrade a mill pond's type");

    registry.registerHandler("convert",
        [](const json& p, const ActionContext& ctx) { ConvertBuildingActionHandler h; return h.validate(p, ctx); },
        [](const json& p, const ActionContext& ctx) { ConvertBuildingActionHandler h; return h.execute(p, ctx); },
        "Convert a building to its next stage");

    registry.registerHandler("train_troops",
        [](const json& p, const ActionContext& ctx) { TrainTroopsActionHandler h; return h.validate(p, ctx); },
        [](const json& p, const ActionContext& ctx) { TrainTroopsActionHandler h; return h.execute(p, ctx); },
        "Train combatants");

    registry.registerHandler("research_magic",
        [](const json& p, const ActionContext& ctx) { ResearchMagicActionHandler h; return h.validate(p, ctx); },
        [](const json& p, const ActionContext& ctx) { ResearchMagicActionHandler h; return h.execute(p, ctx); },
        "Research magic");

    registry.registerHandler("research_tech",
        [](const json& p, const ActionContext& ctx) { ResearchTechActionHandler h; return h.validate(p, ctx); },
        [](const json& p, const ActionContext& ctx) { ResearchTechActionHandler h; return h.execute(p, ctx); },
        "Research technology");
}

namespace Validation {

std::optional<nlohmann::json> getPrerequisitesForLevel(
    GameConfigCache& cache,
    const std::string& building_type,
    int target_level
) {
    auto config_opt = getBuildingConfig(cache, building_type);
    if (!config_opt) return std::nullopt;
    
    auto config = *config_opt;
    if (!config.contains("prerequisites") || !config["prerequisites"].is_array()) {
        return std::nullopt;
    }
    
    auto prerequisites = config["prerequisites"];
    int arr_size = static_cast<int>(prerequisites.size());
    
    if (arr_size == 0) {
        return nlohmann::json::object();
    }
    
    if (target_level <= 0) {
        return nlohmann::json::object();
    }
    // Indexed by level - 1: prerequisites[0] = Level 1 (build), etc.
    int index = target_level - 1;
    if (index < arr_size) {
        return prerequisites[index];
    }
    
    if (arr_size == 1) {
        return prerequisites[0];
    }
    
    nlohmann::json last_obj = prerequisites[arr_size - 1];
    nlohmann::json prev_obj = prerequisites[arr_size - 2];
    nlohmann::json result;
    
    std::set<std::string> all_keys;
    for (auto& [key, val] : last_obj.items()) all_keys.insert(key);
    for (auto& [key, val] : prev_obj.items()) all_keys.insert(key);
    
    for (const auto& key : all_keys) {
        int last_val = last_obj.value(key, 0);
        int prev_val = prev_obj.value(key, 0);
        int delta = last_val - prev_val;
        int extrapolated = last_val + delta * (index - (arr_size - 1));
        result[key] = extrapolated;
    }
    
    return result;
}

// Resolves the ordered stage chain [root, ..., building_name] for a building by
// walking `built_from` links. A building's chain includes itself and every
// lower stage it could have been converted from. Chain depth is unbounded
// ("3 stages" is only a design default, not a limit).
std::vector<std::string> getBuildingStageChain(GameConfigCache& cache, const std::string& building_name) {
    std::vector<std::string> path;
    std::string cur = building_name;
    std::set<std::string> seen;
    while (!cur.empty()) {
        if (seen.count(cur)) break;
        seen.insert(cur);
        path.push_back(cur);
        auto config_opt = getBuildingConfig(cache, cur);
        if (!config_opt) break;
        std::string from = config_opt->value("built_from", "");
        if (from.empty()) break;
        cur = from;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// A building satisfies a prerequisite/dependency requirement for any stage in
// its own chain at or below its current stage (a villein counts as a peasant;
// a plain peasant never counts as a villein), OR if the required id is the
// building's `class` (a `class` groups types definitively, e.g. the "peasant"
// class covers villein/freeholder/yeoman).
bool buildingSatisfiesRequirement(GameConfigCache& cache, const std::string& building_name, const std::string& required_id) {
    auto chain = getBuildingStageChain(cache, building_name);
    if (std::find(chain.begin(), chain.end(), required_id) != chain.end()) {
        return true;
    }
    const std::string cls = cache.getBuildingClass(building_name);
    return !cls.empty() && cls == required_id;
}

int getBuildingLevelInFiefdom(GameConfigCache& cache, int fiefdom_id, const std::string& building_name) {
    auto& db = Database::getInstance().gameDB();
    int level = 0;
    db << "SELECT name, level FROM fiefdom_buildings WHERE fiefdom_id = ? AND level > 0;"
       << fiefdom_id
       >> [&](std::string name, int lvl) {
           if (buildingSatisfiesRequirement(cache, name, building_name)) {
               level = std::max(level, lvl);
           }
       };
    return level;
}

int getFiefdomManorLevel(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    int level = 0;
    db << "SELECT manor_level FROM fiefdoms WHERE id = ?;"
       << fiefdom_id
       >> [&](int ml) { level = ml; };
    return level;
}

// Sums the arable acres claimed by every completed building in the fiefdom.
// Acres are a per-building-type config field (`arable_acres`); a building
// claims its current type's acres (stage chains claim their own current stage,
// since each building's `name` reflects the type it was built/converted to).
int getUsedArableAcres(GameConfigCache& cache, int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    int used = 0;
    db << "SELECT name FROM fiefdom_buildings WHERE fiefdom_id = ? AND level > 0;"
       << fiefdom_id
       >> [&](std::string name) {
           used += cache.getBuildingArableAcres(name);
       };
    return used;
}

// Total arable acres the manor has, scaling with the home-base (manor) level.
double getTotalArableAcres(GameConfigCache& cache, int fiefdom_id) {
    return cache.getArableLandByLevel(getFiefdomManorLevel(fiefdom_id));
}

// Remaining buildable acres = total (from manor level) minus acres already used.
double getAvailableArableAcres(GameConfigCache& cache, int fiefdom_id) {
    return getTotalArableAcres(cache, fiefdom_id) - getUsedArableAcres(cache, fiefdom_id);
}

// Forest acres work exactly like arable acres but for the wood producers
// (woodcutter/coppicer/timber_hauler). Forest is an off-map resource scaling
// with manor level (`forest_land_by_level`), claimed per building type via the
// `forest_acres` config field.
int getUsedForestAcres(GameConfigCache& cache, int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    int used = 0;
    db << "SELECT name FROM fiefdom_buildings WHERE fiefdom_id = ? AND level > 0;"
       << fiefdom_id
       >> [&](std::string name) {
           used += cache.getBuildingForestAcres(name);
       };
    return used;
}

double getTotalForestAcres(GameConfigCache& cache, int fiefdom_id) {
    return cache.getForestLandByLevel(getFiefdomManorLevel(fiefdom_id));
}

double getAvailableForestAcres(GameConfigCache& cache, int fiefdom_id) {
    return getTotalForestAcres(cache, fiefdom_id) - getUsedForestAcres(cache, fiefdom_id);
}

bool checkFiefdomPrerequisites(GameConfigCache& cache, int fiefdom_id, const nlohmann::json& prerequisites) {
    if (prerequisites.empty()) return true;
    
    for (auto& [building_id, required_level] : prerequisites.items()) {
        if (building_id == "manor_level") {
            int current_manor_level = getFiefdomManorLevel(fiefdom_id);
            if (current_manor_level < required_level.get<int>()) {
                return false;
            }
        } else {
            int current_level = getBuildingLevelInFiefdom(cache, fiefdom_id, building_id);
            if (current_level < required_level.get<int>()) {
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Dependency helpers
// ---------------------------------------------------------------------------

/// Gets the dependencies array for a building at a given target level.
/// Returns an empty array if no dependencies configured.
nlohmann::json getDependenciesForLevel(
    GameConfigCache& cache,
    const std::string& building_type,
    int target_level
) {
    auto config_opt = getBuildingConfig(cache, building_type);
    if (!config_opt) return nlohmann::json::array();

    auto config = *config_opt;
    if (!config.contains("dependencies") || !config["dependencies"].is_array()) {
        return nlohmann::json::array();
    }

    auto deps = config["dependencies"];
    int arr_size = static_cast<int>(deps.size());

    if (arr_size == 0) return nlohmann::json::array();
    if (target_level <= 0) return nlohmann::json::array();
    // Indexed by level - 1: deps[0] = Level 1 (build), deps[1] = Level 2, etc.
    int index = target_level - 1;
    if (index < arr_size) return deps[index];
    // Extrapolate: use last entry
    return deps[arr_size - 1];
}

/// Counts buildings in a fiefdom that satisfy a requirement (the building's
/// stage chain includes `target_building`) and meet the minimum level.
int countBuildingsByType(GameConfigCache& cache, int fiefdom_id, const std::string& target_building, int min_level) {
    auto& db = Database::getInstance().gameDB();
    int count = 0;
    db << "SELECT name, level FROM fiefdom_buildings "
          "WHERE fiefdom_id = ? AND level >= ? AND level > 0;"
       << fiefdom_id << min_level
       >> [&](std::string name, int lvl) {
           if (buildingSatisfiesRequirement(cache, name, target_building)) count++;
       };
    return count;
}

/// Aggregates dependency requirements across all buildings in a fiefdom,
/// plus optional additional dependencies (e.g., from a building being validated).
/// Returns a map: target_building -> (max_shared_count, sum_exclusive_count)
std::map<std::string, std::pair<int, int>> aggregateFiefdomDependencies(
    GameConfigCache& cache,
    int fiefdom_id,
    const std::vector<BuildingData>& buildings,
    const nlohmann::json& additional_deps = nlohmann::json::array()
) {
    std::map<std::string, std::pair<int, int>> result;

    // Aggregate from existing buildings
    for (const auto& building : buildings) {
        if (building.level <= 0) continue;
        nlohmann::json deps = getDependenciesForLevel(cache, building.name, building.level);
        if (deps.empty()) continue;

        for (const auto& dep : deps) {
            if (!dep.contains("target_building") || !dep.contains("count")) continue;
            std::string tb = dep["target_building"].get<std::string>();
            int count = dep["count"].is_number() ? dep["count"].get<int>() : 1;
            bool shared = dep.value("shared", false);

            auto& entry = result[tb];
            if (shared) {
                entry.first = std::max(entry.first, count);
            } else {
                entry.second += count;
            }
        }
    }

    // Add additional dependencies (from the building being validated)
    for (const auto& dep : additional_deps) {
        if (!dep.contains("target_building") || !dep.contains("count")) continue;
        std::string tb = dep["target_building"].get<std::string>();
        int count = dep["count"].is_number() ? dep["count"].get<int>() : 1;
        bool shared = dep.value("shared", false);

        auto& entry = result[tb];
        if (shared) {
            entry.first = std::max(entry.first, count);
        } else {
            entry.second += count;
        }
    }

    return result;
}

/// Checks if a fiefdom meets all building dependencies.
/// Returns { true, "" } if met, or { false, "reason string" } if not.
std::pair<bool, std::string> checkBuildingDependencies(
    GameConfigCache& cache,
    int fiefdom_id,
    const std::vector<BuildingData>& buildings,
    const nlohmann::json& deps_to_check
) {
    if (deps_to_check.empty()) return {true, ""};

    auto aggregated = aggregateFiefdomDependencies(cache, fiefdom_id, buildings, deps_to_check);

    for (const auto& [target_building, counts] : aggregated) {
        int shared_needed = counts.first;
        int exclusive_needed = counts.second;
        int min_level = 1; // default minimum level for counting

        // Use the min_level from the deps_to_check if provided
        // (all deps in the same validation share the same target_building,
        //  but could have different min_levels — take the highest)
        for (const auto& dep : deps_to_check) {
            if (dep.contains("target_building") && dep["target_building"] == target_building) {
                int ml = dep.value("min_level", 1);
                if (ml > min_level) min_level = ml;
            }
        }

        int total = countBuildingsByType(cache, fiefdom_id, target_building, min_level);
        int needed = shared_needed + exclusive_needed;

        if (total < needed) {
            int shortfall = needed - total;
            return {false, "Need " + std::to_string(needed) + " " + target_building +
                    " (have " + std::to_string(total) + ", need " + std::to_string(shortfall) + " more)"};
        }
    }

    return {true, ""};
}

/// Gets all buildings in a fiefdom (used by dependency checker).
std::vector<BuildingData> getFiefdomAllBuildings(int fiefdom_id) {
    return FiefdomFetcher::fetchFiefdomBuildings(fiefdom_id);
}

} // namespace Validation

} // namespace GameLogic