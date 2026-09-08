#include "Money.hpp"
#include <algorithm>
#include <cmath>

namespace money {

long gold_to_pence(double gold) {
    if (gold <= 0) return 0;
    return static_cast<long>(std::llround(gold * pence_per_gold));
}

void normalize(double& gold, double& silver_pence) {
    double total_pence = gold_to_pence(gold) + silver_pence;
    if (total_pence < 0) total_pence = 0;
    gold = std::floor(total_pence / pence_per_gold);
    silver_pence = total_pence - gold * pence_per_gold;
}

// Convert a single gold_cost element (number or {gold, shillings, pence} object)
// into a gold double. Non-object entries (plain numbers) pass through unchanged.
static void normalize_gold_cost_entry(nlohmann::json& entry) {
    if (!entry.is_object()) return;
    double gold = entry.value("gold", 0.0);
    double shillings = entry.value("shillings", 0.0);
    double pence = entry.value("pence", 0.0);
    entry = gold + shillings / shillings_per_pound + pence / pence_per_gold;
}

// Normalize the gold_cost array of a single building or wall config object.
static void normalize_gold_cost_array(nlohmann::json& cfg) {
    if (!cfg.is_object() || !cfg.contains("gold_cost") || !cfg["gold_cost"].is_array()) {
        return;
    }
    for (auto& entry : cfg["gold_cost"]) {
        normalize_gold_cost_entry(entry);
    }
}

void normalize_money_costs(nlohmann::json& cfg) {
    // fiefdom_building_types.json: an array of { type_id: {...} } objects
    if (cfg.is_array()) {
        for (auto& building_entry : cfg) {
            if (!building_entry.is_object()) continue;
            for (auto& [type_id, building_cfg] : building_entry.items()) {
                (void)type_id;
                normalize_gold_cost_array(building_cfg);
            }
        }
        return;
    }

    // wall_config.json: { "walls": { "1": {...}, ... } }
    if (cfg.is_object() && cfg.contains("walls") && cfg["walls"].is_object()) {
        for (auto& [generation, wall_cfg] : cfg["walls"].items()) {
            (void)generation;
            normalize_gold_cost_array(wall_cfg);
        }
    }
}

currency default_currency() {
    return currency{};
}

currency load_currency(const nlohmann::json& economy_cfg) {
    const nlohmann::json currency_cfg = economy_cfg.value("currency", nlohmann::json::object());
    currency c;
    c.pence_per_shilling = currency_cfg.value("pence_per_shilling", c.pence_per_shilling);
    c.shillings_per_pound = currency_cfg.value("shillings_per_pound", c.shillings_per_pound);
    c.pence_per_gold = currency_cfg.value("pence_per_gold", c.pence_per_shilling * c.shillings_per_pound);
    return c;
}

double wallet::gold_equivalent(const currency& c) const {
    return gold + silver_pence / c.pence_per_gold;
}

double wallet::pence_equivalent(const currency& c) const {
    return gold * c.pence_per_gold + silver_pence;
}

double price_to_pence(const nlohmann::json& price, const currency& c) {
    if (!price.is_object()) return 0.0;
    return price.value("gold", 0.0) * c.pence_per_gold
         + price.value("shillings", 0.0) * c.pence_per_shilling
         + price.value("pence", 0.0);
}

double price_to_gold(const nlohmann::json& price, const currency& c) {
    return price_to_pence(price, c) / c.pence_per_gold;
}

void take_pence(wallet& w, double amount_pence, const currency& c) {
    if (amount_pence <= 0.0) return;
    if (w.silver_pence >= amount_pence) {
        w.silver_pence -= amount_pence;
        return;
    }
    double shortfall = amount_pence - w.silver_pence;
    w.silver_pence = 0.0;
    w.gold -= shortfall / c.pence_per_gold;
}

void take_gold(wallet& w, double amount_gold, const currency& c) {
    if (amount_gold <= 0.0) return;
    if (w.gold >= amount_gold) {
        w.gold -= amount_gold;
        return;
    }
    double shortfall = amount_gold - w.gold;
    w.gold = 0.0;
    w.silver_pence -= shortfall * c.pence_per_gold;
}

void add_pence(wallet& w, double amount_pence) {
    if (amount_pence > 0.0) w.silver_pence += amount_pence;
}

void add_gold(wallet& w, double amount_gold) {
    if (amount_gold > 0.0) w.gold += amount_gold;
}

namespace {

double stock_at(const std::map<std::string, double>& stock, const std::string& res) {
    auto it = stock.find(res);
    return (it != stock.end()) ? it->second : 0.0;
}

bool import_enabled(const nlohmann::json* import_settings, const std::string& res) {
    if (import_settings && import_settings->is_object() && import_settings->contains(res)) {
        return (*import_settings)[res].get<bool>();
    }
    return true;  // imports default on
}

} // namespace

bool affordable(const wallet& w, const std::map<std::string, double>& stock,
                const nlohmann::json& cost, const cost_context& ctx)
{
    double demand_gold = 0.0;
    for (auto& [res, amount] : cost.items()) {
        if (!amount.is_number() || amount.get<double>() <= 0.0) continue;
        const double amt = amount.get<double>();
        if (res == "gold") { demand_gold += amt; continue; }
        if (res == "silver_pence") { demand_gold += amt / ctx.c.pence_per_gold; continue; }

        const double shortfall = amt - stock_at(stock, res);
        if (shortfall <= 0.0) continue;

        // Cover the shortfall with money (auto-import) when enabled + priced.
        if (!import_enabled(ctx.import_settings, res)) return false;
        if (!ctx.import_prices || !ctx.import_prices->contains(res)) return false;
        const nlohmann::json& price = (*ctx.import_prices)[res];
        if (price.is_object()) {
            demand_gold += shortfall * price_to_pence(price, ctx.c) / ctx.c.pence_per_gold;
        } else if (price.is_number()) {
            demand_gold += shortfall * price.get<double>();
        } else {
            return false;
        }
    }
    return demand_gold <= w.gold_equivalent(ctx.c) + 1e-9;
}

bool pay(wallet& w, std::map<std::string, double>& stock,
         const nlohmann::json& cost, const cost_context& ctx)
{
    if (!affordable(w, stock, cost, ctx)) return false;

    for (auto& [res, amount] : cost.items()) {
        if (!amount.is_number()) continue;
        const double amt = amount.get<double>();
        if (amt <= 0.0) continue;
        if (res == "gold") { take_gold(w, amt, ctx.c); continue; }
        if (res == "silver_pence") { take_pence(w, amt, ctx.c); continue; }

        double& cur = stock[res];
        const double from_stock = std::min(cur, amt);
        cur -= from_stock;
        const double shortfall = amt - from_stock;
        if (shortfall <= 0.0) continue;

        // Auto-import the shortfall with money (affordable() already verified).
        if (!ctx.import_prices || !ctx.import_prices->contains(res)) continue;
        const nlohmann::json& price = (*ctx.import_prices)[res];
        if (price.is_object()) {
            take_pence(w, shortfall * price_to_pence(price, ctx.c), ctx.c);
        } else if (price.is_number()) {
            take_gold(w, shortfall * price.get<double>(), ctx.c);
        }
    }
    return true;
}

} // namespace money
