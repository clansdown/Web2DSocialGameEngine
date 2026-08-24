#include "manor/manor_economy.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>

using json = nlohmann::json;

namespace {

constexpr double kPencePerGold = 240.0;

double money_object_to_gold(const json& obj) {
    double gold = obj.value("gold", 0.0);
    double shillings = obj.value("shillings", 0.0);
    double pence = obj.value("pence", 0.0);
    return gold + shillings / 20.0 + pence / kPencePerGold;
}

// Converts a money object {gold, shillings, pence} to a pence price,
// mirroring the server's money_price_to_pence (12 pence/shillng,
// 20 shillings/pound, 240 pence/gold). Fractional pence (e.g. 0.5) are
// preserved so half-penny prices work. Returns 0 on malformed input.
double money_price_to_pence(const json& obj) {
    if (!obj.is_object()) {
        return 0.0;
    }
    double gold = obj.value("gold", 0.0);
    double shillings = obj.value("shillings", 0.0);
    double pence = obj.value("pence", 0.0);
    return gold * 240.0 + shillings * 12.0 + pence;
}

} // namespace

double resource_state::get(const std::string& resource) const {
    auto it = data_.find(resource);
    return it == data_.end() ? 0.0 : it->second;
}

void resource_state::set(const std::string& resource, double value) {
    data_[resource] = value;
}

void resource_state::add(const std::string& resource, double delta) {
    data_[resource] = get(resource) + delta;
}

double resource_state::total_gold_equivalent() const {
    return get("gold") + get("silver_pence") / kPencePerGold;
}

manor_economy::manor_economy(const building_registry& registry,
                             const json& economy_config)
    : registry_(registry), economy_(economy_config) {}

double manor_economy::import_price_gold(const std::string& resource) const {
    if (economy_.contains("import_prices") && economy_["import_prices"].is_object()) {
        const auto& prices = economy_["import_prices"];
        if (prices.contains(resource)) {
            const auto& p = prices[resource];
            if (p.is_number()) {
                return p.get<double>();
            }
            if (p.is_object()) {
                return money_object_to_gold(p);
            }
        }
    }
    return 0.0;
}

double manor_economy::export_price_gold(const std::string& resource) const {
    if (economy_.contains("export_prices") && economy_["export_prices"].is_object()) {
        const auto& ep = economy_["export_prices"];
        if (ep.contains(resource)) {
            const auto& p = ep[resource];
            if (p.is_number()) {
                return p.get<double>();
            }
            if (p.is_object()) {
                return money_object_to_gold(p);
            }
        }
    }
    double import = import_price_gold(resource);
    if (economy_.contains("export_sell_multipliers") && economy_["export_sell_multipliers"].is_object()) {
        const auto& m = economy_["export_sell_multipliers"];
        if (m.contains(resource)) {
            return m[resource].get<double>() * import;
        }
    }
    double global = economy_.value("export_sell_multiplier", 0.5);
    return global * import;
}

double manor_economy::reserve(const std::string& resource) const {
    if (economy_.contains("default_reserves") && economy_["default_reserves"].is_object()) {
        const auto& r = economy_["default_reserves"];
        if (r.contains(resource)) {
            return r[resource].get<double>();
        }
    }
    return 0.0;
}

bool manor_economy::is_penny_market(const std::string& resource) const {
    if (economy_.contains("import_prices") && economy_["import_prices"].is_object()) {
        const auto& prices = economy_["import_prices"];
        if (prices.contains(resource)) {
            return prices[resource].is_object();
        }
    }
    return false;
}

double manor_economy::arable_land_for_level(int manor_level) const {
    if (economy_.contains("arable_land_by_level") && economy_["arable_land_by_level"].is_array()) {
        const auto& arr = economy_["arable_land_by_level"];
        if (arr.empty()) return 0.0;
        if (manor_level < 0) manor_level = 0;
        size_t idx = static_cast<size_t>(manor_level);
        if (idx >= arr.size()) idx = arr.size() - 1;
        if (arr[idx].is_number()) return arr[idx].get<double>();
    }
    return 0.0;
}

