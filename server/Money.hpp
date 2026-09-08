#pragma once
#include <nlohmann/json.hpp>
#include <map>

namespace money {

// Medieval English currency ratios (match the "currency" block in economy.json).
// 1 gold piece (£) = 20 silver shillings = 240 silver pence; 1 shilling = 12 pence.
constexpr double pence_per_shilling = 12.0;
constexpr double shillings_per_pound = 20.0;
constexpr double pence_per_gold = pence_per_shilling * shillings_per_pound;

/**
 * Converts a fractional gold amount to the nearest whole silver pence.
 *
 * @param gold - Amount in gold pieces (fractional allowed)
 * @returns long - Equivalent amount in silver pence (rounded)
 */
long gold_to_pence(double gold);

/**
 * Normalizes a (gold, silver_pence) money pool into canonical form: gold holds
 * only whole gold pieces and silver_pence holds the 0..239 pence remainder.
 * Silver is stored as fractional gold, so this only affects storage/display.
 *
 * @param gold - In/out: gold pieces (carries any silver overflow, discards the
 *               fractional pence remainder)
 * @param silver_pence - In/out: pence balance (fractional-capable), normalized
 *               to 0..239
 */
void normalize(double& gold, double& silver_pence);

/**
 * Converts any object-form money cost entry into a gold double, in place.
 * Used by config post-processing so all cost readers see plain numbers.
 * Accepts { "gold": x, "shillings": y, "pence": z } (all optional).
 *
 * @param cfg - The loaded config (buildings array or wall_config object)
 */
void normalize_money_costs(nlohmann::json& cfg);

// ── Generic fungible-money layer ────────────────────────────────────────────
// One source of truth for "how much does this cost", "what money do we have",
// and "pay it" — with silver/gold treated as a single convertible wallet
// (pence_per_gold from the "currency" block in economy.json). All server money
// movement (build actions, economy imports/exports, hire fees, future gear)
// routes through these helpers.

// Config-driven currency ratios (mirror economy.json "currency").
struct currency {
    double pence_per_shilling = 12.0;
    double shillings_per_pound = 20.0;
    double pence_per_gold = 240.0;
};

currency default_currency();
// Reads the "currency" sub-object from an economy config; falls back to the
// default (medieval) ratios on missing/partial blocks.
currency load_currency(const nlohmann::json& economy_cfg);

// The fiefdom's spendable money pool. gold and silver_pence are one wallet:
// a cost denominated in either can be paid from the other (converting at
// pence_per_gold).
struct wallet {
    double gold = 0.0;
    double silver_pence = 0.0;

    // Combined wealth in a single unit.
    double gold_equivalent(const currency& c = default_currency()) const;
    double pence_equivalent(const currency& c = default_currency()) const;
};

// Converts a money-object price entry ({gold, shillings, pence}) to pence or
// gold. Requires an object; plain-number entries are handled by callers
// (they are gold-denominated and passed through untouched).
double price_to_pence(const nlohmann::json& price, const currency& c);
double price_to_gold(const nlohmann::json& price, const currency& c);

// Pays a charge from the wallet, crossing currency boundaries at pence_per_gold:
//   - take_pence pays silver first, then converts gold -> pence.
//   - take_gold  pays gold first, then converts pence -> gold.
// Callers must guarantee the wallet covers the charge (see affordable/pay).
void take_pence(wallet& w, double amount_pence, const currency& c);
void take_gold(wallet& w, double amount_gold, const currency& c);

// Credits an amount into the wallet (independent of payment units — gold and
// pence are one pool at conversion time).
void add_pence(wallet& w, double amount_pence);
void add_gold(wallet& w, double amount_gold);

// Import/pricing context for affordable()/pay().
struct cost_context {
    const nlohmann::json* import_prices = nullptr;    // economy.json "import_prices"
    const nlohmann::json* import_settings = nullptr;  // fiefdom import_settings
    currency c = default_currency();
};

// True if the wallet + physical stock can cover the cost, given import prices
// and the per-resource import toggles. cost is a resource -> amount map;
// "gold"/"silver_pence" entries are wallet charges; other resources are drawn
// from stock and any shortfall is auto-imported with money at import price
// when the resource's import is enabled (un-importable shortfalls make the
// cost unaffordable).
bool affordable(const wallet& w, const std::map<std::string, double>& stock,
                const nlohmann::json& cost, const cost_context& ctx);

// All-or-nothing payment of cost from the fungible wallet + physical stock
// (auto-importing shortfalls per affordable()). Mutates w and stock on success.
bool pay(wallet& w, std::map<std::string, double>& stock,
         const nlohmann::json& cost, const cost_context& ctx);

} // namespace money
