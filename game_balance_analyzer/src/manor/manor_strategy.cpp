#include "manor/manor_strategy.hpp"
#include "fmt.hpp"

#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>

using json = nlohmann::json;

namespace {

constexpr double kPencePerGold = 240.0;

// Compact double formatting (comma-grouped, never scientific).
std::string fmt(double v) { return nfmt::format_number(v); }

// Returns an ANSI escape sequence that paints a background for alternating row
// striping, plus the trailing reset. When color is disabled (non-TTY stdout) it
// returns an empty prefix so the reset is also empty. Day 1 -> black, day 2 ->
// dark-grey, alternating; a full row is wrapped in a single prefix+reset pair.
std::string stripe_prefix(bool color, int day) {
    if (!color) {
        return "";
    }
    return (day % 2 == 1) ? "\x1b[40m" : "\x1b[100m";
}

// The reset sequence paired with stripe_prefix(); empty when color is off.
std::string stripe_reset(bool color) {
    return color ? "\x1b[0m" : "";
}

// Max entries per physical line in the day-by-day trace (buildings and
// resources wrap when they exceed this).
constexpr size_t kTracePerLine = 6;

// Writes a space-separated list of pre-formatted "name=value" entries, wrapping
// at `per_line` entries per physical line. `prefix` is the visible text written
// before the first entry (e.g. "  day 1  buildings:"); continuation lines are
// indented to align their entries under the first line's entries. `pre` (ANSI
// row-stripe background, possibly empty) is emitted at the start of every
// physical line and `reset` once after the final entry, so the day's background
// spans wrapped lines. An empty list emits just prefix (with pre/reset).
void write_wrapped_section(std::ostringstream& out,
                           const std::vector<std::string>& entries,
                           size_t per_line,
                           const std::string& prefix,
                           const std::string& pre,
                           const std::string& reset) {
    if (entries.empty()) {
        out << pre << prefix << reset;
        return;
    }
    std::string indent(prefix.size(), ' ');
    out << pre << prefix;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0 && i % per_line == 0) {
            out << "\n" << pre << indent;
        }
        out << " " << entries[i];
    }
    out << reset;
}

// Resource keys that count as build-cost resources (level-1 costs, importable).
const std::vector<std::string>& build_cost_resources() {
    static const std::vector<std::string> res = {
        "gold", "silver_pence", "wood", "steel", "bronze", "grain",
        "leather", "mana", "charcoal", "iron", "ironwork", "fancy_ironwork",
        "beams", "boards",
    };
    return res;
}

// Infrastructure / non-production buildings that are never built by a policy.
bool is_infrastructure(const std::string& id) {
    static const std::set<std::string> infra = {
        "road", "head_race", "tail_race", "mill_pond",
        "chapel", "church", "parish_church",
    };
    return infra.count(id) != 0;
}

}  // namespace

manor_strategy_sim::manor_strategy_sim(const config_loader& loader, manor_sim_config config)
    : loader_(loader),
      economy_(registry_, loader.economy()),
      config_(config) {
    registry_.load(loader_.building_types());

    // Load the starting resource balances from economy.json's
    // "starting_resources" block. Resources not listed default to 0 (matching
    // the server's fiefdom creation).
    const json economy_json = loader_.economy();
    if (economy_json.contains("starting_resources") && economy_json["starting_resources"].is_object()) {
        for (auto it = economy_json["starting_resources"].begin();
             it != economy_json["starting_resources"].end(); ++it) {
            if (it.value().is_number()) {
                starting_resources_[it.key()] = it.value().get<double>();
            }
        }
    }

    // Determine the effective seed: the configured one if given, otherwise a
    // fresh draw from std::random_device (combined into a 64-bit value).
    if (config_.seed) {
        effective_seed_ = *config_.seed;
    } else {
        std::random_device rd;
        effective_seed_ = (std::uint64_t(rd()) << 32) ^ std::uint64_t(rd());
    }

    // Seed the PCG64 engine deterministically from the effective seed so a run
    // can be reproduced by passing the same value back via manor_sim_config.
    std::seed_seq seed_seq{
        static_cast<std::uint32_t>(effective_seed_ >> 32),
        static_cast<std::uint32_t>(effective_seed_ & 0xffffffffULL),
    };
    rng_.seed(seed_seq);

    // Load heuristic strategies from analyzer_manor_strategies.json.
    const json strategies = loader_.manor_strategies();
    if (!strategies.contains("heuristics") || !strategies["heuristics"].is_array()) {
        return;
    }
    for (const auto& h : strategies["heuristics"]) {
        if (!h.is_object()) {
            continue;
        }
        manor_heuristic mh;
        mh.id = h.value("id", "");
        if (mh.id.empty()) {
            continue;
        }
        if (h.contains("weights") && h["weights"].is_object()) {
            for (auto it = h["weights"].begin(); it != h["weights"].end(); ++it) {
                mh.weights[it.key()] = it.value().get<double>();
            }
        }
        if (h.contains("min_ratios") && h["min_ratios"].is_object()) {
            for (auto a = h["min_ratios"].begin(); a != h["min_ratios"].end(); ++a) {
                if (!a.value().is_object()) {
                    continue;
                }
                for (auto b = a.value().begin(); b != a.value().end(); ++b) {
                    mh.min_ratios[a.key()][b.key()] = b.value().get<int>();
                }
            }
        }
        heuristics_.push_back(std::move(mh));
    }
}

