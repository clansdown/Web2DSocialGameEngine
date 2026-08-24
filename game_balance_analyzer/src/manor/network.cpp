#include "manor/network.hpp"
#include "fmt.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace {

constexpr double kPencePerGold = 240.0;

// The production-network report is a steady-state model: it treats the manor
// house as always present at full expansion, and its arable/forest land totals
// are read at this assumed manor level (lines below use it directly). The
// counts display reports home_base as this level rather than a meaningless
// constant count of 1.
constexpr int kAssumedManorLevel = 10;

// Shared number formatting (comma-grouped, never scientific).
std::string fmt(double v) { return nfmt::format_number(v); }

// Compact integer formatting for counts.
std::string fmt(int v) { return nfmt::format_int(v); }

}  // namespace

manor_network_analyzer::manor_network_analyzer(const config_loader& loader,
                                               network_config config)
    : loader_(loader),
      economy_(registry_, loader.economy()),
      config_(config) {
    registry_.load(loader_.building_types());
}

std::vector<std::vector<std::string>> manor_network_analyzer::collect_chains() const {
    // Excluded: infrastructure and morale-only chains. Infrastructure has no
    // production to balance; morale buildings only boost morale/roads.
    static const std::set<std::string> kExcluded = {
        "road", "head_race", "tail_race", "mill_pond",
        "chapel", "church", "parish_church",
    };
    std::vector<std::vector<std::string>> chains;
    for (const auto& b : registry_.all()) {
        if (b.built_from()) {
            continue;  // not a chain root
        }
        if (b.id() == "home_base") {
            continue;  // handled separately in stage_set
        }
        if (kExcluded.count(b.id())) {
            continue;
        }
        // Walk DOWN from the root through successors to get the full chain
        // (chain_for walks up via built_from and would only return the root).
        std::vector<std::string> chain;
        std::string cur = b.id();
        while (true) {
            const building_type* stage = registry_.find(cur);
            if (!stage) {
                break;
            }
            chain.push_back(cur);
            if (stage->successor()) {
                cur = *stage->successor();
            } else {
                break;
            }
        }
        chains.push_back(std::move(chain));
    }
    // Stable order: by first chain element (config order of roots).
    return chains;
}

std::vector<std::string> manor_network_analyzer::stage_set(int s) const {
    std::vector<std::string> set;
    set.push_back("home_base");
    for (const auto& chain : collect_chains()) {
        size_t idx = std::min<size_t>(static_cast<size_t>(s) - 1, chain.size() - 1);
        set.push_back(chain[idx]);
    }
    return set;
}

std::vector<std::string> manor_network_analyzer::all_resources(
    const std::vector<std::string>& set_ids) const {
    std::set<std::string> res;
    for (const auto& id : set_ids) {
        const building_type* b = registry_.find(id);
        if (!b) {
            continue;
        }
        for (const auto& [r, amt] : b->outputs_at(1)) {
            res.insert(r);
        }
        for (const auto& [r, amt] : b->inputs_at(1)) {
            if (r != "silver_pence") {
                res.insert(r);
            }
        }
        for (const auto& [r, amt] : b->daily_cost()) {
            if (r != "silver_pence") {
                res.insert(r);
            }
        }
    }
    return {res.begin(), res.end()};
}

