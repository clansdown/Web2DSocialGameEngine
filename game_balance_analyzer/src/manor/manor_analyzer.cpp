#include "manor/manor_analyzer.hpp"
#include "fmt.hpp"
#include "money.hpp"

#include <algorithm>
#include <unordered_map>

using json = nlohmann::json;

manor_analyzer::manor_analyzer(const config_loader& loader)
    : loader_(loader),
      economy_(registry_, loader.economy()) {
    registry_.load(loader_.building_types());
}

namespace {

// Formats a double compactly (comma-grouped, never scientific).
std::string fmt(double v) { return nfmt::format_number(v); }

// Import price of a resource in gold from the normalized map (0 if absent).
double import_price_gold(const std::string& res,
                         const std::unordered_map<std::string, double>& import_prices_gold) {
    auto it = import_prices_gold.find(res);
    return it != import_prices_gold.end() ? it->second : 0.0;
}

// Export price of a resource in gold, resolved by precedence:
//   export_prices[res] -> export_sell_multipliers[res] * import ->
//   export_sell_multiplier * import.
double export_price_gold(const std::string& res,
                         const std::unordered_map<std::string, double>& import_prices_gold,
                         const json& economy_json) {
    const auto& exports = economy_json.value("export_prices", json::object());
    if (exports.is_object() && exports.contains(res)) {
        const auto& e = exports[res];
        if (e.is_number()) {
            return e.get<double>();
        }
        if (e.is_object()) {
            return money::money_object_to_gold(e);
        }
    }
    double import = import_price_gold(res, import_prices_gold);
    const auto& mults = economy_json.value("export_sell_multipliers", json::object());
    if (mults.is_object() && mults.contains(res)) {
        return mults[res].get<double>() * import;
    }
    return economy_json.value("export_sell_multiplier", 0.5) * import;
}

} // namespace

