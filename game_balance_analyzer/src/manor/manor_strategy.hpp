#pragma once

#include "config_loader.hpp"
#include "manor/manor_economy.hpp"
#include "manor/manor_model.hpp"
#include "statistics.hpp"

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "pcg_random.hpp"

// Configuration for the manor strategy simulation.
struct manor_sim_config {
    int days = 30;               // Simulation horizon in days.
    int iterations = 2000;       // Repeats per heuristic AND Monte-Carlo sample count.
    bool quiet = false;          // Suppress in-place progress feedback on stderr.
    bool record_top_trace = false;  // Record per-day snapshots of the top run.
    bool color = false;          // Emit ANSI row striping in the trace (set from stdout TTY).
    // Optional RNG seed. When nullopt the sim seeds from std::random_device
    // (non-reproducible); set it to replay a specific run. The effective seed
    // actually used is reported by manor_strategy_sim::seed_used().
    std::optional<std::uint64_t> seed;
};

// A per-day snapshot of one simulated run: building counts, resource balances,
// and the day's net change / unmet needs, captured at the end of a day (after
// building + production).
struct day_snapshot {
    int day = 0;
    int manor_level = 0;
    std::unordered_map<std::string, double> resources;
    std::unordered_map<std::string, double> net;    // net production (produced − consumed; imports count as consumption); gold combines gold+silver_pence as decimal gold
    std::unordered_map<std::string, double> unmet;  // needs unmet after imports
    std::unordered_map<std::string, int> buildings; // type_id -> count; NB: "home_base" holds the manor LEVEL (it's always a single instance, so a count would be meaningless)
};

// The retained day-by-day trace of the highest-scoring run so far (a running
// maximum: only the best run's trace is kept, never all runs).
struct top_run_trace {
    std::string label;
    double score = 0.0;
    std::vector<day_snapshot> days;
    bool empty() const { return days.empty(); }
};

// One weighted heuristic strategy, loaded from analyzer_manor_strategies.json.
//
// weights: building_id -> relative weight. At each build decision the policy
//   picks among currently-feasible buildings proportional to these weights.
// min_ratios: A -> { B: n } means "don't build another A until
//   count(A) * n <= count(B)" (e.g. blacksmith -> {peasant: 5} means one
//   blacksmith needs five peasants before a second blacksmith is allowed).
struct manor_heuristic {
    std::string id;
    std::unordered_map<std::string, double> weights;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> min_ratios;
};

// A single simulated run outcome.
struct manor_sim_outcome {
    std::string label;        // Strategy label.
    double score = 0.0;       // Final net worth in gold equivalent.
    double net_gold = 0.0;    // Final gold.
    double net_silver = 0.0;  // Final silver pence.
    std::vector<building_instance> buildings;  // Buildings at end of run.
};

// Runs the manor strategy simulation: a set of weighted heuristic strategies
// (each repeated `iterations` times for statistics) plus a feasibility-gated
// Monte-Carlo search. Every strategy is driven by the same adaptive policy:
// each day it builds all currently-feasible buildings, choosing among them by
// weight (heuristics) or uniformly (Monte-Carlo).
class manor_strategy_sim {
public:
    manor_strategy_sim(const config_loader& loader, manor_sim_config config);

    // Runs every heuristic (repeated `iterations` times) plus Monte-Carlo.
    std::vector<manor_sim_outcome> run_all();

    // Runs each heuristic strategy `iterations` times; outcomes are labeled
    // "<id>_<run>".
    std::vector<manor_sim_outcome> run_heuristic();

    // Runs the feasibility-gated Monte-Carlo random search (`iterations` runs).
    std::vector<manor_sim_outcome> run_monte_carlo();

    // Builds the optimality distribution from a set of outcomes.
    static optimality_distribution distribution(
        const std::vector<manor_sim_outcome>& outcomes);

    // Renders results as text.
    std::string render(const std::vector<manor_sim_outcome>& outcomes) const;

    // Renders results as JSON.
    nlohmann::json render_json(const std::vector<manor_sim_outcome>& outcomes) const;