building_flows manor_network_analyzer::flows_for(
    const building_type& b, int level,
    const std::vector<std::pair<std::string, int>>& counts) const {
    building_flows f;
    f.type_id = b.id();
    f.level = level;
    for (const auto& [res, amt] : b.outputs_at(level)) {
        f.outputs[res] = amt;
    }
    f.inputs = b.inputs_at(level);
    f.daily_cost = b.daily_cost();

    // Modifier effects (chain-aware target matching, ideal-targets assumption):
    // sources are grouped by `modifier_id` so that within a group each target
    // is boosted at most once (the server's assigned_targets model — a miller
    // and windmill never stack). Coverage is assigned in (level, multiplier)
    // priority, so the strongest source covers the population first and weaker
    // sources only cover overflow; the effective per-target multiplier over the
    // covered targets is the slot-weighted mean. Groups with different
    // modifier_id combine multiplicatively. The aggregate effect on a target is
    //   1 + (covered/targets) * (mean_multiplier - 1).
    int tgt_count = 0;
    for (const auto& [id, c] : counts) {
        if (id == b.id()) {
            tgt_count = c;
            break;
        }
    }
    if (tgt_count <= 0) {
        tgt_count = 1;
    }
    struct src_contrib {
        int level = 1;
        double multiplier = 1.0;
        int slots = 0;  // distinct targets this source can cover
    };
    std::map<std::tuple<std::string, std::string, std::string>, std::vector<src_contrib>> mod_groups;
    for (const auto& [src_id, src_count] : counts) {
        const building_type* src = registry_.find(src_id);
        if (!src) {
            continue;
        }
        const auto& raw = src->raw();
        if (!raw.contains("modifiers") || !raw["modifiers"].is_array()) {
            continue;
        }
        // The summary model evaluates every building at level 1.
        for (const auto& mod : raw["modifiers"]) {
            std::string modifier_id = mod.value("modifier_id", "");
            if (modifier_id.empty()) {
                continue;
            }
            std::string target_building = mod.value("target_building", "");
            if (!registry_.satisfies(b.id(), target_building)) {
                continue;
            }
            std::string resource = mod.value("target_resource", "");
            if (f.outputs.find(resource) == f.outputs.end()) {
                continue;
            }
            double multiplier = amount_at_level(mod["multiplier"], 1);
            int max_targets = mod.contains("max_targets")
                                  ? std::max(1, static_cast<int>(std::lround(
                                                    amount_at_level(mod["max_targets"], 1))))
                                  : 1;
            mod_groups[{modifier_id, target_building, resource}].push_back(
                {1, multiplier, src_count * max_targets});
        }
    }
    std::unordered_map<std::string, double> mods;
    for (auto& [key, contribs] : mod_groups) {
        (void)key;
        std::stable_sort(contribs.begin(), contribs.end(),
                         [](const src_contrib& a, const src_contrib& b) {
                             if (a.level != b.level) return a.level > b.level;
                             return a.multiplier > b.multiplier;
                         });
        int covered = 0;
        double slot_weighted = 0.0;
        int remaining_targets = tgt_count;
        for (const auto& c : contribs) {
            if (remaining_targets <= 0) {
                break;
            }
            int use = std::min(c.slots, remaining_targets);
            covered += use;
            slot_weighted += static_cast<double>(use) * c.multiplier;
            remaining_targets -= use;
        }
        if (covered <= 0) {
            continue;
        }
        const std::string& resource = std::get<2>(key);
        double mean = slot_weighted / static_cast<double>(covered);
        double per_target = 1.0 + static_cast<double>(covered) / tgt_count * (mean - 1.0);
        double& cur = mods[resource];
        cur = (cur == 0.0) ? per_target : cur * per_target;
    }
    for (auto& [res, amt] : f.outputs) {
        auto it = mods.find(res);
        if (it != mods.end()) {
            amt *= it->second;
        }
    }
    return f;
}

std::unordered_map<std::string, double> manor_network_analyzer::compute_nets(
    const std::vector<std::pair<std::string, int>>& counts,
    const std::vector<std::string>& resources) const {
    std::unordered_map<std::string, double> net;
    for (const auto& r : resources) {
        net[r] = 0.0;
    }
    for (const auto& [id, c] : counts) {
        const building_type* b = registry_.find(id);
        if (!b) {
            continue;
        }
        building_flows f = flows_for(*b, 1, counts);
        for (const auto& [res, amt] : f.outputs) {
            net[res] += amt * c;
        }
        for (const auto& [res, amt] : f.inputs) {
            net[res] -= amt * c;
        }
        for (const auto& [res, amt] : f.daily_cost) {
            net[res] -= amt * c;
        }
    }
    return net;
}

