#pragma once
#include "game_logic.hpp"
#include "FiefdomData.hpp"
#include "GameConfigCache.hpp"
#include "MoraleCalculator.hpp"
#include <nlohmann/json.hpp>

namespace GameLogic {

class ActionHandler {
public:
    virtual ~ActionHandler() = default;
    virtual ActionResult validate(const nlohmann::json& payload, const ActionContext& ctx) = 0;
    virtual ActionResult execute(const nlohmann::json& payload, const ActionContext& ctx) = 0;
    virtual std::string getDescription() const = 0;

protected:
    void addDiff(ActionResult& result, const std::string& field, const std::string& source_type,
                 int source_id, const nlohmann::json& from_value, const nlohmann::json& to_value) {
        DiffValue diff;
        diff.field = field;
        diff.source_type = source_type;
        diff.source_id = source_id;
        diff.entity_key = source_type + "_id";
        diff.from_value = from_value;
        diff.to_value = to_value;
        result.side_effects.push_back(diff);
    }
};

namespace Validation {
    bool userOwnsFiefdom(const ActionContext& ctx, int fiefdom_id);
    bool fiefdomExists(int fiefdom_id);
    bool hasEnoughResources(int fiefdom_id, const nlohmann::json& costs);
    ActionResult deductResources(int fiefdom_id, const nlohmann::json& costs, ActionResult& result);
    bool buildingTypeExists(const std::string& building_type);
    std::optional<nlohmann::json> getBuildingConfig(const std::string& building_type);
    bool canBuildBuildingHere(const std::string& building_type, int fiefdom_id, int x, int y);
    int64_t getCurrentTimestamp();
    std::optional<nlohmann::json> getWallConfig();
    bool validWallPlacement(int fiefdom_id, const nlohmann::json& payload);
    
    class TransactionGuard {
    public:
        TransactionGuard(sqlite::database& db);
        ~TransactionGuard();
        void commit();
    private:
        sqlite::database& db_;
        bool committed_ = false;
    };
}

} // namespace GameLogic