    // The RNG seed actually used for this simulation (either the configured
    // seed or the random one drawn at construction). Pass it back via
    // manor_sim_config::seed to reproduce an exact run.
    std::uint64_t seed_used() const { return effective_seed_; }

    // The day-by-day trace of the highest-scoring run, populated only when
    // manor_sim_config::record_top_trace is set. Empty otherwise.
    const top_run_trace& top_trace() const { return best_trace_; }

private:
    // Evaluates one strategy defined by a policy. Two mutually exclusive modes:
    //   - heuristic: a config-authored policy (per-building weights + optional
    //     min-ratio constraints); heuristic must be non-null and mc_weights null.
    //   - monte-carlo: a per-run randomized weight vector over the candidate
    //     buildings; mc_weights must be non-null and heuristic null. The weights
    //     are re-normalized over the feasible subset at each build decision.
    // The manor house is always present.
    manor_sim_outcome evaluate(const std::string& label,
                               const manor_heuristic* heuristic,
                               const std::unordered_map<std::string, double>* mc_weights) const;

    // True if the building's level-1 build cost is coverable from current
    // stock plus imports affordable with current gold/silver_pence, its
    // max_per_fiefdom cap is not yet reached, its manor_level requirement is
    // met, and it fits within the manor's remaining arable/forest acres.
    bool is_feasible(const building_type& b,
                     const resource_state& state,
                     const std::vector<building_instance>& buildings,
                     int manor_level) const;

    // True if the given level-indexed costs can be covered from stock plus
    // imports affordable with the current fungible gold/silver_pence wallet.
    bool is_affordable(const std::vector<std::pair<std::string, double>>& costs,
                       const resource_state& state) const;

    // True if the manor house can be upgraded (below max_level and affordable).
    bool is_manor_upgrade_feasible(const resource_state& state,
                                   const std::vector<building_instance>& buildings) const;

    // True if building A with ratio constraint A->{B: n} may be built given
    // the current counts. If n==0 or B absent, no constraint applies.
    bool ratio_allows(const std::string& type_id,
                      const manor_heuristic& h,
                      const std::vector<building_instance>& buildings) const;

    // Commits the build: spends stock for each cost resource and imports the
    // shortfalls, deducting gold/silver_pence accordingly.
    void commit_build(const building_type& b,
                      resource_state& state) const;

    // Spends the given costs from stock, importing shortfalls with the
    // fungible gold/silver_pence wallet (shared by buildings and the upgrade).
    void spend_costs(const std::vector<std::pair<std::string, double>>& costs,
                     resource_state& state) const;

    // The synthetic action id for upgrading the manor house, offered in the
    // build-decision pool alongside the candidate buildings.
    static constexpr const char* kManorUpgradeAction = "upgrade_manor";

    // Helper: current count of a building type in the fiefdom.
    static int count_of(const std::string& type_id,
                        const std::vector<building_instance>& buildings);

    // Prints an in-place progress line ("\r\x1b[2K" + label + done/total) to
    // stderr, unless config_.quiet. Callers print a trailing newline when a
    // phase completes.
    void print_progress(const std::string& label, int done, int total) const;

    // RNG: PCG64 (pcg-cpp, M.E. O'Neill). Deterministic for a fixed seed.
    mutable pcg64 rng_;

    // Running maximum: the day-by-day trace of the highest-scoring run so far.
    // Mutable because evaluate() is const (same pattern as rng_).
    mutable top_run_trace best_trace_;

    // Seed actually used; set in the constructor.
    std::uint64_t effective_seed_ = 0;

    std::vector<manor_heuristic> heuristics_;

    const config_loader& loader_;
    building_registry registry_;
    manor_economy economy_;

    // Starting resource balances for a new fiefdom, loaded from economy.json's
    // "starting_resources" block (single source of truth shared with the
    // server). Resources not listed default to 0.
    std::unordered_map<std::string, double> starting_resources_;

    manor_sim_config config_;
};