int manor_strategy_sim::count_of(const std::string& type_id,
                                 const std::vector<building_instance>& buildings) {
    int count = 0;
    for (const auto& inst : buildings) {
        if (inst.type_id == type_id) {
            ++count;
        }
    }
    return count;
}

bool manor_strategy_sim::ratio_allows(const std::string& type_id,
                                      const manor_heuristic& h,
                                      const std::vector<building_instance>& buildings) const {
    auto it = h.min_ratios.find(type_id);
    if (it == h.min_ratios.end()) {
        return true;
    }
    int have = count_of(type_id, buildings);
    for (const auto& [other, n] : it->second) {
        if (n <= 0) {
            continue;
        }
        // Building another `type_id` is only allowed if the ratio is preserved:
        // (have + 1) / count(other) <= 1 / n, i.e. (have + 1) * n <= count(other).
        int other_count = count_of(other, buildings);
        if ((have + 1) * n > other_count) {
            return false;
        }
    }
    return true;
}

bool manor_strategy_sim::is_feasible(const building_type& b,
                                     const resource_state& state,
                                     const std::vector<building_instance>& buildings,
                                     int manor_level) const {
    // max_per_fiefdom cap.
    if (b.has_max_per_fiefdom() && count_of(b.id(), buildings) >= b.max_per_fiefdom()) {
        return false;
    }

    // Manor-level requirement (stage 2 -> 3, stage 3 -> 6, etc.).
    if (b.manor_level_requirement() > manor_level) {
        return false;
    }

    // Arable land: the building must fit within the manor's remaining acres,
    // scaled by the (possibly upgraded) manor level.
    if (b.arable_acres() > 0) {
        int used = 0;
        for (const auto& inst : buildings) {
            const building_type* eb = registry_.find(inst.type_id);
            if (eb) used += eb->arable_acres();
        }
        double total = economy_.arable_land_for_level(manor_level);
        if (used + b.arable_acres() > total + 1e-9) {
            return false;
        }
    }

    // Forest land: the wood producers claim off-map forest acres, scaled by
    // the manor level exactly like arable land.
    if (b.forest_acres() > 0) {
        int used = 0;
        for (const auto& inst : buildings) {
            const building_type* eb = registry_.find(inst.type_id);
            if (eb) used += eb->forest_acres();
        }
        double total = economy_.forest_land_for_level(manor_level);
        if (used + b.forest_acres() > total + 1e-9) {
            return false;
        }
    }

    // Import-aware affordability.
    std::vector<std::pair<std::string, double>> costs;
    for (const std::string& res : build_cost_resources()) {
        double c = b.cost_at(res, 1);
        if (c > 0.0) costs.emplace_back(res, c);
    }
    return is_affordable(costs, state);
}