std::vector<std::pair<std::string, int>> manor_network_analyzer::solve_balance(
    const std::vector<std::string>& set_ids,
    const std::vector<std::string>& resources,
    std::vector<std::string>& warnings) const {
    std::vector<std::pair<std::string, int>> counts;
    for (const auto& id : set_ids) {
        counts.emplace_back(id, 1);
    }

    // Greedy deficit-driven fixpoint: while some resource is in deficit,
    // increment the producer of the worst deficit that contributes the most
    // net of that resource. Counts only ever grow, so the iteration terminates
    // at a self-sufficient superset.
    const int kMaxIterations = 20000;
    bool balanced = false;
    for (int iter = 0; iter < kMaxIterations; ++iter) {
        auto nets = compute_nets(counts, resources);

        std::string worst;
        double worst_val = 0.0;
        for (const auto& r : resources) {
            if (nets[r] < worst_val) {
                worst_val = nets[r];
                worst = r;
            }
        }
        if (worst.empty()) {
            balanced = true;
            break;
        }

        // Choose the type that produces the most net of the deficit resource
        // per additional instance.
        std::string best_type;
        double best_contrib = 0.0;
        for (const auto& [id, c] : counts) {
            const building_type* b = registry_.find(id);
            if (!b) {
                continue;
            }
            building_flows f = flows_for(*b, 1, counts);
            double per = 0.0;
            auto o = f.outputs.find(worst);
            if (o != f.outputs.end()) {
                per += o->second;
            }
            auto i = f.inputs.find(worst);
            if (i != f.inputs.end()) {
                per -= i->second;
            }
            auto d = f.daily_cost.find(worst);
            if (d != f.daily_cost.end()) {
                per -= d->second;
            }
            if (per > best_contrib) {
                best_contrib = per;
                best_type = id;
            }
        }
        if (best_type.empty()) {
            warnings.push_back("Unresolvable deficit for '" + worst
                               + "': no building in the set produces it.");
            break;
        }
        for (auto& [id, c] : counts) {
            if (id == best_type) {
                ++c;
                break;
            }
        }
    }

    if (!balanced) {
        warnings.push_back("Balance solve did not converge; the reported set may "
                           "not be self-sufficient.");
    }
    return counts;
}

manor_network_analyzer::build_result manor_network_analyzer::simulate_build(
    const std::vector<std::pair<std::string, int>>& counts) const {
    // Deterministic parallel-construction-time estimate. Every building in the
    // set is queued; up to `build_slots` are constructed concurrently, each
    // taking construction_time[level 1] / day_seconds days. The manor house is
    // already present (auto-placed free) and is not built here.
    //
    // Simplifications (documented): construction starts as soon as a slot is
    // free (gold/wood affordability is assumed satisfied over the build
    // window) and builds complete at the same rate regardless of economy. The
    // total gold build cost is reported separately.
    std::vector<std::pair<std::string, int>> queue;
    double gold_spent = 0.0;
    for (const auto& [id, c] : counts) {
        if (id == "home_base") {
            continue;
        }
        queue.emplace_back(id, c);
        const building_type* b = registry_.find(id);
        if (b) {
            gold_spent += b->cost("gold") * c;
        }
    }

    if (queue.empty()) {
        // Only the manor house in the set: nothing to build.
        return {0.0, gold_spent, true};
    }

    // Start long builds first so parallel slots stay busy.
    std::stable_sort(queue.begin(), queue.end(), [this](const auto& a, const auto& b) {
        const building_type* ba = registry_.find(a.first);
        const building_type* bb = registry_.find(b.first);
        double ta = ba ? ba->construction_time(1) : 0.0;
        double tb = bb ? bb->construction_time(1) : 0.0;
        return ta > tb;
    });

    struct slot {
        double remaining = 0.0;   // days left on the current build
    };
    std::vector<slot> slots(static_cast<size_t>(std::max(1, config_.build_slots)));

    int remaining_buildings = 0;
    for (const auto& [id, c] : queue) {
        remaining_buildings += c;
    }

    double days = 0.0;
    const int kMaxDays = 5000;
    while (remaining_buildings > 0) {
        // Assign queued buildings to free slots.
        for (auto& sl : slots) {
            if (sl.remaining > 0.0) {
                continue;
            }
            auto it = std::find_if(queue.begin(), queue.end(),
                                   [](const auto& q) { return q.second > 0; });
            if (it == queue.end()) {
                continue;
            }
            const building_type* b = registry_.find(it->first);
            sl.remaining = (b ? b->construction_time(1) : 0.0) / config_.day_seconds;
            if (--it->second <= 0) {
                // (leaves a zero-count entry; skipped next pass)
            }
        }

        // Tick one day of construction.
        bool any_active = false;
        for (auto& sl : slots) {
            if (sl.remaining <= 0.0) {
                continue;
            }
            any_active = true;
            sl.remaining -= 1.0;
            if (sl.remaining <= 0.0) {
                --remaining_buildings;
            }
        }
        ++days;
        if (!any_active) {
            break;  // nothing in progress and nothing queued (shouldn't happen)
        }
        if (days >= kMaxDays) {
            return {days, gold_spent, false};
        }
    }
    return {days, gold_spent, true};
}