double manor_economy::forest_land_for_level(int manor_level) const {
    if (economy_.contains("forest_land_by_level") && economy_["forest_land_by_level"].is_array()) {
        const auto& arr = economy_["forest_land_by_level"];
        if (arr.empty()) return 0.0;
        if (manor_level < 0) manor_level = 0;
        size_t idx = static_cast<size_t>(manor_level);
        if (idx >= arr.size()) idx = arr.size() - 1;
        if (arr[idx].is_number()) return arr[idx].get<double>();
    }
    return 0.0;
}

namespace {

// One source instance contributing to a `mod_group`. It boosts up to its own
// `max_targets` distinct target instances (the server's per-source cap); it is
// assigned to targets in level-then-multiplier order, so the strongest source
// covers the population first and weaker sources only cover overflow.
struct mod_contribution {
    int level = 1;
    double multiplier = 1.0;   // multiplier at the source's level
    int max_targets = 1;       // distinct targets this source can boost
    int remaining = 0;         // slots still available this tick
};

// One modifier_id group, mirroring the server's assigned_targets model: within
// a group every target output is boosted at most once (two flour_milling
// sources — miller/windmill/watermill — never stack on the same grain output).
// Groups with different modifier_id stack multiplicatively.
struct mod_group {
    std::string target_building;   // chain/class-aware target id
    std::string resource;          // resource boosted on the target
    std::vector<mod_contribution> sources;
};

} // namespace

