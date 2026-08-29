#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// A single building type as authored in fiefdom_building_types.json.
//
// Provides clean accessors for the fields the balance analyzer cares about:
// costs, production amounts (which may be plain numbers, per-level arrays, or
// money objects normalized to gold), stage-chain relationships, prerequisites,
// dependencies, and modifiers.
class building_type {
public:
    building_type() = default;
    explicit building_type(const std::string& id, const nlohmann::json& config);

    const std::string& id() const { return id_; }
    const std::string& display_name() const { return display_name_; }

    // Level-1 build costs by resource (gold, wood, stone, ..., charcoal,
    // iron, ironwork, fancy_ironwork). Missing costs default to 0.
    double cost(const std::string& resource) const;

    // Build cost of the given resource at the given target level (level-indexed
    // cost arrays; array index = level - 1, clamped). Used for manor upgrades.
    double cost_at(const std::string& resource, int level) const;

    // Total level-1 build cost normalized to gold using the given import prices.
    // Gold adds directly; silver_pence converts at 240 pence/gold; penny-market
    // resources (those with money-object import prices) convert through the
    // gold<->silver_pence rate.
    double gold_normalized_cost(const std::unordered_map<std::string, double>& import_prices_gold) const;

    // Returns the production amount for the given resource at the given level.
    // Mirrors the server's compute_amount: plain number, {amount}, per-level
    // array (linear extrapolation), or money object normalized to gold.
    double production(const std::string& resource, int level) const;

    // Resources this building produces (top-level "<resource>": {amount} plus
    // any "outputs" array entries), at the given level.
    std::vector<std::pair<std::string, double>> outputs_at(int level) const;

    // Daily upkeep cost (gold-normalized) from "daily_cost".
    std::unordered_map<std::string, double> daily_cost() const;

    // Inputs this building consumes at the given level: the top-level "inputs"
    // map plus each "outputs" entry's inputs (all resolved at `level`).
    std::unordered_map<std::string, double> inputs_at(int level) const;

    // Construction time in seconds at the given level (construction_times
    // array; missing entries fall back to the last authored value).
    double construction_time(int level) const;

    int max_level() const { return max_level_; }
    int manor_level_requirement() const;
    std::optional<std::string> built_from() const { return built_from_; }
    bool is_water_powered() const { return is_water_powered_; }
    bool has_max_per_fiefdom() const { return max_per_fiefdom_.has_value(); }
    int max_per_fiefdom() const { return max_per_fiefdom_.value_or(0); }

    // Arable acres this building claims (config `arable_acres`; default 0).
    int arable_acres() const { return arable_acres_; }

    // Forest acres this building claims (config `forest_acres`; default 0).
    // Only the wood producers (woodcutter chain) claim forest.
    int forest_acres() const { return forest_acres_; }

    // Class grouping (config `class`), a definitive grouping independent of the
    // built_from chain (e.g. "peasant" covers villein/freeholder/yeoman).
    const std::string& building_class() const { return class_; }

    // True if the building belongs to the given class.
    bool is_class(const std::string& cls) const { return !cls.empty() && class_ == cls; }

    // The immediate successor stage, if any (reverse of built_from lookup).
    std::optional<std::string> successor() const { return successor_; }

    const nlohmann::json& raw() const { return raw_; }

private:
    friend class building_registry;  // registry resolves successor in a second pass
    std::string id_;
    std::string display_name_;
    nlohmann::json raw_;
    int max_level_ = 1;
    std::optional<std::string> built_from_;
    std::optional<std::string> successor_;
    bool is_water_powered_ = false;
    std::optional<int> max_per_fiefdom_;
    int arable_acres_ = 0;
    int forest_acres_ = 0;
    std::string class_;
};

// A registry of all building types, with chain helpers.
//
// Stage chains are linked by "built_from": a successor is the next stage of a
// production line. Chain-aware counting means a building satisfies
// requirements for itself and every lower stage in its chain (a villein/yeoman
// counts as a peasant).
class building_registry {
public:
    // Parses the top-level array from fiefdom_building_types.json.
    void load(const nlohmann::json& types_array);

    const building_type* find(const std::string& id) const;
    const std::vector<building_type>& all() const { return buildings_; }
    bool empty() const { return buildings_.empty(); }

    // Builds the ordered chain from the lowest stage up to (and including) the
    // given stage. Returns {stage1, stage2, ..., id}.
    std::vector<std::string> chain_for(const std::string& id) const;

    // True if `higher` is id, or is a descendant in the same chain as `id`
    // (i.e. `higher` counts as `id` for requirement purposes).
    bool satisfies(const std::string& higher, const std::string& lower) const;

private:
    std::vector<building_type> buildings_;
    std::unordered_map<std::string, size_t> index_;
};

// Converts a per-level value (number or array) to the value at a 1-based level,
// linearly extrapolating past the end of the array, mirroring the server's
// getIntForLevel behavior (but returning a double).
double amount_at_level(const nlohmann::json& amount, int level);