std::string manor_network_analyzer::choose_anchor(
    const std::vector<std::pair<std::string, int>>& counts,
    const std::vector<std::string>& set_ids) const {
    // The anchor is the manufactured-good hub we scale around (e.g. the
    // blacksmith that many peasants/woodcutters/colliers/bloomeries feed). We
    // prefer the least-numerous type that produces the manufactured metal
    // goods (ironwork/fancy_ironwork); failing that, the least-numerous type
    // with the highest gold-normalized build cost; finally the first
    // least-numerous non-home_base type.
    auto produces_metal = [&](const building_type* b) {
        for (const auto& [res, amt] : b->outputs_at(1)) {
            if (res == "ironwork" || res == "fancy_ironwork") {
                return true;
            }
        }
        return false;
    };

    // Pass 1: the least-numerous metal-producing type (ironwork/fancy_ironwork
    // manufacturer) is the anchor. This is scoped among metal producers — the
    // smithy may legitimately have a higher count than raw-material buildings
    // (e.g. water_smithy needs 5 while timber_hauler needs 1), but it is still
    // the manufactured-good hub we scale around.
    int metal_min_c = std::numeric_limits<int>::max();
    std::string metal_anchor;
    for (const auto& [id, c] : counts) {
        if (id == "home_base") {
            continue;
        }
        const building_type* b = registry_.find(id);
        if (b && produces_metal(b)) {
            if (c < metal_min_c) {
                metal_min_c = c;
                metal_anchor = id;
            }
        }
    }
    if (!metal_anchor.empty()) {
        return metal_anchor;
    }

    int min_c = std::numeric_limits<int>::max();
    for (const auto& [id, c] : counts) {
        if (id != "home_base") {
            min_c = std::min(min_c, c);
        }
    }

    // Gold-normalized level-1 build cost for the fallback comparison.
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

    // Pass 2: least-numerous type with the highest gold-normalized build cost.
    std::string best;
    double best_cost = -1.0;
    for (const auto& [id, c] : counts) {
        if (id == "home_base" || c != min_c) {
            continue;
        }
        const building_type* b = registry_.find(id);
        if (!b) {
            continue;
        }
        double cost = b->gold_normalized_cost(import_prices_gold);
        if (cost > best_cost) {
            best_cost = cost;
            best = id;
        }
    }
    if (!best.empty()) {
        return best;
    }

    // Pass 3: first least-numerous non-home_base type.
    for (const auto& [id, c] : counts) {
        if (id != "home_base" && c == min_c) {
            return id;
        }
    }
    return set_ids.empty() ? "home_base" : set_ids.front();
}

