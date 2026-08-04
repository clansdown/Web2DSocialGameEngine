#pragma once
#include <nlohmann/json.hpp>

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
 * @param silver_pence - In/out: pence balance, normalized to 0..239
 */
void normalize(double& gold, int& silver_pence);

/**
 * Converts any object-form money cost entry into a gold double, in place.
 * Used by config post-processing so all cost readers see plain numbers.
 * Accepts { "gold": x, "shillings": y, "pence": z } (all optional).
 *
 * @param cfg - The loaded config (buildings array or wall_config object)
 */
void normalize_money_costs(nlohmann::json& cfg);

} // namespace money