bool manor_strategy_sim::is_affordable(
    const std::vector<std::pair<std::string, double>>& costs,
    const resource_state& state) const {
    double gold_demand = 0.0;
    double silver_demand = 0.0;
    for (const auto& [res, cost] : costs) {
        if (cost <= 0.0) {
            continue;
        }
        if (res == "gold") {
            gold_demand += cost;
            continue;
        }
        if (res == "silver_pence") {
            silver_demand += cost;
            continue;
        }
        double in_stock = state.get(res);
        double shortfall = cost - in_stock;
        if (shortfall <= 0.0) {
            continue;
        }
        if (economy_.is_penny_market(res)) {
            double pence_per_unit = economy_.import_price_gold(res) * kPencePerGold;
            silver_demand += shortfall * pence_per_unit;
        } else {
            gold_demand += shortfall * economy_.import_price_gold(res);
        }
    }
    // Money is fungible: gold and silver_pence are one wallet at the standard
    // rate (kPencePerGold pence/gold). Affordable iff the total demand in
    // gold-equivalent fits within the total liquid wealth.
    double total_gold_demand = gold_demand + silver_demand / kPencePerGold;
    return total_gold_demand <= state.total_gold_equivalent() + 1e-9;
}

bool manor_strategy_sim::is_manor_upgrade_feasible(
    const resource_state& state,
    const std::vector<building_instance>& buildings) const {
    const building_type* manor = registry_.find("home_base");
    if (!manor) {
        return false;
    }
    int manor_level = 1;
    for (const auto& inst : buildings) {
        if (inst.type_id == "home_base") {
            manor_level = inst.level;
            break;
        }
    }
    if (manor_level >= manor->max_level()) {
        return false;
    }
    std::vector<std::pair<std::string, double>> costs;
    for (const std::string& res : build_cost_resources()) {
        double c = manor->cost_at(res, manor_level + 1);
        if (c > 0.0) costs.emplace_back(res, c);
    }
    return is_affordable(costs, state);
}

void manor_strategy_sim::spend_costs(
    const std::vector<std::pair<std::string, double>>& costs,
    resource_state& state) const {
    // Money is fungible: gold and silver_pence are one wallet at the standard
    // rate (kPencePerGold pence/gold). These helpers pay a cost in the given
    // currency, converting from the other on shortfall.
    auto spend_gold = [&](double gold_cost) {
        if (gold_cost <= 0.0) return;
        if (state.gold() >= gold_cost) {
            state.add("gold", -gold_cost);
            return;
        }
        double shortfall = gold_cost - state.gold();
        state.set("gold", 0.0);
        state.add("silver_pence", -shortfall * kPencePerGold);
    };
    auto spend_silver = [&](double pence_cost) {
        if (pence_cost <= 0.0) return;
        if (state.silver_pence() >= pence_cost) {
            state.add("silver_pence", -pence_cost);
            return;
        }
        double shortfall = pence_cost - state.silver_pence();
        state.set("silver_pence", 0.0);
        state.add("gold", -shortfall / kPencePerGold);
    };

    for (const auto& [res, cost] : costs) {
        if (cost <= 0.0) {
            continue;
        }
        if (res == "gold") {
            spend_gold(cost);
            continue;
        }
        if (res == "silver_pence") {
            spend_silver(cost);
            continue;
        }
        // Spend available stock, then import the shortfall.
        double in_stock = state.get(res);
        double from_stock = std::min(in_stock, cost);
        state.add(res, -from_stock);
        double shortfall = cost - from_stock;
        if (shortfall <= 0.0) {
            continue;
        }
        if (economy_.is_penny_market(res)) {
            double pence_per_unit = economy_.import_price_gold(res) * kPencePerGold;
            spend_silver(shortfall * pence_per_unit);
        } else {
            spend_gold(shortfall * economy_.import_price_gold(res));
        }
    }
}

void manor_strategy_sim::commit_build(const building_type& b,
                                      resource_state& state) const {
    std::vector<std::pair<std::string, double>> costs;
    for (const std::string& res : build_cost_resources()) {
        double c = b.cost(res);
        if (c > 0.0) costs.emplace_back(res, c);
    }
    spend_costs(costs, state);
}