network_stage manor_network_analyzer::analyze_stage(int s) const {
    network_stage stage;
    stage.stage = s;
    stage.set_ids = stage_set(s);

    std::vector<std::string> resources = all_resources(stage.set_ids);
    std::vector<std::string> solve_warnings;
    auto base = solve_balance(stage.set_ids, resources, solve_warnings);
    for (const auto& w : solve_warnings) {
        stage.warnings.push_back(w);
    }

    // Anchor: the least-numerous production type, preferring a "final-goods"
    // producer. The manor house is always single and is excluded.
    int min_c = 1;
    for (const auto& [id, c] : base) {
        if (id != "home_base") {
            min_c = std::min(min_c, c);
        }
    }
    std::string anchor = choose_anchor(base, stage.set_ids);
    // Re-derive the anchor's multiplicity for scaling.
    for (const auto& [id, c] : base) {
        if (id == anchor) {
            min_c = c;
            break;
        }
    }

    for (int k = 1; k <= config_.scales; ++k) {
        network_scale sc;
        sc.k = k;
        sc.anchor_id = anchor;
        for (const auto& [id, c] : base) {
            // The manor house stays at 1; production buildings scale with the
            // anchor multiplicity.
            int scaled = (id == "home_base")
                             ? 1
                             : static_cast<int>(std::ceil(static_cast<double>(k) * c / min_c));
            sc.counts.emplace_back(id, scaled);
        }

        // Net resources/day and gold/silver income. Positive net = exportable
        // surplus (export price); negative net on an unproduced resource = a
        // daily import cost (import price) — both deducted from net gold.
        auto nets = compute_nets(sc.counts, resources);
        for (const auto& [res, net] : nets) {
            sc.net_resources_per_day[res] = net;
            if (res == "gold") {
                sc.net_gold_per_day += net;
                continue;
            }
            if (res == "silver_pence") {
                continue;
            }
            if (net < 0.0) {
                double price = economy_.import_price_gold(res);
                double value_gold = -net * price;
                if (economy_.is_penny_market(res)) {
                    sc.net_silver_per_day -= value_gold * kPencePerGold;
                } else {
                    sc.net_gold_per_day -= value_gold;
                }
                continue;
            }
            double price = economy_.export_price_gold(res);
            double value_gold = net * price;
            if (economy_.is_penny_market(res)) {
                sc.net_silver_per_day += value_gold * kPencePerGold;
            } else {
                sc.net_gold_per_day += value_gold;
            }
        }
        // Net gold includes the silver-pence income converted to gold.
        sc.net_gold_per_day += sc.net_silver_per_day / kPencePerGold;

        // Total build cost per resource across the scaled set.
        static const char* build_resources[] = {
            "gold", "wood", "steel", "bronze", "grain", "leather",
            "mana", "charcoal", "iron", "ironwork", "fancy_ironwork", "silver_pence",
            "beams", "boards",
        };
        for (const auto& [id, c] : sc.counts) {
            const building_type* b = registry_.find(id);
            if (!b) {
                continue;
            }
            for (const char* res : build_resources) {
                double cost = b->cost(res);
                if (cost > 0.0) {
                    sc.build_cost[res] += cost * c;
                }
            }
        }

        // Build-time estimate.
        auto br = simulate_build(sc.counts);
        sc.build_days = br.days;
        sc.cumulative_gold_spent = br.gold_spent;
        if (!br.converged) {
            sc.warnings.push_back("Build-out did not converge within 5000 days.");
        }

        // Warnings: max_per_fiefdom caps and water-power limits.
        int water_count = 0;
        for (const auto& [id, c] : sc.counts) {
            const building_type* b = registry_.find(id);
            if (!b) {
                continue;
            }
            if (b->is_water_powered()) {
                water_count += c;
            }
            if (b->has_max_per_fiefdom() && c > b->max_per_fiefdom()) {
                sc.warnings.push_back("'" + id + "' requires " + fmt(c)
                                      + " but max_per_fiefdom is "
                                      + fmt(b->max_per_fiefdom()) + ".");
            }
        }
        if (water_count > 6) {
            sc.warnings.push_back("Cannot power " + fmt(water_count)
                                  + " water-powered buildings (stone pond caps at 6).");
        } else if (water_count > 2) {
            sc.warnings.push_back("Needs " + fmt(water_count)
                                  + " water-power; upgrade mill pond (earthen 2, timber 4, stone 6).");
        }

        // Arable land: report acres used and warn if it exceeds the max
        // available at a fully-upgraded (level-10) manor house.
        sc.arable_total = economy_.arable_land_for_level(kAssumedManorLevel);
        for (const auto& [id, c] : sc.counts) {
            const building_type* b = registry_.find(id);
            if (b) {
                sc.arable_acres += b->arable_acres() * c;
            }
        }
        if (sc.arable_acres > sc.arable_total + 1e-9) {
            sc.warnings.push_back("Set uses " + fmt(sc.arable_acres)
                                  + " arable acres, exceeding the level-10 manor maximum of "
                                  + fmt(sc.arable_total) + ".");
        }

        // Forest land: same reporting for the wood producers (off-map resource).
        sc.forest_total = economy_.forest_land_for_level(kAssumedManorLevel);
        for (const auto& [id, c] : sc.counts) {
            const building_type* b = registry_.find(id);
            if (b) {
                sc.forest_acres += b->forest_acres() * c;
            }
        }
        if (sc.forest_acres > sc.forest_total + 1e-9) {
            sc.warnings.push_back("Set uses " + fmt(sc.forest_acres)
                                  + " forest acres, exceeding the level-10 manor maximum of "
                                  + fmt(sc.forest_total) + ".");
        }

        stage.scales.push_back(std::move(sc));
    }
    return stage;
}

