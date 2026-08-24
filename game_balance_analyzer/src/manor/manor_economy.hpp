#pragma once

#include "manor/manor_model.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// A single building instance within a simulated fiefdom.
struct building_instance {
    std::string type_id;
    int level = 1;
};

// Resources tracked by the economy simulation. The value at each key is a
// quantity (not gold). silver_pence is the penny-market currency; gold is the
// primary currency.
class resource_state {
public:
    resource_state() = default;

    double get(const std::string& resource) const;
    void set(const std::string& resource, double value);
    void add(const std::string& resource, double delta);

    double gold() const { return get("gold"); }
    double silver_pence() const { return get("silver_pence"); }

    // Total liquid wealth in gold: gold plus silver_pence converted at 240
    // pence/gold.
    double total_gold_equivalent() const;

    const std::unordered_map<std::string, double>& raw() const { return data_; }

private:
    std::unordered_map<std::string, double> data_;
};

// Result of running one simulated day of fiefdom economy.
struct economy_day_result {
    resource_state net;                 // Net change in each resource that day.
    std::unordered_map<std::string, double> produced;  // Gross production.
    std::unordered_map<std::string, double> consumed;  // Gross consumption.
    std::unordered_map<std::string, double> imported;  // Imports to cover deficits.
    std::unordered_map<std::string, double> unmet;     // Needs still unmet after imports.
    std::unordered_map<std::string, double> exported;  // Auto-sold excess.
    std::unordered_map<std::string, double> sale_gold; // Gold earned from exports.
};

// An independent re-implementation of the manor economy tick, mirroring the
// rules in the server's game_logic updateStateSince:
//   - Buildings produce outputs and consume inputs; production is gated by the
//     ratio of available inputs to required inputs.
//   - daily_cost is consumed unconditionally.
//   - Modifiers (targeting a building/resource, chain-aware) multiply output.
//   - Deficits below the reserve are imported at the resource's import price;
//     excess above the reserve is auto-sold at the export price.
//   - Water-powered buildings produce nothing unless powered (simplified: an
//     optional powered flag on the instance).
class manor_economy {
public:
    manor_economy(const building_registry& registry,
                  const nlohmann::json& economy_config);

    // Runs one simulated day starting from the given resource state.
    // The instance list is interpreted as current buildings at their levels.
    economy_day_result run_day(resource_state& state,
                               const std::vector<building_instance>& buildings,
                               int manor_level) const;

    // Import price of a resource in gold (number or money object normalized).
    double import_price_gold(const std::string& resource) const;

    // Export price of a resource in gold, resolved by precedence:
    //   export_prices[res] -> export_sell_multipliers[res] * import ->
    //   export_sell_multiplier * import.
    double export_price_gold(const std::string& resource) const;

    // Per-resource reserve (minimum stockpile) used for export gating.
    double reserve(const std::string& resource) const;

    // True if the resource trades in the penny market (money-object import
    // price), i.e. imports pay silver_pence and exports credit silver_pence.
    bool is_penny_market(const std::string& resource) const;

    // Total arable acres available at a given manor (home-base) level, from
    // economy.json's "arable_land_by_level" array (index 0-10, clamped).
    double arable_land_for_level(int manor_level) const;

    // Total forest acres available at a given manor level, from economy.json's
    // "forest_land_by_level" array (200 at level 1 -> 600 at level 10).
    double forest_land_for_level(int manor_level) const;

private:
    const building_registry& registry_;
    nlohmann::json economy_;
};