manor_sim_outcome manor_strategy_sim::evaluate(const std::string& label,
                                               const manor_heuristic* heuristic,
                                               const std::unordered_map<std::string, double>* mc_weights) const {
    resource_state state;
    for (const auto& [res, val] : starting_resources_) {
        state.set(res, val);
    }

    std::vector<building_instance> buildings;
    buildings.push_back({"home_base", 1});

    int manor_level = 1;

    // Optional per-day snapshots of this run, retained only if it becomes the
    // highest-scoring run so far (running maximum).
    std::vector<day_snapshot> trace;
    if (config_.record_top_trace) {
        trace.reserve(static_cast<size_t>(config_.days));
    }

    // Candidate universe: all production (non-infrastructure, non-water-powered)
    // building types in the registry. The policy filters these by feasibility
    // and, for heuristics, by the heuristic's weights.
    std::vector<std::string> candidates;
    for (const auto& b : registry_.all()) {
        if (b.id() == "home_base" || is_infrastructure(b.id()) || b.is_water_powered()) {
            continue;
        }
        candidates.push_back(b.id());
    }

    for (int day = 0; day < config_.days; ++day) {
        // Build all feasible buildings chosen by the policy, until none remain.
        bool built_any = true;
        while (built_any) {
            built_any = false;
            // Gather feasible candidates.
            std::vector<std::string> feasible;
            std::vector<double> feasible_weights;
            double weight_total = 0.0;
            for (const auto& cid : candidates) {
                const building_type* b = registry_.find(cid);
                if (!b) {
                    continue;
                }
                if (!is_feasible(*b, state, buildings, manor_level)) {
                    continue;
                }
                // Ratio constraints (heuristics only; MC has none).
                if (heuristic && !ratio_allows(cid, *heuristic, buildings)) {
                    continue;
                }
                double w = 0.0;
                if (heuristic) {
                    auto it = heuristic->weights.find(cid);
                    if (it == heuristic->weights.end() || it->second <= 0.0) {
                        continue;  // not part of this heuristic's policy
                    }
                    w = it->second;
                } else {
                    // Monte-Carlo: use this run's randomized weights. The pick
                    // below re-normalizes over the feasible subset only.
                    auto it = mc_weights->find(cid);
                    if (it == mc_weights->end() || it->second <= 0.0) {
                        continue;
                    }
                    w = it->second;
                }
                feasible.push_back(cid);
                feasible_weights.push_back(w);
                weight_total += w;
            }
            // The manor upgrade is offered as an action alongside buildings:
            // the sim is willing to raise the manor level (unlocking more
            // arable/forest land and higher-stage buildings). Heuristics may
            // tune it via weights["upgrade_manor"] (default 1.0; 0 disables).
            // Monte-Carlo draws a per-run random weight like any building.
            if (is_manor_upgrade_feasible(state, buildings)) {
                double uw = 1.0;
                if (heuristic) {
                    auto it = heuristic->weights.find(kManorUpgradeAction);
                    if (it != heuristic->weights.end()) {
                        uw = it->second;
                    }
                } else if (mc_weights) {
                    auto it = mc_weights->find(kManorUpgradeAction);
                    if (it == mc_weights->end()) {
                        uw = 0.0;  // MC: drawn in run_monte_carlo; absent = off
                    } else {
                        uw = it->second;
                    }
                }
                if (uw > 0.0) {
                    feasible.push_back(kManorUpgradeAction);
                    feasible_weights.push_back(uw);
                    weight_total += uw;
                }
            }
            if (feasible.empty()) {
                break;
            }
            // Pick one feasible candidate by weight.
            std::string chosen;
            if (feasible.size() == 1) {
                chosen = feasible[0];
            } else {
                double roll = std::uniform_real_distribution<double>(0.0, weight_total)(rng_);
                double acc = 0.0;
                for (size_t i = 0; i < feasible.size(); ++i) {
                    acc += feasible_weights[i];
                    if (roll <= acc) {
                        chosen = feasible[i];
                        break;
                    }
                }
                if (chosen.empty()) {
                    chosen = feasible.back();
                }
            }
            if (chosen == kManorUpgradeAction) {
                // Upgrade the manor house to the next level: pay the level cost
                // (index = current level) and advance the home_base instance.
                const building_type* manor = registry_.find("home_base");
                if (!manor) {
                    break;
                }
                std::vector<std::pair<std::string, double>> costs;
                for (const std::string& res : build_cost_resources()) {
                    double c = manor->cost_at(res, manor_level + 1);
                    if (c > 0.0) costs.emplace_back(res, c);
                }
                spend_costs(costs, state);
                for (auto& inst : buildings) {
                    if (inst.type_id == "home_base") {
                        inst.level += 1;
                        manor_level = inst.level;
                        break;
                    }
                }
                built_any = true;
            } else {
                const building_type* b = registry_.find(chosen);
                if (!b) {
                    break;
                }
                commit_build(*b, state);
                buildings.push_back({chosen, 1});
                built_any = true;
            }
        }

        // Run one simulated day of production.
        auto day_result = economy_.run_day(state, buildings, manor_level);

        // Capture an end-of-day snapshot when tracing the top run.
        if (config_.record_top_trace) {
            day_snapshot snap;
            snap.day = day + 1;
            snap.manor_level = manor_level;
            for (const auto& [res, val] : state.raw()) {
                snap.resources[res] = val;
            }
            for (const auto& [res, val] : day_result.net.raw()) {
                if (val != 0.0) {
                    snap.net[res] = val;
                }
            }
            snap.unmet = day_result.unmet;
            for (const auto& inst : buildings) {
                if (inst.type_id == "home_base") {
                    // The manor house is always a single instance, so a count of
                    // 1 is meaningless. Record its LEVEL instead.
                    snap.buildings["home_base"] = inst.level;
                } else {
                    snap.buildings[inst.type_id] += 1;
                }
            }
            trace.push_back(std::move(snap));
        }
    }

    // Score: total liquid gold-equivalent + rough value of standing buildings.
    manor_sim_outcome out;
    out.label = label;
    out.net_gold = state.get("gold");
    out.net_silver = state.get("silver_pence");
    out.buildings = buildings;
    out.score = state.total_gold_equivalent();

    std::unordered_map<std::string, double> import_prices_gold;
    const auto& economy_json = loader_.economy();
    if (economy_json.contains("import_prices") && economy_json["import_prices"].is_object()) {
        for (auto it = economy_json["import_prices"].begin(); it != economy_json["import_prices"].end(); ++it) {
            if (it.value().is_number()) {
                import_prices_gold[it.key()] = it.value().get<double>();
            } else if (it.value().is_object()) {
                import_prices_gold[it.key()] = it.value().value("gold", 0.0)
                                               + it.value().value("shillings", 0.0) / 20.0
                                               + it.value().value("pence", 0.0) / 240.0;
            }
        }
    }
    for (const auto& inst : buildings) {
        const building_type* b = registry_.find(inst.type_id);
        if (b && b->id() != "home_base" && b->id() != "road") {
            out.score += b->gold_normalized_cost(import_prices_gold) * 0.8;
        }
    }

    // Running maximum: retain this run's day-by-day trace if it beats the
    // current top run (or no top run has been recorded yet).
    if (config_.record_top_trace &&
        (best_trace_.empty() || out.score > best_trace_.score)) {
        best_trace_.label = label;
        best_trace_.score = out.score;
        best_trace_.days = std::move(trace);
    }

    return out;
}