std::vector<network_stage> manor_network_analyzer::analyze() const {
    // Max stage = deepest chain (the peasant chain is 4 deep).
    int max_stage = 1;
    for (const auto& chain : collect_chains()) {
        max_stage = std::max(max_stage, static_cast<int>(chain.size()));
    }
    std::vector<network_stage> stages;
    for (int s = 1; s <= max_stage; ++s) {
        if (config_.max_stages > 0 && s > config_.max_stages) {
            break;
        }
        stages.push_back(analyze_stage(s));
    }
    return stages;
}

std::string manor_network_analyzer::render(
    const std::vector<network_stage>& stages) const {
    std::ostringstream out;
    out << "Manor production network (per stage, all buildings at level 1)\n";

    for (const auto& stage : stages) {
        out << "\n=== Stage " << stage.stage << " ===\n";
        out << "  set:";
        for (const auto& id : stage.set_ids) {
            out << " " << id;
        }
        out << "\n";
        for (const auto& w : stage.warnings) {
            out << "  WARNING: " << w << "\n";
        }
        if (stage.scales.empty()) {
            continue;
        }
        const std::string& anchor = stage.scales.front().anchor_id;

        for (const auto& sc : stage.scales) {
            out << "\n  k=" << sc.k << "  (anchor '" << anchor << "' = " << sc.k << ")\n";
            out << "    counts (ratio):";
            for (const auto& [id, c] : sc.counts) {
                if (id == "home_base") {
                    // The manor house is always a single instance, so a constant
                    // count of 1 is meaningless; report its assumed level instead.
                    out << "  home_base lvl" << kAssumedManorLevel;
                    continue;
                }
                double ratio = sc.k > 0 ? static_cast<double>(c) / sc.k : 0.0;
                out << "  " << id << " " << fmt(c) << " (" << fmt(ratio) << ")";
            }
            out << "\n";
            out << "    net resources/day:";
            bool any = false;
            for (const auto& [res, net] : sc.net_resources_per_day) {
                if (net == 0.0) {
                    continue;
                }
                out << "  " << res << (net > 0.0 ? "+" : "") << fmt(net);
                any = true;
            }
            if (!any) {
                out << "  (none)";
            }
            out << "\n";
            out << "    net gold/day: " << fmt(sc.net_gold_per_day);
            if (sc.net_silver_per_day != 0.0) {
                out << "  (+" << fmt(sc.net_silver_per_day) << " pence from penny-market exports)";
            }
            out << "\n";
            out << "    arable land: " << fmt(sc.arable_acres) << " / "
                << fmt(sc.arable_total) << " acres\n";
            out << "    forest land: " << fmt(sc.forest_acres) << " / "
                << fmt(sc.forest_total) << " acres\n";
            out << "    build cost:";
            for (const auto& [res, amt] : sc.build_cost) {
                out << "  " << res << " " << fmt(amt);
            }
            out << "\n";
            out << "    build time: ~" << fmt(sc.build_days) << " days ("
                << config_.build_slots << " parallel slots, day=" << fmt(config_.day_seconds)
                << "s)  [gold cost " << fmt(sc.cumulative_gold_spent) << "]";
            if (sc.build_days <= 0.0) {
                out << "  (nothing to build)";
            }
            out << "\n";
            for (const auto& w : sc.warnings) {
                out << "    WARNING: " << w << "\n";
            }
        }
    }

    // Stage progression summary at k=1.
    out << "\n=== Stage progression (k=1, anchor count 1) ===\n";
    out << "  stage  set_size  net_gold/day  build_days  build_cost(gold)\n";
    for (const auto& stage : stages) {
        if (stage.scales.empty()) {
            continue;
        }
        const auto& sc = stage.scales.front();
        double gold_cost = 0.0;
        auto it = sc.build_cost.find("gold");
        if (it != sc.build_cost.end()) {
            gold_cost = it->second;
        }
        out << "  " << stage.stage << "        " << stage.set_ids.size() << "          "
            << fmt(sc.net_gold_per_day) << "          " << fmt(sc.build_days) << "        "
            << fmt(gold_cost) << "\n";
    }
    return out.str();
}