economy_day_result manor_economy::run_day(
    resource_state& state,
    const std::vector<building_instance>& buildings,
    int manor_level) const {
    economy_day_result result;

    // manor_level is reserved for future gating analysis; not used in the
    // summary production pass.
    (void)manor_level;

    // Snapshot the starting state so we can report net change precisely.
    resource_state start = state;

    // One production output of a building instance, gated by its own inputs.
    struct inst_output {
        int instance_index = -1;
        std::string building_id;
        std::string resource;
        double amount = 0.0;
        double multiplier = 1.0;
        std::unordered_map<std::string, double> inputs;    // resource -> required
        std::unordered_map<std::string, double> supplied;  // resource -> supplied
    };
    std::vector<inst_output> outputs;

    // Unconditional daily upkeep for one building instance.
    struct inst_upkeep {
        int instance_index = -1;
        std::unordered_map<std::string, double> cost;  // resource -> per-day amount
    };
    std::vector<inst_upkeep> upkeeps;

    // Precompute modifier groups by `modifier_id`, mirroring the server's
    // assigned_targets model: within a group each target output is boosted at
    // most once, so two flour_milling sources (miller/windmill/watermill) never
    // stack on the same grain output. `target_building`/`resource` come from the
    // first source in the group (all sources of one modifier_id share them).
    std::vector<mod_group> groups;
    {
        std::map<std::string, mod_group> by_id;
        for (const auto& inst : buildings) {
            const building_type* src = registry_.find(inst.type_id);
            if (!src) {
                continue;
            }
            const auto& raw = src->raw();
            if (!raw.contains("modifiers") || !raw["modifiers"].is_array()) {
                continue;
            }
            for (const auto& mod : raw["modifiers"]) {
                std::string modifier_id = mod.value("modifier_id", "");
                if (modifier_id.empty()) {
                    continue;
                }
                mod_group& g = by_id[modifier_id];
                if (g.sources.empty()) {
                    g.target_building = mod.value("target_building", "");
                    g.resource = mod.value("target_resource", "");
                }
                mod_contribution c;
                c.level = inst.level;
                c.multiplier = amount_at_level(mod["multiplier"], inst.level);
                c.max_targets = mod.contains("max_targets")
                                    ? std::max(1, static_cast<int>(std::lround(
                                                      amount_at_level(mod["max_targets"], inst.level))))
                                    : 1;
                c.remaining = c.max_targets;
                g.sources.push_back(c);
            }
        }
        for (auto& [id, g] : by_id) {
            (void)id;
            std::stable_sort(g.sources.begin(), g.sources.end(),
                             [](const mod_contribution& a, const mod_contribution& b) {
                                 if (a.level != b.level) return a.level > b.level;
                                 return a.multiplier > b.multiplier;
                             });
            groups.push_back(std::move(g));
        }
    }

    // Build per-instance outputs and unconditional daily upkeep.
    for (size_t ii = 0; ii < buildings.size(); ++ii) {
        const building_type* b = registry_.find(buildings[ii].type_id);
        if (!b) {
            continue;
        }
        int lvl = buildings[ii].level;

        for (const auto& [res, need] : b->daily_cost()) {
            if (res == "gold" || need <= 0.0) {
                continue;  // gold upkeep handled separately below
            }
            inst_upkeep* uk = nullptr;
            for (auto& u : upkeeps) {
                if (u.instance_index == static_cast<int>(ii)) {
                    uk = &u;
                    break;
                }
            }
            if (!uk) {
                upkeeps.push_back({static_cast<int>(ii), {}});
                uk = &upkeeps.back();
            }
            uk->cost[res] += need;
        }

        // The `outputs` array is the canonical production schema: each output
        // declares its own inputs (consumed once per building per day) and an
        // optional min_level unlock. A building-level `inputs` map is not used.
        if (b->raw().contains("outputs") && b->raw()["outputs"].is_array()) {
            for (const auto& out : b->raw()["outputs"]) {
                int min_level = out.value("min_level", 1);
                if (lvl < min_level) {
                    continue;
                }
                inst_output o;
                o.instance_index = static_cast<int>(ii);
                o.building_id = b->id();
                o.resource = out.value("resource", "");
                o.amount = amount_at_level(out["amount"], lvl);
                if (o.amount <= 0.0) {
                    continue;
                }
                if (out.contains("inputs") && out["inputs"].is_object()) {
                    for (auto it = out["inputs"].begin(); it != out["inputs"].end(); ++it) {
                        double need = amount_at_level(it.value(), lvl);
                        if (it.key() != "gold" && need > 0.0) {
                            o.inputs[it.key()] = need;
                        }
                    }
                }
                outputs.push_back(std::move(o));
            }
        }
    }

    // Import-enabled supply of a need: consume from stock, then import the
    // shortfall (floored to whole units, like the server; all resources are
    // importable in the analyzer). Records consumed/imported and leaves the
    // residual in `unmet`. Returns the amount actually supplied.
    auto supply_need = [&](const std::string& res, double need) -> double {
        if (need <= 0.001) {
            return 0.0;
        }
        double avail = state.get(res);
        double effective = std::min(avail, need);
        state.add(res, -effective);
        result.consumed[res] += effective;
        double unmet = need - effective;
        double supplied = effective;
        if (unmet > 0.001) {
            const auto& prices = economy_.value("import_prices", json::object());
            if (prices.is_object() && prices.contains(res)) {
                const auto& price = prices[res];
                int64_t affordable = static_cast<int64_t>(std::floor(unmet));
                if (affordable >= 1) {
                    if (price.is_object()) {
                        // Penny market: pay from the fungible silver+gold wallet
                        // (gold converts to pence at kPencePerGold; silver first).
                        int64_t pence_price = money_price_to_pence(price);
                        if (pence_price > 0) {
                            double silver = state.get("silver_pence");
                            double total_pence = silver + state.get("gold") * kPencePerGold;
                            int64_t units = static_cast<int64_t>(
                                std::floor(total_pence / static_cast<double>(pence_price)));
                            units = std::min(units, affordable);
                            if (units >= 1) {
                                double cost_pence = static_cast<double>(units * pence_price);
                                double from_silver = std::min(silver, cost_pence);
                                state.add("silver_pence", -from_silver);
                                state.add("gold", -(cost_pence - from_silver) / kPencePerGold);
                                state.add(res, static_cast<double>(units));
                                supplied += static_cast<double>(units);
                                unmet -= static_cast<double>(units);
                                result.consumed[res] += static_cast<double>(units);
                                result.imported[res] += static_cast<double>(units);
                            }
                        }
                    } else {
                        // Gold market: pay gold.
                        double price_gold = price.is_number() ? price.get<double>() : 2.0;
                        int64_t units = static_cast<int64_t>(
                            std::floor(state.get("gold") / price_gold));
                        units = std::min(units, affordable);
                        if (units >= 1) {
                            state.add("gold", -static_cast<double>(units) * price_gold);
                            state.add(res, static_cast<double>(units));
                            supplied += static_cast<double>(units);
                            unmet -= static_cast<double>(units);
                            result.consumed[res] += static_cast<double>(units);
                            result.imported[res] += static_cast<double>(units);
                        }
                    }
                }
            }
        }
        if (unmet > 0.001) {
            result.unmet[res] += unmet;
        }
        return supplied;
    };

    // Dependency graph over building instances: an edge A -> B exists when A
    // produces a resource that B consumes as a production input. Production
    // runs in topological order so upstream producers run before downstream
    // consumers; scarce inputs are allocated deterministically (lowest config
    // `priority`, then instance id). daily_cost/population upkeep are NOT graph
    // edges — they run in the upkeep phase after all production.
    int default_prio = static_cast<int>(economy_.value("default_priority", 50));
    std::map<int, int> inst_prio;
    for (size_t ii = 0; ii < buildings.size(); ++ii) {
        const building_type* b = registry_.find(buildings[ii].type_id);
        int p = b ? static_cast<int>(b->raw().value("priority", default_prio)) : default_prio;
        inst_prio[static_cast<int>(ii)] = p;
    }
    std::map<std::string, std::set<int>> producers;
    for (const auto& o : outputs) {
        producers[o.resource].insert(o.instance_index);
    }
    std::map<int, std::set<int>> deps;
    std::map<int, int> indeg;
    for (size_t ii = 0; ii < buildings.size(); ++ii) {
        deps[static_cast<int>(ii)] = {};
        indeg[static_cast<int>(ii)] = 0;
    }
    for (const auto& o : outputs) {
        for (const auto& [res, req] : o.inputs) {
            auto pit = producers.find(res);
            if (pit == producers.end()) {
                continue;
            }
            for (int prod : pit->second) {
                if (prod != o.instance_index) {
                    deps[o.instance_index].insert(prod);
                }
            }
        }
    }
    for (const auto& [ii, us] : deps) {
        indeg[ii] = static_cast<int>(us.size());
    }

    // Kahn topological order over all instances.
    std::vector<int> order;
    {
        std::set<int> remaining;
        for (size_t ii = 0; ii < buildings.size(); ++ii) {
            remaining.insert(static_cast<int>(ii));
        }
        while (!remaining.empty()) {
            int chosen = -1;
            int chosen_prio = 0;
            for (int ii : remaining) {
                if (indeg[ii] != 0) {
                    continue;
                }
                int p = inst_prio.count(ii) ? inst_prio[ii] : default_prio;
                if (chosen == -1 || p < chosen_prio || (p == chosen_prio && ii < chosen)) {
                    chosen = ii;
                    chosen_prio = p;
                }
            }
            if (chosen == -1) {
                break;  // cycle guard: skip remaining instances this tick
            }
            order.push_back(chosen);
            remaining.erase(chosen);
            for (int ii : remaining) {
                if (deps[ii].count(chosen)) {
                    --indeg[ii];
                }
            }
        }
    }

    // Produce phase: run in topological order so upstream producers have
    // already produced before downstream consumers consume (and so imported
    // inputs can feed same-tick production).
    for (int ii : order) {
        // Consume this instance's inputs, then produce its outputs.
        for (auto& o : outputs) {
            if (o.instance_index != ii) {
                continue;
            }
            for (const auto& [res, req] : o.inputs) {
                o.supplied[res] = supply_need(res, req);
            }
        }
        for (auto& o : outputs) {
            if (o.instance_index != ii) {
                continue;
            }
            double ratio = 1.0;
            if (!o.inputs.empty()) {
                for (const auto& [res, req] : o.inputs) {
                    double supplied = o.supplied.count(res) ? o.supplied[res] : 0.0;
                    double r = (req > 0) ? std::min(1.0, supplied / req) : 1.0;
                    ratio = std::min(ratio, r);
                }
            }
            // Modifier effects: within a modifier_id group each target output is
            // boosted by at most one source (the strongest with remaining
            // capacity), matching the server's assigned_targets model. Groups
            // with different modifier_id stack multiplicatively.
            for (auto& g : groups) {
                if (!registry_.satisfies(o.building_id, g.target_building)) {
                    continue;
                }
                if (g.resource != o.resource) {
                    continue;
                }
                for (auto& src : g.sources) {
                    if (src.remaining <= 0) {
                        continue;
                    }
                    o.multiplier *= src.multiplier;
                    --src.remaining;
                    break;
                }
            }
            double produced = o.amount * ratio * o.multiplier;
            if (produced <= 0.0) {
                continue;
            }
            state.add(o.resource, produced);
            result.produced[o.resource] += produced;
        }
    }

    // Upkeep phase: consume each instance's daily_cost AFTER all production,
    // so any building's output can feed any household the same day.
    for (auto& uk : upkeeps) {
        for (const auto& [res, need] : uk.cost) {
            supply_need(res, need);
        }
    }

    // Gold upkeep (consumes gold from stock if any building has a gold daily_cost).
    for (const auto& inst : buildings) {
        const building_type* b = registry_.find(inst.type_id);
        if (!b) {
            continue;
        }
        auto it = b->daily_cost().find("gold");
        if (it != b->daily_cost().end() && it->second > 0) {
            double need = it->second;
            double avail = state.get("gold");
            double effective = std::min(avail, need);
            state.add("gold", -effective);
            result.consumed["gold"] += effective;
            double unmet = need - effective;
            if (unmet > 0.001) {
                result.consumed["gold"] += unmet;  // gold upkeep that couldn't be paid
            }
        }
    }

    // Step 3: sell excess above reserve at the export price (crediting
    // silver_pence for penny-market resources, gold otherwise). Reserves are
    // filled by production surplus only; we never import to fill them.
    static const char* sellable[] = {
        "grain", "wood", "steel", "bronze", "leather", "mana",
        "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards",
    };
    for (const char* res : sellable) {
        double cur = state.get(res);
        double resv = reserve(res);
        if (cur <= resv + 0.001) {
            continue;
        }
        double excess = cur - resv;
        double unit_value = 0.0;
        bool pence_market = is_penny_market(res);
        // Per-unit sell price with precedence: export_prices -> export_sell_multipliers
        // -> export_sell_multiplier (× import price).
        const auto& exports = economy_.value("export_prices", json::object());
        if (exports.is_object() && exports.contains(res)) {
            const auto& ep = exports[res];
            if (ep.is_object()) {
                pence_market = true;
                unit_value = static_cast<double>(money_price_to_pence(ep));
            } else {
                pence_market = false;
                unit_value = ep.get<double>();
            }
        } else {
            double ratio = economy_.value("export_sell_multiplier", 0.5);
            const auto& mults = economy_.value("export_sell_multipliers", json::object());
            if (mults.is_object() && mults.contains(res) && mults[res].is_number()) {
                ratio = mults[res].get<double>();
            }
            double import = import_price_gold(res);
            if (pence_market) {
                unit_value = static_cast<double>(money_price_to_pence(economy_.value("import_prices", json::object()).value(res, json::object()))) * ratio;
            } else {
                unit_value = import * ratio;
            }
        }

        state.set(res, resv);
        result.exported[res] = excess;
        if (pence_market) {
            // Fractional pence are preserved (e.g. boards sell at 0.5d each).
            double pence_earned = excess * unit_value;
            state.add("silver_pence", pence_earned);
            result.sale_gold[res] = pence_earned / kPencePerGold;
        } else {
            double sale_gold = excess * unit_value;
            state.add("gold", sale_gold);
            result.sale_gold[res] = sale_gold;
        }
    }

    // Capture net production for the report: produced minus consumed for each
    // commodity resource (imports count as consumption, so a resource bought
    // via imports more than produced shows a negative net). Currency is
    // normalized to a single decimal-gold figure: the day's net change in gold
    // plus the silver_pence change converted at kPencePerGold.
    result.net = resource_state();
    static const char* all_resources[] = {
        "gold", "silver_pence", "grain", "wood", "steel", "bronze",
        "leather", "mana", "charcoal", "iron", "ironwork", "fancy_ironwork",
        "beams", "boards",
    };
    for (const char* res : all_resources) {
        if (res == std::string("gold") || res == std::string("silver_pence")) {
            continue;
        }
        result.net.set(res, result.produced[res] - result.consumed[res]);
    }
    result.net.set("gold", (state.get("gold") - start.get("gold"))
                               + (state.get("silver_pence") - start.get("silver_pence"))
                                     / kPencePerGold);
    result.net.set("silver_pence", 0.0);

    return result;
}