balance_report manor_analyzer::analyze() const {
    balance_report report;
    if (registry_.empty()) {
        report.add_critical("Empty building registry",
                            "fiefdom_building_types.json did not parse or was empty.");
        return report;
    }

    // Build gold import-price map for cost normalization.
    std::unordered_map<std::string, double> import_prices_gold;
    const auto& economy_json = loader_.economy();
    if (economy_json.contains("import_prices") && economy_json["import_prices"].is_object()) {
        for (auto it = economy_json["import_prices"].begin(); it != economy_json["import_prices"].end(); ++it) {
            if (it.value().is_number()) {
                import_prices_gold[it.key()] = it.value().get<double>();
            } else if (it.value().is_object()) {
                import_prices_gold[it.key()] = money::money_object_to_gold(it.value());
            }
        }
    }

    // 1) Per-building: gold-normalized level-1 cost and input-aware daily net.
    //    Outputs are valued at their export price (the cash realized when the
    //    fief sells surplus); inputs and daily upkeep at import price. This is
    //    an informational snapshot — the coordinated production-network report
    //    ("Manor production network" section) is the authoritative view.
    struct efficiency {
        std::string id;
        double cost = 0.0;
        double daily_net = 0.0;       // outputs at export, inputs/daily at import
        double daily_net_import = 0.0;  // everything at import price
        double payback_days = 0.0;
    };
    std::vector<efficiency> effs;
    for (const auto& b : registry_.all()) {
        if (b.id() == "road" || b.id() == "head_race" || b.id() == "tail_race" ||
            b.id() == "mill_pond") {
            // Infrastructure: no direct production worth scoring here.
            continue;
        }
        efficiency e;
        e.id = b.id();
        e.cost = b.gold_normalized_cost(import_prices_gold);

        // Daily net value = sum(outputs * export price) - sum(inputs * import)
        // - sum(daily_cost * import).
        double out_val = 0.0;         // outputs at export price
        double out_val_import = 0.0;  // outputs at import price
        for (const auto& [res, amt] : b.outputs_at(1)) {
            double price = (res == "gold") ? 1.0 : export_price_gold(res, import_prices_gold, economy_json);
            double price_import = (res == "gold") ? 1.0 : import_price_gold(res, import_prices_gold);
            out_val += amt * price;
            out_val_import += amt * price_import;
        }
        double in_val = 0.0;
        for (const auto& [res, amt] : b.inputs_at(1)) {
            double price = (res == "gold") ? 1.0 : import_price_gold(res, import_prices_gold);
            in_val += amt * price;
        }
        double cost_val = 0.0;
        for (const auto& [res, amt] : b.daily_cost()) {
            double price = (res == "gold") ? 1.0 : import_price_gold(res, import_prices_gold);
            cost_val += amt * price;
        }
        e.daily_net = out_val - in_val - cost_val;
        e.daily_net_import = out_val_import - in_val - cost_val;
        if (e.daily_net_import > 0.0) {
            e.payback_days = e.cost / e.daily_net_import;
        }
        effs.push_back(e);

        std::ostringstream det;
        det << "gold_cost=" << fmt(e.cost)
            << " daily_net(import)=" << fmt(e.daily_net_import)
            << " daily_net(export)=" << fmt(e.daily_net)
            << " payback=" << (e.daily_net_import > 0.0 ? fmt(e.payback_days) + "d" : "inf");
        report.add_info("Building " + b.id(), det.str(), b.id());
    }

    // 2) Flag buildings whose payback is very long (> 100 days). Converters
    //    (buildings with inputs) are skipped: their standalone daily net is
    //    intentionally negative at level 1, and their real value is captured by
    //    the production-network analysis.
    for (const auto& e : effs) {
        if (e.cost > 0.0 && e.payback_days > 100.0) {
            report.add_warning("Very long payback for " + e.id,
                               "Payback period exceeds 100 days; may be underpowered.",
                               e.id);
        }
    }

    // 3) The standalone snapshot above is superseded for balance purposes by
    //    the coordinated production-network report (inputs + export prices +
    //    per-stage ratios), rendered in the "Manor production network" section.
    report.add_info("Production network",
                    "See 'Manor production network' section for the input/export-aware "
                    "ratio analysis and per-stage progression.",
                    "network");

    // 4) Import/export pricing sanity for tradable resources.
    const auto& prices = economy_json.value("import_prices", json::object());
    const auto& exports = economy_json.value("export_prices", json::object());
    const auto& sell_mults = economy_json.value("export_sell_multipliers", json::object());
    for (auto it = prices.begin(); it != prices.end(); ++it) {
        std::string res = it.key();
        double import = 0.0;
        if (it.value().is_number()) {
            import = it.value().get<double>();
        } else if (it.value().is_object()) {
            import = money::money_object_to_gold(it.value());
        }
        double export_price = 0.0;
        if (exports.is_object() && exports.contains(res)) {
            const auto& e = exports[res];
            export_price = e.is_number() ? e.get<double>()
                                         : money::money_object_to_gold(e);
        } else if (sell_mults.is_object() && sell_mults.contains(res)) {
            export_price = sell_mults[res].get<double>() * import;
        } else {
            export_price = economy_json.value("export_sell_multiplier", 0.5) * import;
        }
        // A sell price above import price is an arbitrage red flag.
        if (export_price > import * 0.999 && import > 0.0) {
            report.add_warning("Possible arbitrage on " + res,
                               "Export price (" + fmt(export_price)
                                   + ") exceeds import price (" + fmt(import)
                                   + "); a player could profit by trading.",
                               res);
        }
    }

    // 5) Prerequisite reachability: every building's prerequisite building must
    // exist in the registry (chain-aware satisfied by at least one known type).
    for (const auto& b : registry_.all()) {
        const auto& raw = b.raw();
        if (!raw.contains("prerequisites") || !raw["prerequisites"].is_array()) {
            continue;
        }
        for (const auto& prereq : raw["prerequisites"]) {
            if (!prereq.is_object()) {
                continue;
            }
            for (auto pit = prereq.begin(); pit != prereq.end(); ++pit) {
                const std::string& res = pit.key();
                if (res == "manor_level") {
                    continue;
                }
                if (registry_.find(res) == nullptr) {
                    // Chain-aware: the prereq may target a base stage satisfied
                    // by some higher stage in the registry.
                    bool ok = false;
                    for (const auto& other : registry_.all()) {
                        if (registry_.satisfies(other.id(), res)) {
                            ok = true;
                            break;
                        }
                    }
                    if (!ok) {
                        report.add_critical("Unresolvable prerequisite",
                                            "Building '" + b.id() + "' requires '" + res
                                                + "' which does not exist.",
                                            b.id());
                    }
                }
            }
        }
    }

    // 6) Manor-level gating: check for gaps (buildings that require a manor
    // level with no building unlocking at lower levels is not itself a problem,
    // but we flag any building whose manor_level requirement exceeds a sane cap).
    for (const auto& b : registry_.all()) {
        int req = b.manor_level_requirement();
        if (req > 32) {
            report.add_warning("Excessive manor-level requirement",
                               "Building '" + b.id() + "' requires manor_level " + nfmt::format_int(req)
                                   + " but the manor house caps at level 32.",
                               b.id());
        }
    }

    return report;
}
