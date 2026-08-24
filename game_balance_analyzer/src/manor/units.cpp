#include "manor/units.hpp"
#include "fmt.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

using json = nlohmann::json;

namespace {

constexpr double kPencePerGold = 240.0;

// Shared number formatting (comma-grouped, never scientific).
std::string fmt(double v) { return nfmt::format_number(v); }

// Gold value of a money object {gold, shillings, pence}.
double money_object_to_gold(const json& obj) {
    return obj.value("gold", 0.0) + obj.value("shillings", 0.0) / 20.0
           + obj.value("pence", 0.0) / kPencePerGold;
}

// Formats a silver-pence value compactly as shillings and pence
// (12 pence/shillng, 20 shillings/pound, 240 pence/gold), e.g. 208 -> "17s 4d",
// 12 -> "1s", 6 -> "6d", 0 -> "0s". Negatives get a "-" prefix.
std::string fmt_silver(double pence) {
    if (pence == 0.0) {
        return "0s";
    }
    bool neg = pence < 0.0;
    long long p = std::llround(std::fabs(pence));
    long long sh = p / 12;
    long long d = p % 12;
    std::string out;
    if (neg) {
        out += "-";
    }
    if (sh > 0 && d > 0) {
        out += nfmt::format_int(sh) + "s " + nfmt::format_int(d) + "d";
    } else if (sh > 0) {
        out += nfmt::format_int(sh) + "s";
    } else {
        out += nfmt::format_int(d) + "d";
    }
    return out;
}

}  // namespace

manor_units_analyzer::manor_units_analyzer(const config_loader& loader)
    : loader_(loader), economy_(loader.economy()) {
    registry_.load(loader_.building_types());

    // Build the gold-equivalent import-price map for valuation.
    const auto& prices = economy_.value("import_prices", json::object());
    if (prices.is_object()) {
        for (auto it = prices.begin(); it != prices.end(); ++it) {
            if (it.value().is_number()) {
                import_prices_gold_[it.key()] = it.value().get<double>();
            } else if (it.value().is_object()) {
                import_prices_gold_[it.key()] = money_object_to_gold(it.value());
            }
        }
    }
}

double manor_units_analyzer::import_price_gold(const std::string& res) const {
    auto it = import_prices_gold_.find(res);
    return it != import_prices_gold_.end() ? it->second : 0.0;
}

double manor_units_analyzer::export_price_gold(const std::string& res) const {
    const auto& exports = economy_.value("export_prices", json::object());
    if (exports.is_object() && exports.contains(res)) {
        const auto& e = exports[res];
        return e.is_number() ? e.get<double>() : money_object_to_gold(e);
    }
    double import = import_price_gold(res);
    const auto& mults = economy_.value("export_sell_multipliers", json::object());
    if (mults.is_object() && mults.contains(res) && mults[res].is_number()) {
        return mults[res].get<double>() * import;
    }
    return economy_.value("export_sell_multiplier", 0.5) * import;
}

bool manor_units_analyzer::is_penny_market(const std::string& res) const {
    const auto& prices = economy_.value("import_prices", json::object());
    return prices.is_object() && prices.contains(res) && prices[res].is_object();
}

std::pair<double, double> manor_units_analyzer::split_value(
    const std::string& res, double amount, double price_gold) const {
    if (res == "gold") {
        return {amount, 0.0};
    }
    if (res == "silver_pence") {
        return {0.0, amount};
    }
    if (is_penny_market(res)) {
        return {0.0, amount * price_gold * kPencePerGold};
    }
    return {amount * price_gold, 0.0};
}

double manor_units_analyzer::cumulative_build_cost(const building_type& b,
                                                   int level) const {
    static const char* res_names[] = {
        "gold", "silver_pence", "grain", "wood", "steel", "bronze",
        "leather", "mana", "charcoal", "iron", "ironwork", "fancy_ironwork",
        "beams", "boards",
    };
    double total = 0.0;
    for (const char* rn : res_names) {
        const std::string key = std::string(rn) + "_cost";
        if (!b.raw().contains(key)) {
            continue;
        }
        const auto& val = b.raw()[key];
        double amount = 0.0;
        if (val.is_array()) {
            // Per-level array: reach level `level` by paying levels 1..level.
            for (int l = 1; l <= level; ++l) {
                amount += amount_at_level(val, l);
            }
        } else {
            // Single (non-level-scaled) cost, paid once.
            amount = amount_at_level(val, 1);
        }
        if (amount == 0.0) {
            continue;
        }
        if (std::string(rn) == "gold") {
            total += amount;
        } else if (std::string(rn) == "silver_pence") {
            total += amount / kPencePerGold;
        } else {
            total += amount * import_price_gold(rn);
        }
    }
    return total;
}

