#include "ActionHandlers.hpp"
#include "FiefdomFetcher.hpp"
#include "Database.hpp"
#include "GameConfigCache.hpp"
#include "GridCollision.hpp"
#include <optional>

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
    
    if (!Validation::buildingTypeExists(building_type)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "unknown_building";
        result.error_message = "Unknown building type: " + building_type;
        return result;
    }

    auto config_opt = Validation::getBuildingConfig(building_type);
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

    if (!payload.contains("x") || !payload.contains("y")) {
        result.status = ActionStatus::FAIL;
        result.error_code = "coordinates_required";
        result.error_message = "x and y coordinates are required for building placement";
        return result;
    }

    int x = payload["x"];
    int y = payload["y"];

    if (!Validation::canBuildBuildingHere(building_type, fiefdom_id, x, y)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "invalid_location";
        result.error_message = "Cannot build at specified location";
        return result;
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
    
    auto config = Validation::getBuildingConfig(building_type);
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
        
        auto deduct_result = Validation::deductResources(fiefdom_id, costs, result);
        if (deduct_result.status != ActionStatus::OK) {
            return deduct_result;
        }

        if (!FiefdomFetcher::createBuilding(fiefdom_id, building_type, 0, now, 0, "", x, y)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "database_error";
            result.error_message = "Failed to create building";
            return result;
        }

        result.result["building_type"] = building_type;
        result.result["fiefdom_id"] = fiefdom_id;
        result.result["x"] = x;
        result.result["y"] = y;
        result.result["construction_start_ts"] = now;
        result.result["level"] = 0;
        
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
        int fiefdom_id = ctx.requesting_fiefdom_id;

        auto& db = Database::getInstance().gameDB();
        db << "SELECT name, level FROM fiefdom_buildings WHERE id = ?;"
           << building_id
           >> [&](std::string name, int lvl) {
               building_name = name;
               level = lvl;
           };

        try {
            Validation::TransactionGuard tx(Database::getInstance().gameDB());

            auto cumulative = Validation::calculateCumulativeCost(building_name, level);
            nlohmann::json refund;

            for (auto& [key, value] : cumulative.items()) {
                int refund_amount = static_cast<int>(value.get<int>() * 0.8);
                refund[key] = refund_amount;
            }

            Validation::refundResources(fiefdom_id, refund, result);
            Validation::deleteBuilding(building_id);

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

        auto placement = GridCollision::checkPlacement(ctx.requesting_fiefdom_id, building_name, x, y, false, building_id);
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
            std::string cost_fields[] = {"gold_cost", "wood_cost", "stone_cost", "steel_cost", "bronze_cost", "grain_cost", "leather_cost", "mana_cost"};
            std::string resource_fields[] = {"gold", "wood", "stone", "steel", "bronze", "grain", "leather", "mana"};

            auto config_opt = Validation::getBuildingConfig(building_name);
            if (config_opt) {
                auto config = *config_opt;
                for (size_t i = 0; i < 8; i++) {
                    std::string field = cost_fields[i];
                    if (config.contains(field) && config[field].is_array()) {
                        auto costs = config[field];
                        int level_index = level - 1;
                        if (level_index >= 0 && level_index < costs.size()) {
                            int full_cost = costs[level_index].get<int>();
                            cost[resource_fields[i]] = full_cost / 10;
                        }
                    }
                }
            }

            auto deduct_result = Validation::deductResources(ctx.requesting_fiefdom_id, cost, result);
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

bool hasEnoughResources(int fiefdom_id, const json& costs) {
    auto& db = Database::getInstance().gameDB();
    int gold = 0, wood = 0, stone = 0, steel = 0, bronze = 0, grain = 0, leather = 0, mana = 0;
    db << "SELECT gold, wood, stone, steel, bronze, grain, leather, mana FROM fiefdoms WHERE id = ?;"
       << fiefdom_id
       >> [&](int g, int w, int st, int stl, int b, int gr, int l, int m) {
           gold = g; wood = w; stone = st; steel = stl; bronze = b; grain = gr; leather = l; mana = m;
       };
    
    if (costs.contains("gold") && gold < costs["gold"]) return false;
    if (costs.contains("wood") && wood < costs["wood"]) return false;
    if (costs.contains("stone") && stone < costs["stone"]) return false;
    if (costs.contains("steel") && steel < costs["steel"]) return false;
    if (costs.contains("bronze") && bronze < costs["bronze"]) return false;
    if (costs.contains("grain") && grain < costs["grain"]) return false;
    if (costs.contains("leather") && leather < costs["leather"]) return false;
    if (costs.contains("mana") && mana < costs["mana"]) return false;
    
    return true;
}

ActionResult deductResources(int fiefdom_id, const json& costs, ActionResult& result) {
    ActionResult r;
    r.status = ActionStatus::OK;
    
    auto& db = Database::getInstance().gameDB();
    
    if (costs.empty()) return r;
    
    int gold = 0, wood = 0, stone = 0, steel = 0, bronze = 0, grain = 0, leather = 0, mana = 0;
    db << "SELECT gold, wood, stone, steel, bronze, grain, leather, mana FROM fiefdoms WHERE id = ?;"
       << fiefdom_id
       >> [&](int g, int w, int st, int stl, int b, int gr, int l, int m) {
           gold = g; wood = w; stone = st; steel = stl; bronze = b; grain = gr; leather = l; mana = m;
       };
    
    std::string resource_fields[] = {"gold", "wood", "stone", "steel", "bronze", "grain", "leather", "mana"};
    int* resource_ptrs[] = {&gold, &wood, &stone, &steel, &bronze, &grain, &leather, &mana};
    
    for (size_t i = 0; i < 8; i++) {
        if (costs.contains(resource_fields[i])) {
            int before = *resource_ptrs[i];
            *resource_ptrs[i] -= costs[resource_fields[i]];
            int after = *resource_ptrs[i];
            
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
    
    db << "UPDATE fiefdoms SET gold = ?, wood = ?, stone = ?, steel = ?, bronze = ?, grain = ?, leather = ?, mana = ? WHERE id = ?;"
       << gold << wood << stone << steel << bronze << grain << leather << mana << fiefdom_id;
    
    return r;
}

bool buildingTypeExists(const std::string& building_type) {
    auto& cache = GameConfigCache::getInstance();
    auto types = cache.getFiefdomBuildingTypes();
    for (const auto& type_obj : types) {
        if (type_obj.contains(building_type)) return true;
    }
    return false;
}

std::optional<json> getBuildingConfig(const std::string& building_type) {
    auto& cache = GameConfigCache::getInstance();
    auto types = cache.getFiefdomBuildingTypes();
    for (const auto& type_obj : types) {
        if (type_obj.contains(building_type)) return type_obj[building_type];
    }
    return std::nullopt;
}

bool canBuildBuildingHere(const std::string& building_type, int fiefdom_id, int x, int y) {
    bool isHomeBase = (building_type == "home_base");
    auto result = GridCollision::checkPlacement(fiefdom_id, building_type, x, y, isHomeBase);
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

std::optional<json> getWallConfig() {
    auto& cache = GameConfigCache::getInstance();
    if (!cache.isLoaded()) return std::nullopt;
    auto config = cache.getAllConfigs();
    if (config.contains("wall_config")) return config["wall_config"];
    return std::nullopt;
}

bool validWallPlacement(int fiefdom_id, const json& payload) {
    auto wall_config_opt = getWallConfig();
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

nlohmann::json calculateCumulativeCost(const std::string& building_type, int current_level) {
    auto config_opt = getBuildingConfig(building_type);
    if (!config_opt) return nlohmann::json::object();

    auto config = *config_opt;
    nlohmann::json cumulative;
    std::string cost_fields[] = {"gold_cost", "wood_cost", "stone_cost", "steel_cost", "bronze_cost", "grain_cost", "leather_cost", "mana_cost"};
    std::string resource_fields[] = {"gold", "wood", "stone", "steel", "bronze", "grain", "leather", "mana"};

    for (size_t i = 0; i < 8; i++) {
        const auto& cost_key = cost_fields[i];
        const auto& resource_key = resource_fields[i];

        if (config.contains(cost_key) && config[cost_key].is_array()) {
            auto costs = config[cost_key];
            int total = 0;
            for (int j = 0; j < current_level && j < costs.size(); j++) {
                total += costs[j].get<int>();
            }
            if (total > 0) cumulative[resource_key] = total;
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

    int gold = 0, wood = 0, stone = 0, steel = 0, bronze = 0, grain = 0, leather = 0, mana = 0;
    db << "SELECT gold, wood, stone, steel, bronze, grain, leather, mana FROM fiefdoms WHERE id = ?;"
       << fiefdom_id
       >> [&](int g, int w, int st, int stl, int b, int gr, int l, int m) {
           gold = g; wood = w; stone = st; steel = stl; bronze = b; grain = gr; leather = l; mana = m;
       };

    std::string resource_fields[] = {"gold", "wood", "stone", "steel", "bronze", "grain", "leather", "mana"};
    int* resource_ptrs[] = {&gold, &wood, &stone, &steel, &bronze, &grain, &leather, &mana};

    for (size_t i = 0; i < 8; i++) {
        if (amounts.contains(resource_fields[i])) {
            int refund = amounts[resource_fields[i]].get<int>();
            int before = *resource_ptrs[i];
            *resource_ptrs[i] += refund;
            int after = *resource_ptrs[i];

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

    db << "UPDATE fiefdoms SET gold = ?, wood = ?, stone = ?, steel = ?, bronze = ?, grain = ?, leather = ?, mana = ? WHERE id = ?;"
       << gold << wood << stone << steel << bronze << grain << leather << mana << fiefdom_id;

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

std::optional<json> getWallConfigByGeneration(int generation) {
    auto config_opt = getWallConfig();
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

bool canAffordWall(int fiefdom_id, int generation, int level) {
    auto config_opt = getWallConfigByGeneration(generation);
    if (!config_opt) return false;

    std::string cost_fields[] = {"gold_cost", "stone_cost"};
    std::string resource_fields[] = {"gold", "stone"};

    auto& db = Database::getInstance().gameDB();
    int gold = 0, stone = 0;
    db << "SELECT gold, stone FROM fiefdoms WHERE id = ?;"
       << fiefdom_id
       >> [&](int g, int s) { gold = g; stone = s; };

    int* resource_ptrs[] = {&gold, &stone};

    for (size_t i = 0; i < 2; i++) {
        if (config_opt->contains(cost_fields[i])) {
            auto costs = (*config_opt)[cost_fields[i]];
            if (costs.is_array() && level > 0 && level <= static_cast<int>(costs.size())) {
                int cost = costs[level - 1].get<int>();
                if (*resource_ptrs[i] < cost) return false;
            }
        }
    }
    return true;
}

int getWallHP(int generation, int level) {
    auto config_opt = getWallConfigByGeneration(generation);
    if (!config_opt) return 0;

    if (config_opt->contains("hp")) {
        auto hp_array = (*config_opt)["hp"];
        if (hp_array.is_array() && level > 0 && level <= static_cast<int>(hp_array.size())) {
            return hp_array[level - 1].get<int>();
        }
    }
    return 0;
}

double getWallMoraleBoost(int generation, int level) {
    auto config_opt = getWallConfigByGeneration(generation);
    if (!config_opt) return 0.0;

    if (config_opt->contains("morale_boost")) {
        auto morale_array = (*config_opt)["morale_boost"];
        if (morale_array.is_array() && level > 0 && level <= static_cast<int>(morale_array.size())) {
            return morale_array[level - 1].get<double>();
        }
    }
    return 0.0;
}

nlohmann::json calculateWallUpgradeCost(int generation, int current_level) {
    auto config_opt = getWallConfigByGeneration(generation);
    nlohmann::json cost;

    if (!config_opt) return cost;

    std::string cost_fields[] = {"gold_cost", "stone_cost"};
    std::string resource_fields[] = {"gold", "stone"};

    for (size_t i = 0; i < 2; i++) {
        if (config_opt->contains(cost_fields[i])) {
            auto costs = (*config_opt)[cost_fields[i]];
            if (costs.is_array() && current_level + 1 > 0 && (current_level + 1) <= static_cast<int>(costs.size())) {
                cost[resource_fields[i]] = costs[current_level].get<int>();
            }
        }
    }
    return cost;
}

nlohmann::json getDemolishRefund(int building_id) {
    auto& db = Database::getInstance().gameDB();
    std::string building_name;
    int level;
    db << "SELECT name, level FROM fiefdom_buildings WHERE id = ?;"
       << building_id
       >> [&](std::string name, int lvl) {
           building_name = name;
           level = lvl;
       };

    if (building_name.empty()) return nlohmann::json::object();

    auto cumulative = calculateCumulativeCost(building_name, level);
    nlohmann::json refund;

    for (auto& [key, value] : cumulative.items()) {
        int refund_amount = static_cast<int>(value.get<int>() * 0.8);
        refund[key] = refund_amount;
    }

    return refund;
}

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

    auto config_opt = getWallConfigByGeneration(wall_generation);
    if (!config_opt) {
        result.status = ActionStatus::FAIL;
        result.error_code = "generation_invalid";
        result.error_message = "Invalid wall generation: " + std::to_string(wall_generation);
        return result;
    }

    if (wall_generation > 1 && !hasWallGeneration(fiefdom_id, wall_generation - 1)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "generation_sequence_required";
        result.error_message = "Must build wall generation " + std::to_string(wall_generation - 1) + " first";
        return result;
    }

    if (hasWallGeneration(fiefdom_id, wall_generation)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "generation_exists";
        result.error_message = "Wall generation " + std::to_string(wall_generation) + " already exists";
        return result;
    }

    if (!canAffordWall(fiefdom_id, wall_generation, 1)) {
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

    auto config_opt = getWallConfigByGeneration(wall_generation);
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

        auto deduct_result = Validation::deductResources(fiefdom_id, cost, result);
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
            auto [bw, bh] = GridCollision::getBuildingDimensionsPair(building["name"]);

            if (GridCollision::overlapsWalls(fiefdom_id, wall_generation, bx, by, bw, bh)) {
                int building_id = building["id"].get<int>();
                auto refund = getDemolishRefund(building_id);

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

        int initial_hp = getWallHP(wall_generation, 1);
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
        db << "SELECT name, level FROM fiefdom_buildings WHERE id = ?;"
           << building_id
           >> [&](std::string name, int lvl) {
               building_name = name;
               current_level = lvl;
           };

        if (current_level == 0) {
            result.status = ActionStatus::FAIL;
            result.error_code = "upgrade_in_progress";
            result.error_message = "Building is already under construction";
            return result;
        }

        auto config_opt = Validation::getBuildingConfig(building_name);
        if (!config_opt) {
            result.status = ActionStatus::FAIL;
            result.error_code = "invalid_config";
            result.error_message = "Building configuration not found";
            return result;
        }

        int max_level = config_opt->value("max_level", 1);
        if (current_level >= max_level) {
            result.status = ActionStatus::FAIL;
            result.error_code = "max_level_reached";
            result.error_message = "Building is at maximum level";
            return result;
        }

        nlohmann::json next_cost;
        std::string cost_fields[] = {"gold_cost", "wood_cost", "stone_cost", "steel_cost", "bronze_cost", "grain_cost", "leather_cost", "mana_cost"};
        for (const auto& field : cost_fields) {
            if (config_opt->contains(field)) {
                auto costs = (*config_opt)[field];
                if (costs.is_array() && current_level > 0 && current_level < static_cast<int>(costs.size())) {
                    next_cost[field] = costs[current_level].get<int>();
                }
            }
        }

        if (!Validation::hasEnoughResources(fiefdom_id, next_cost)) {
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

        auto config_opt = getWallConfigByGeneration(generation);
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

        auto cost = calculateWallUpgradeCost(generation, current_level);
        if (!Validation::hasEnoughResources(fiefdom_id, cost)) {
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
            auto& db = Database::getInstance().gameDB();
            db << "SELECT name, level FROM fiefdom_buildings WHERE id = ?;"
               << building_id
               >> [&](std::string name, int lvl) {
                   building_name = name;
                   current_level = lvl;
               };

            auto config_opt = Validation::getBuildingConfig(building_name);
            if (!config_opt) {
                result.status = ActionStatus::FAIL;
                result.error_code = "invalid_config";
                result.error_message = "Building configuration not found";
                return result;
            }

            nlohmann::json next_cost;
            std::string cost_fields[] = {"gold_cost", "wood_cost", "stone_cost", "steel_cost", "bronze_cost", "grain_cost", "leather_cost", "mana_cost"};
            for (const auto& field : cost_fields) {
                if (config_opt->contains(field)) {
                    auto costs = (*config_opt)[field];
                    if (costs.is_array() && current_level > 0 && current_level < static_cast<int>(costs.size())) {
                        next_cost[field] = costs[current_level].get<int>();
                    }
                }
            }

            auto deduct_result = Validation::deductResources(fiefdom_id, next_cost, result);
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

            auto cost = calculateWallUpgradeCost(generation, current_level);
            auto deduct_result = Validation::deductResources(fiefdom_id, cost, result);
            if (deduct_result.status != ActionStatus::OK) return deduct_result;

            int new_hp = getWallHP(generation, current_level + 1);
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

void registerAllActionHandlers(ActionRegistry& registry) {
    registry.registerHandler("build",
        BuildActionHandler().validate,
        BuildActionHandler().execute,
        "Build structures");

    registry.registerHandler("demolish",
        DemolishActionHandler().validate,
        DemolishActionHandler().execute,
        "Demolish buildings (80% refund)");

    registry.registerHandler("move",
        MoveBuildingActionHandler().validate,
        MoveBuildingActionHandler().execute,
        "Move buildings (10% cost)");

    registry.registerHandler("build_wall",
        BuildWallActionHandler().validate,
        BuildWallActionHandler().execute,
        "Build/upgrade walls");

    registry.registerHandler("upgrade",
        UpgradeActionHandler().validate,
        UpgradeActionHandler().execute,
        "Upgrade buildings and walls");

    registry.registerHandler("train_troops",
        TrainTroopsActionHandler().validate,
        TrainTroopsActionHandler().execute,
        "Train combatants");

    registry.registerHandler("research_magic",
        ResearchMagicActionHandler().validate,
        ResearchMagicActionHandler().execute,
        "Research magic");

    registry.registerHandler("research_tech",
        ResearchTechActionHandler().validate,
        ResearchTechActionHandler().execute,
        "Research technology");
}

} // namespace Validation

} // namespace GameLogic