void manor_strategy_sim::print_progress(const std::string& label, int done, int total) const {
    if (config_.quiet || total <= 1) {
        return;
    }
    std::cerr << "\r\x1b[2K  [sim] " << label << ": " << done << "/" << total << std::flush;
}

std::vector<manor_sim_outcome> manor_strategy_sim::run_heuristic() {
    std::vector<manor_sim_outcome> results;
    for (const auto& h : heuristics_) {
        const std::string label = "heuristic '" + h.id + "'";
        for (int i = 0; i < config_.iterations; ++i) {
            print_progress(label, i + 1, config_.iterations);
            results.push_back(evaluate(h.id + "_" + std::to_string(i), &h, nullptr));
        }
        if (!config_.quiet && config_.iterations > 1) {
            std::cerr << "\n";
        }
    }
    return results;
}

std::vector<manor_sim_outcome> manor_strategy_sim::run_monte_carlo() {
    std::vector<manor_sim_outcome> results;

    // Candidate universe for the per-run randomized weight vector: all
    // production (non-infrastructure, non-water-powered) building types.
    std::vector<std::string> candidates;
    for (const auto& b : registry_.all()) {
        if (b.id() == "home_base" || is_infrastructure(b.id()) || b.is_water_powered()) {
            continue;
        }
        candidates.push_back(b.id());
    }

    for (int i = 0; i < config_.iterations; ++i) {
        print_progress("monte_carlo", i + 1, config_.iterations);
        // Each run draws a fresh random weight for every candidate building;
        // the weighted pick re-normalizes over the feasible subset per decision.
        std::unordered_map<std::string, double> weights;
        for (const auto& cid : candidates) {
            weights[cid] = std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
        }
        weights[kManorUpgradeAction] = std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
        results.push_back(evaluate("mc_" + std::to_string(i), nullptr, &weights));
    }
    if (!config_.quiet && config_.iterations > 1) {
        std::cerr << "\n";
    }
    return results;
}

