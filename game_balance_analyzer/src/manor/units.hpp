#pragma once

#include "config_loader.hpp"
#include "manor/manor_model.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Static "units" analysis of the manor economy.
//
// For every building type at every level it computes the net production value
// in gold and silver pence, assuming all inputs and maintenance costs are
// imported (nothing is supplied locally). Outputs are valued at export price
// (the cash realized when selling surplus); inputs and maintenance are valued
// at import price. Peasant households are a building class, not a tracked
// population resource, so no population is modeled.
//
// The inputs-vs-costs distinction mirrors the server's produce-first tick:
//   - Production is calculated before daily costs, so a building's own output
//     first covers its own daily cost (that portion nets to zero); only the
//     surplus is saleable income and only the uncovered daily cost is paid from
//     outside.
//   - Inputs gate production (importing them enables full output) and are
//     valued at import price.
// Build cost is reported separately with a payback period.

// Per-level net-production snapshot for one building type.
struct unit_row {
    int level = 1;
    double gross_gold = 0.0;      // saleable surplus at export price (gold-market)
    double gross_silver = 0.0;    // saleable surplus at export price (penny-market, pence)
    double input_gold = 0.0;      // production inputs at import price (gold)
    double input_silver = 0.0;    // production inputs at import price (pence)
    double cost_gold = 0.0;       // daily cost not covered by own production, at import (gold)
    double cost_silver = 0.0;     // daily cost not covered by own production, at import (pence)
    double net_gold = 0.0;        // gross - input - cost (gold)
    double net_silver = 0.0;      // gross - input - cost (pence)
    double build_cost_gold = 0.0; // cumulative gold-normalized build cost to this level
    double payback_days = -1.0;   // build_cost / net(gold equiv); -1 = never (net <= 0)
};

// One building type and its per-level rows.
struct unit_building {
    std::string building_id;
    std::string display_name;
    bool produces = false;  // true if any level has a nonzero production output
    std::vector<unit_row> levels;
};

class manor_units_analyzer {
public:
    explicit manor_units_analyzer(const config_loader& loader);

    // Computes the per-building, per-level units table.
    std::vector<unit_building> analyze() const;

    std::string render(const std::vector<unit_building>& buildings) const;
    nlohmann::json render_json(const std::vector<unit_building>& buildings) const;

private:
    // Gold value of one unit of `res` at import price (0 if not priced).
    double import_price_gold(const std::string& res) const;

    // Gold value of one unit of `res` at export price, resolved by precedence:
    // export_prices[res] -> export_sell_multipliers[res] * import ->
    // export_sell_multiplier * import.
    double export_price_gold(const std::string& res) const;

    // True if `res` trades in the penny market (money-object import price).
    bool is_penny_market(const std::string& res) const;

    // Splits `amount` of `res` at a unit price (in gold) into {gold, silver}.
    // Penny-market resources credit silver_pence; gold-market credit gold.
    std::pair<double, double> split_value(const std::string& res, double amount,
                                          double price_gold) const;

    // Cumulative gold-normalized build cost to reach `level` of `b`.
    double cumulative_build_cost(const building_type& b, int level) const;

    const config_loader& loader_;
    building_registry registry_;
    nlohmann::json economy_;
    std::unordered_map<std::string, double> import_prices_gold_;
};
