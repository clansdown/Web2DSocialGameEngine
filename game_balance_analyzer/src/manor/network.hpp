#pragma once

#include "config_loader.hpp"
#include "manor/manor_economy.hpp"
#include "manor/manor_model.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// Production-network ratio analysis of the manor economy.
//
// For each stage of the building chains (independently), the module computes
// the minimal self-sufficient set of building counts (the "balance solve"),
// then scales it up by increasing the count of the least-numerous building
// (the "anchor"). At each scale it reports:
//   - absolute counts and per-anchor ratios,
//   - net resources/day (the exportable surplus after satisfying all internal
//     consumption),
//   - net gold/day (exports at export price + direct gold output),
//   - total build cost, and
//   - a parallel-aware build-time estimate (construction slots + gold gating).
//
// The balance model coordinates inputs and outputs across buildings: every
// internally-producible resource must be in surplus after all downstream
// consumption. Modifier buildings (wood_hewer/sawyer/miller) are part of the
// set and use an "ideal-targets" assumption: each modifier source boosts up to
// max_targets target instances, concentrated on the targets that benefit most.
class manor_network_analyzer;

// Daily flows of one building type at a level: gross outputs (after modifier
// effects), input requirements, and unconditional daily upkeep.
struct building_flows {
    std::string type_id;
    int level = 1;
    std::unordered_map<std::string, double> outputs;
    std::unordered_map<std::string, double> inputs;
    std::unordered_map<std::string, double> daily_cost;
};

// One balance solution at a specific anchor multiplicity.
struct network_scale {
    int k = 1;                                        // anchor multiplicity
    std::string anchor_id;                            // least-numerous type
    std::vector<std::pair<std::string, int>> counts;  // type -> absolute count
    std::unordered_map<std::string, double> net_resources_per_day;
    double net_gold_per_day = 0.0;      // exports + gold outputs, in gold
    double net_silver_per_day = 0.0;    // penny-market export income, in pence
    double arable_acres = 0.0;          // arable acres used by the scaled set
    double arable_total = 0.0;          // max arable acres (manor level 10)
    double forest_acres = 0.0;          // forest acres used by the scaled set
    double forest_total = 0.0;          // max forest acres (manor level 10)
    std::unordered_map<std::string, double> build_cost;  // per-resource totals
    double build_days = 0.0;            // parallel-aware build-time estimate
    double cumulative_gold_spent = 0.0;
    std::vector<std::string> warnings;
};

// One stage's full analysis (scales k = 1..config.scales).
struct network_stage {
    int stage = 1;
    std::vector<std::string> set_ids;  // building types in the set
    std::vector<network_scale> scales;
    std::vector<std::string> warnings;  // set-level warnings (e.g. solver)
};

// Options for the network analyzer.
struct network_config {
    int scales = 5;             // number of anchor-multiplicity iterations
    int build_slots = 3;        // concurrent construction slots
    double day_seconds = 60.0;  // construction seconds per simulated day
    int max_stages = 0;         // analyze only stages 1..max_stages (0 = all)
};

class manor_network_analyzer {
public:
    explicit manor_network_analyzer(const config_loader& loader,
                                    network_config config = {});

    // Runs the full per-stage analysis.
    std::vector<network_stage> analyze() const;

    std::string render(const std::vector<network_stage>& stages) const;
    nlohmann::json render_json(const std::vector<network_stage>& stages) const;

private:
    // Ordered chain list (root .. top) for each production chain.
    std::vector<std::vector<std::string>> collect_chains() const;

    // The stage-s building set: stage-s building of every chain (top stage if
    // the chain is shorter) plus home_base.
    std::vector<std::string> stage_set(int s) const;

    // All resources the set touches (outputs, inputs, daily_cost), excluding
    // silver_pence. Used for balance solving and net reports.
    std::vector<std::string> all_resources(
        const std::vector<std::string>& set_ids) const;

    // Per-building flows with modifier effects, given current counts.
    building_flows flows_for(const building_type& b, int level,
                             const std::vector<std::pair<std::string, int>>& counts) const;

    // Net per-resource production across the whole set (negative = deficit).
    std::unordered_map<std::string, double> compute_nets(
        const std::vector<std::pair<std::string, int>>& counts,
        const std::vector<std::string>& resources) const;

    // Minimal balanced integer counts for the set (anchor normalized to 1).
    std::vector<std::pair<std::string, int>> solve_balance(
        const std::vector<std::string>& set_ids,
        const std::vector<std::string>& resources,
        std::vector<std::string>& warnings) const;

    // Chooses the scaling anchor from the balanced counts: prefers the
    // "final-goods" producer (whose outputs are not consumed by other set
    // types), maximizing its per-instance output value; falls back to the
    // least-numerous type. home_base is never the anchor.
    std::string choose_anchor(
        const std::vector<std::pair<std::string, int>>& counts,
        const std::vector<std::string>& set_ids) const;

    // Builds the scales for one stage from the balanced base counts.
    network_stage analyze_stage(int s) const;

    // Parallel-aware build-out simulation for a target count vector.
    struct build_result {
        double days = 0.0;
        double gold_spent = 0.0;
        bool converged = false;
    };
    build_result simulate_build(
        const std::vector<std::pair<std::string, int>>& counts) const;

    const config_loader& loader_;
    building_registry registry_;
    manor_economy economy_;
    network_config config_;
};
