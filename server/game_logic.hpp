#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <nlohmann/json.hpp>
#include "Database.hpp"
#include "ApiResponse.hpp"
#include "FiefdomData.hpp"

namespace GameLogic {

using Timestamp = int64_t;
using DurationSeconds = int64_t;

enum class ActionStatus {
    OK,
    FAIL,
    PARTIAL
};

struct ActionContext {
    int requesting_fiefdom_id;
    int requesting_character_id;
    std::string request_id;
    std::string ip_address;
};

struct DiffValue {
    std::string field;
    std::string source_type;
    int source_id;
    std::string entity_key;
    nlohmann::json from_value;
    nlohmann::json to_value;
};

struct ActionResult {
    ActionStatus status = ActionStatus::OK;
    std::string error_message;
    std::string error_code;
    nlohmann::json result;
    std::vector<DiffValue> side_effects;
    Timestamp action_timestamp;
};

using ValidateFn = std::function<ActionResult(const nlohmann::json&, const ActionContext&)>;
using ExecuteFn = std::function<ActionResult(const nlohmann::json&, const ActionContext&)>;

class ActionRegistry {
public:
    static ActionRegistry& getInstance();
    
    void registerHandler(
        const std::string& action_type,
        ValidateFn validate_fn,
        ExecuteFn execute_fn,
        const std::string& description
    );
    
    ActionResult validate(const std::string& action_type, const nlohmann::json& payload, const ActionContext& ctx);
    ActionResult execute(const std::string& action_type, const nlohmann::json& payload, const ActionContext& ctx);
    ActionResult validateAndExecute(const std::string& action_type, const nlohmann::json& payload, const ActionContext& ctx);
    
    std::vector<std::string> getRegisteredTypes() const;
    bool hasType(const std::string& action_type) const;
    const std::string& getDescription(const std::string& action_type) const;

private:
    ActionRegistry() = default;
    struct Handler {
        ValidateFn validate_fn;
        ExecuteFn execute_fn;
        std::string description;
    };
    std::unordered_map<std::string, Handler> handlers_;
};

struct ProductionUpdate {
    std::string resource_type;
    double amount_produced;
    std::string source_type;
    int source_id;
    int fiefdom_id;
};

struct TimeUpdateResult {
    Timestamp new_timestamp;
    double time_hours_elapsed;
    int production_updates_applied;
    std::vector<ProductionUpdate> productions;
    std::vector<std::pair<std::string, int>> completed_trainings;
    std::vector<std::pair<int, double>> morale_changes;
    int fiefdoms_updated;
};

TimeUpdateResult updateStateSince(
    Timestamp last_update_time,
    const std::string& fiefdom_filter_id = ""
);

} // namespace GameLogic