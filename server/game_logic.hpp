#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <nlohmann/json.hpp>
#include "Database.hpp"

class GameConfigCache;
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
    GameConfigCache* config_cache = nullptr;
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

struct FailedUpgrade {
    int building_id;
    int attempted_level;
    std::string reason;
    nlohmann::json refund;
};

struct TimeUpdateResult {
    Timestamp new_timestamp;
    double time_hours_elapsed;
    int production_updates_applied;
    std::vector<ProductionUpdate> productions;
    std::vector<std::pair<std::string, int>> completed_trainings;
    std::vector<std::pair<int, double>> morale_changes;
    std::vector<FailedUpgrade> failed_upgrades;
    int fiefdoms_updated;
    /// Per-fiefdom economy ledger (produced, consumed, imported, exported,
    /// net_gold, recommendations) keyed by fiefdom_id. Populated for each
    /// fiefdom processed during the update.
    std::unordered_map<int, nlohmann::json> economy_reports;
};

TimeUpdateResult updateStateSince(
    GameConfigCache& config_cache,
    Timestamp last_update_time,
    const std::string& fiefdom_filter_id = ""
);

/// A single produced output of a building and its own input requirements.
struct OutputPlan {
    std::string resource;                       // produced resource
    double amount = 0.0;                        // this tick: amount/day × days × modifier × rate
    std::map<std::string, double> inputs;       // this tick: required per-day × days × rate
    std::map<std::string, double> supplied;     // actually supplied (stock + imports) this tick
    double rate = 1.0;                          // player-controlled utilization 0..1
};

/// Per-building production/input plan used by the economy engine.
/// Each output has its own inputs and is gated by its own satisfaction ratio.
struct BuildingPlan {
    std::vector<OutputPlan> outputs;
};

/// Builds economic recommendation strings from the per-fiefdom ledger and
/// building configs. Returns a JSON array of human-readable suggestions.
nlohmann::json build_economy_recommendations(
    GameConfigCache& config_cache,
    const nlohmann::json& produced,
    const nlohmann::json& consumed,
    const nlohmann::json& imported,
    const nlohmann::json& exported,
    const std::map<int, BuildingPlan>& plans,
    const nlohmann::json& building_types
);

/// Helper: index into a level-indexed JSON array with linear extrapolation.
/// arr can be a number (returns as-is) or an array. level is 0-indexed.
int getIntForLevel(const nlohmann::json& arr, int level, int default_val);
double getDoubleForLevel(const nlohmann::json& arr, int level, double default_val);

/// Result type for computeBuildingModifiers: target_building_id -> (resource -> combined_multiplier)
using ModifierMap = std::unordered_map<int, std::unordered_map<std::string, double>>;

/// Computes building-to-building modifier assignments for a fiefdom.
/// Returns a map of (target_building_id) -> (resource -> total_multiplier).
/// Modifiers are recomputed every cycle — no persistence needed.
ModifierMap computeBuildingModifiers(
    const std::vector<BuildingData>& buildings,
    const nlohmann::json& building_types
);

} // namespace GameLogic