std::vector<manor_sim_outcome> manor_strategy_sim::run_all() {
    auto results = run_heuristic();
    auto mc = run_monte_carlo();
    results.insert(results.end(), mc.begin(), mc.end());
    return results;
}

optimality_distribution manor_strategy_sim::distribution(
    const std::vector<manor_sim_outcome>& outcomes) {
    std::vector<strategy_result> results;
    results.reserve(outcomes.size());
    for (const auto& o : outcomes) {
        results.push_back({o.label, o.score});
    }
    return optimality_distribution(std::move(results));
}

std::string manor_strategy_sim::render(
    const std::vector<manor_sim_outcome>& outcomes) const {
    std::ostringstream out;
    out << "Manor strategy simulation (" << nfmt::format_int(static_cast<long long>(outcomes.size())) << " runs, "
        << nfmt::format_int(config_.days) << " days)\n";
    out << "  seed=" << effective_seed_
        << " (re-run with --seed " << effective_seed_ << " to reproduce)\n";

    // Per-heuristic descriptive statistics.
    out << "\nPer-heuristic statistics (score):\n";
    for (const auto& h : heuristics_) {
        std::vector<double> scores;
        for (const auto& o : outcomes) {
            if (o.label.rfind(h.id + "_", 0) == 0) {
                scores.push_back(o.score);
            }
        }
        if (scores.empty()) {
            continue;
        }
        descriptive_stats s = compute_descriptive_stats(scores);
        out << "  " << h.id << ": " << s.render_line() << "\n";
    }
    // Monte-Carlo aggregate.
    {
        std::vector<double> scores;
        for (const auto& o : outcomes) {
            if (o.label.rfind("mc_", 0) == 0) {
                scores.push_back(o.score);
            }
        }
        if (!scores.empty()) {
            descriptive_stats s = compute_descriptive_stats(scores);
            out << "  mc (random): " << s.render_line() << "\n";
        }
    }

    out << "\n" << distribution(outcomes).render_line();

    // Top strategies.
    auto sorted = outcomes;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const manor_sim_outcome& a, const manor_sim_outcome& b) {
                         return a.score > b.score;
                     });
    out << "\nTop 10 runs:\n";
    size_t top = std::min<size_t>(10, sorted.size());
    for (size_t i = 0; i < top; ++i) {
        const auto& o = sorted[i];
        out << "  #" << (i + 1) << " " << o.label
            << "  score=" << fmt(o.score)
            << " (gold=" << fmt(o.net_gold) << ", silver=" << fmt(o.net_silver)
            << ", buildings=" << nfmt::format_int(static_cast<long long>(o.buildings.size())) << ")\n";
    }

    // Day-by-day trace of the top run (only present when tracing was enabled).
    if (config_.record_top_trace && !best_trace_.empty()) {
        out << "\nTop run day-by-day: " << best_trace_.label
            << "  score=" << fmt(best_trace_.score) << "\n";
        for (const auto& snap : best_trace_.days) {
            const std::string pre = stripe_prefix(config_.color, snap.day);
            const std::string reset = stripe_reset(config_.color);
            const std::string day_label = "  day " + std::to_string(snap.day)
                                          + " [ml=" + std::to_string(snap.manor_level) + "]";

            std::vector<std::string> bld_entries;
            for (const auto& [id, count] : snap.buildings) {
                if (count > 0) {
                    bld_entries.push_back(id + "=" + nfmt::format_int(static_cast<long long>(count)));
                }
            }
            write_wrapped_section(out, bld_entries, kTracePerLine,
                                  day_label + "  buildings:", pre, reset);
            out << "\n";

            std::vector<std::string> res_entries;
            for (const auto& [res, val] : snap.resources) {
                if (val == 0.0) {
                    continue;
                }
                res_entries.push_back(res + "=" + nfmt::format_number(val));
            }
            write_wrapped_section(out, res_entries, kTracePerLine,
                                  day_label + "  resources:", pre, reset);
            out << "\n";

            std::vector<std::string> net_entries;
            for (const auto& [res, val] : snap.net) {
                if (val == 0.0) {
                    continue;
                }
                std::string sign = val > 0.0 ? "+" : "";
                net_entries.push_back(res + "=" + sign + nfmt::format_number(val));
            }
            write_wrapped_section(out, net_entries, kTracePerLine,
                                  day_label + "  net:", pre, reset);
            out << "\n";

            std::vector<std::string> unmet_entries;
            for (const auto& [res, val] : snap.unmet) {
                if (val != 0.0) {
                    unmet_entries.push_back(res + "=" + nfmt::format_number(val));
                }
            }
            write_wrapped_section(out, unmet_entries, kTracePerLine,
                                  day_label + "  unmet:", pre, reset);
            out << "\n";
        }
    }
    return out.str();
}