json manor_network_analyzer::render_json(
    const std::vector<network_stage>& stages) const {
    json stages_json = json::array();
    for (const auto& stage : stages) {
        json st;
        st["stage"] = stage.stage;
        st["set"] = stage.set_ids;
        st["warnings"] = stage.warnings;
        json scales = json::array();
        for (const auto& sc : stage.scales) {
            json s;
            s["k"] = sc.k;
            s["anchor"] = sc.anchor_id;
            json counts = json::object();
            json ratios = json::object();
            for (const auto& [id, c] : sc.counts) {
                if (id == "home_base") {
                    // The manor house is always a single instance; report its
                    // assumed level rather than a constant count of 1 (no ratio).
                    counts[id] = kAssumedManorLevel;
                    continue;
                }
                counts[id] = c;
                ratios[id] = sc.k > 0 ? static_cast<double>(c) / sc.k : 0.0;
            }
            s["counts"] = std::move(counts);
            s["ratios"] = std::move(ratios);
            s["manor_level"] = kAssumedManorLevel;
            s["net_resources_per_day"] = sc.net_resources_per_day;
            s["net_gold_per_day"] = sc.net_gold_per_day;
            s["net_silver_per_day"] = sc.net_silver_per_day;
            s["arable_acres"] = sc.arable_acres;
            s["arable_total"] = sc.arable_total;
            s["forest_acres"] = sc.forest_acres;
            s["forest_total"] = sc.forest_total;
            s["build_cost"] = sc.build_cost;
            s["build_days"] = sc.build_days;
            s["cumulative_gold_spent"] = sc.cumulative_gold_spent;
            s["warnings"] = sc.warnings;
            scales.push_back(std::move(s));
        }
        st["scales"] = std::move(scales);
        stages_json.push_back(std::move(st));
    }

    json out;
    out["config"] = {
        {"scales", config_.scales},
        {"build_slots", config_.build_slots},
        {"day_seconds", config_.day_seconds},
        {"max_stages", config_.max_stages},
    };
    out["stages"] = std::move(stages_json);

    // Stage progression summary at k=1.
    json summary = json::array();
    for (const auto& stage : stages) {
        if (stage.scales.empty()) {
            continue;
        }
        const auto& sc = stage.scales.front();
        double gold_cost = 0.0;
        auto it = sc.build_cost.find("gold");
        if (it != sc.build_cost.end()) {
            gold_cost = it->second;
        }
        summary.push_back({
            {"stage", stage.stage},
            {"set_size", stage.set_ids.size()},
            {"net_gold_per_day", sc.net_gold_per_day},
            {"build_days", sc.build_days},
            {"build_cost_gold", gold_cost},
        });
    }
    out["summary"] = std::move(summary);
    return out;
}
