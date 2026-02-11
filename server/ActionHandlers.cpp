#include "ActionHandlers.hpp"
#include "FiefdomFetcher.hpp"
#include "Database.hpp"
#include "GameConfigCache.hpp"
#include <optional>

namespace GameLogic {

void registerAllActionHandlers(ActionRegistry& registry) {
    registry.registerHandler("build", 
        BuildActionHandler().validate, 
        BuildActionHandler().execute,
        "Build/upgrade structures");
    
    registry.registerHandler("build_wall",
        BuildWallActionHandler().validate,
        BuildWallActionHandler().execute,
        "Build/upgrade walls");
    
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

    if (payload.contains("x") && payload.contains("y")) {
        int x = payload["x"];
        int y = payload["y"];
        if (!Validation::canBuildBuildingHere(building_type, fiefdom_id, x, y)) {
            result.status = ActionStatus::FAIL;
            result.error_code = "invalid_location";
            result.error_message = "Cannot build at specified location";
            return result;
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
        
        if (!FiefdomFetcher::createBuilding(fiefdom_id, building_type, 0, now, 0, "")) {
            result.status = ActionStatus::FAIL;
            result.error_code = "database_error";
            result.error_message = "Failed to create building";
            return result;
        }
        
        result.result["building_type"] = building_type;
        result.result["fiefdom_id"] = fiefdom_id;
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

// === BuildWallActionHandler Implementation ===

ActionResult BuildWallActionHandler::validate(const json& payload, const ActionContext& ctx) {
    ActionResult result;
    
    if (!payload.contains("fiefdom_id")) {
        result.status = ActionStatus::FAIL;
        result.error_code = "fiefdom_id_required";
        result.error_message = "fiefdom_id is required";
        return result;
    }
    
    int fiefdom_id = payload["fiefdom_id"];
    
    if (!Validation::userOwnsFiefdom(ctx, fiefdom_id)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "not_owner";
        result.error_message = "User does not own this fiefdom";
        return result;
    }
    
    auto wall_config_opt = Validation::getWallConfig();
    if (!wall_config_opt) {
        result.status = ActionStatus::FAIL;
        result.error_code = "missing_wall_config";
        result.error_message = "Wall configuration not found";
        return result;
    }
    
    if (!Validation::validWallPlacement(fiefdom_id, payload)) {
        result.status = ActionStatus::FAIL;
        result.error_code = "invalid_wall_placement";
        result.error_message = "Cannot place wall at this location";
        return result;
    }
    
    result.status = ActionStatus::OK;
    result.error_message = "OK";
    return result;
}

ActionResult BuildWallActionHandler::execute(const json& payload, const ActionContext& ctx) {
    ActionResult result;
    
    auto validate_result = validate(payload, ctx);
    if (validate_result.status != ActionStatus::OK) {
        return validate_result;
    }
    
    int fiefdom_id = payload["fiefdom_id"];
    int64_t now = Validation::getCurrentTimestamp();
    
    try {
        Validation::TransactionGuard tx(Database::getInstance().gameDB());
        
        auto& db = Database::getInstance().gameDB();
        int current_walls = 0;
        db << "SELECT wall_count FROM fiefdoms WHERE id = ?;" << fiefdom_id
           >> [&](int wall_count) { current_walls = wall_count; };
        
        db << "UPDATE fiefdoms SET wall_count = ? WHERE id = ?;"
           << (current_walls + 1) << fiefdom_id;
        
        addDiff(result, "wall_count", "fiefdom", fiefdom_id, current_walls, current_walls + 1);
        
        tx.commit();
        
        result.status = ActionStatus::OK;
        result.action_timestamp = now;
        result.result["fiefdom_id"] = fiefdom_id;
        result.result["wall_constructed"] = true;
        result.result["new_wall_count"] = current_walls + 1;
        return result;
        
    } catch (const std::exception& e) {
        result.status = ActionStatus::FAIL;
        result.error_code = "database_error";
        result.error_message = std::string(e.what());
        return result;
    }
}

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
    return true;
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

} // namespace Validation

} // namespace GameLogic