json manor_strategy_sim::render_json(
    const std::vector<manor_sim_outcome>& outcomes) const {
    json j;
    j["days"] = config_.days;
    j["iterations"] = config_.iterations;
    j["seed"] = effective_seed_;
    j["distribution"] = distribution(outcomes).render_json();

    // Per-heuristic descriptive statistics.
    json heuristics_json = json::array();
    for (const auto& h : heuristics_) {
        std::vector<double> scores;
        for (const auto& o : outcomes) {
            if (o.label.rfind(h.id + "_", 0) == 0) {
                scores.push_back(o.score);
            }
        }
        if (scores.empty()) {
            continue;
        }
        json hj;
        hj["id"] = h.id;
        hj["stats"] = compute_descriptive_stats(scores).render_json();
        heuristics_json.push_back(std::move(hj));
    }
    {
        std::vector<double> scores;
        for (const auto& o : outcomes) {
            if (o.label.rfind("mc_", 0) == 0) {
                scores.push_back(o.score);
            }
        }
        if (!scores.empty()) {
            json mj;
            mj["id"] = "mc";
            mj["stats"] = compute_descriptive_stats(scores).render_json();
            heuristics_json.push_back(std::move(mj));
        }
    }
    j["strategies"] = std::move(heuristics_json);

    json list = json::array();
    auto sorted = outcomes;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const manor_sim_outcome& a, const manor_sim_outcome& b) {
                         return a.score > b.score;
                     });
    for (const auto& o : sorted) {
        json item;
        item["label"] = o.label;
        item["score"] = o.score;
        item["gold"] = o.net_gold;
        item["silver_pence"] = o.net_silver;
        json bld = json::array();
        for (const auto& b : o.buildings) {
            bld.push_back({{"type", b.type_id}, {"level", b.level}});
        }
        item["buildings"] = std::move(bld);
        list.push_back(std::move(item));
    }
    j["results"] = std::move(list);

    if (config_.record_top_trace && !best_trace_.empty()) {
        json top;
        top["label"] = best_trace_.label;
        top["score"] = best_trace_.score;
        json days = json::array();
        for (const auto& snap : best_trace_.days) {
            days.push_back({
                {"day", snap.day},
                {"manor_level", snap.manor_level},
                {"resources", snap.resources},
                {"net", snap.net},
                {"unmet", snap.unmet},
                {"buildings", snap.buildings},
            });
        }
        top["days"] = std::move(days);
        j["top_run"] = std::move(top);
    }
    return j;
}