std::vector<unit_building> manor_units_analyzer::analyze() const {
    // Pure infrastructure has no production economics to score.
    static const std::set<std::string> kInfrastructure = {
        "road", "head_race", "tail_race", "mill_pond",
    };

    std::vector<unit_building> result;
    for (const auto& b : registry_.all()) {
        if (kInfrastructure.count(b.id())) {
            continue;
        }
        unit_building ub;
        ub.building_id = b.id();
        ub.display_name = b.display_name();

        for (int level = 1; level <= b.max_level(); ++level) {
            unit_row r;
            r.level = level;

            // Collect per-resource production, daily cost, and inputs.
            std::unordered_map<std::string, double> produced;
            for (const auto& [res, amt] : b.outputs_at(level)) {
                produced[res] += amt;
            }
            std::unordered_map<std::string, double> cost;
            for (const auto& [res, amt] : b.daily_cost()) {
                cost[res] += amt;
            }
            auto inputs = b.inputs_at(level);

            // Produce-first: a building's own output is calculated before daily
            // costs, so it first covers its own daily cost (that portion nets to
            // zero). Only the surplus is saleable income at export price, and
            // only the daily cost not covered by own production is paid from
            // outside at import price.
            for (const auto& [res, amt] : produced) {
                if (amt > 0.0) {
                    ub.produces = true;
                }
                double cover = std::min(amt, cost[res]);
                double surplus = amt - cover;
                double external_cost = cost[res] - cover;
                double price = (res == "gold") ? 1.0 : export_price_gold(res);
                auto [g, s] = split_value(res, surplus, price);
                r.gross_gold += g;
                r.gross_silver += s;
                double iprice = (res == "gold") ? 1.0 : import_price_gold(res);
                auto [cg, cs] = split_value(res, external_cost, iprice);
                r.cost_gold += cg;
                r.cost_silver += cs;
            }
            // Daily-cost resources that are not produced at all are fully paid
            // from outside at import price.
            for (const auto& [res, amt] : cost) {
                if (produced.count(res)) {
                    continue;  // already handled above
                }
                double iprice = (res == "gold") ? 1.0 : import_price_gold(res);
                auto [cg, cs] = split_value(res, amt, iprice);
                r.cost_gold += cg;
                r.cost_silver += cs;
            }

            // Minus production inputs at import price.
            for (const auto& [res, amt] : inputs) {
                double price = (res == "gold") ? 1.0 : import_price_gold(res);
                auto [g, s] = split_value(res, amt, price);
                r.input_gold += g;
                r.input_silver += s;
            }

            r.net_gold = r.gross_gold - r.input_gold - r.cost_gold;
            r.net_silver = r.gross_silver - r.input_silver - r.cost_silver;
            r.build_cost_gold = cumulative_build_cost(b, level);

            double net_equiv = r.net_gold + r.net_silver / kPencePerGold;
            if (net_equiv > 0.0 && r.build_cost_gold > 0.0) {
                r.payback_days = r.build_cost_gold / net_equiv;
            } else {
                r.payback_days = -1.0;
            }

            ub.levels.push_back(std::move(r));
        }
        result.push_back(std::move(ub));
    }
    return result;
}

std::string manor_units_analyzer::render(
    const std::vector<unit_building>& units) const {
    std::ostringstream out;
    out << "Manor units analysis (every building x every level; all inputs/costs "
           "imported; outputs at export price; no modifiers)\n";

    // Buildings that produce nothing are summarized once instead of listed per
    // level (gross = saleable surplus, so classification uses the raw
    // production flag, not the surplus value).
    std::vector<std::string> no_production;
    for (const auto& ub : units) {
        if (!ub.produces) {
            no_production.push_back(ub.building_id + " (" + ub.display_name + ")");
            continue;
        }
        out << "\n" << ub.building_id << "  (" << ub.display_name << ")\n";
        for (const auto& r : ub.levels) {
            out << "  L=" << nfmt::format_int(r.level)
                << "  gross(g=" << fmt(r.gross_gold) << ",s=" << fmt_silver(r.gross_silver) << ")"
                << "  input(g=" << fmt(r.input_gold) << ",s=" << fmt_silver(r.input_silver) << ")"
                << "  upkeep(g=" << fmt(r.cost_gold) << ",s=" << fmt_silver(r.cost_silver) << ")"
                << "  net(g=" << fmt(r.net_gold) << ",s=" << fmt_silver(r.net_silver) << ")"
                << "  build=" << fmt(r.build_cost_gold) << "g"
                << "  payback=" << (r.payback_days >= 0.0 ? fmt(r.payback_days) : "never")
                << "d\n";
        }
    }

    if (!no_production.empty()) {
        out << "\nNo production (no outputs):";
        for (const auto& id : no_production) {
            out << "  " << id;
        }
        out << "\n";
    }
    return out.str();
}

json manor_units_analyzer::render_json(
    const std::vector<unit_building>& units) const {
    json arr = json::array();
    for (const auto& ub : units) {
        json entry;
        entry["building_id"] = ub.building_id;
        entry["display_name"] = ub.display_name;
        json levels = json::array();
        for (const auto& r : ub.levels) {
            levels.push_back({
                {"level", r.level},
                {"gross_gold", r.gross_gold},
                {"gross_silver", r.gross_silver},
                {"input_gold", r.input_gold},
                {"input_silver", r.input_silver},
                {"cost_gold", r.cost_gold},
                {"cost_silver", r.cost_silver},
                {"net_gold", r.net_gold},
                {"net_silver", r.net_silver},
                {"build_cost_gold", r.build_cost_gold},
                {"payback_days", r.payback_days},
            });
        }
        entry["levels"] = std::move(levels);
        arr.push_back(std::move(entry));
    }
    return arr;
}
