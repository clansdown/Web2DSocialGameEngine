#pragma once
#include <nlohmann/json.hpp>
#include <sqlite_modern_cpp.h>
#include <string>

namespace ongoing_rewards {

constexpr int64_t DAY_SECONDS = 86400;

/**
 * Computes the base silver reward (in pence) for an ongoing mini-game at the
 * given difficulty and size. The size reward comes from the config's explicit
 * size table; difficulty adds a per-level coefficient on top of the difficulty-1
 * base. Formula: reward = size_reward_pence + difficulty_coeff_pence * (difficulty - 1).
 *
 * @param ongoing_config - The game's ongoing.json config
 * @param difficulty - Chosen difficulty (1..n, per difficulty_options)
 * @param size - Chosen size (rounds for TD, grid size for weeding)
 * @returns int - Reward in silver pence
 */
int compute_base_reward_pence(const nlohmann::json& ongoing_config, int difficulty, int size);

/**
 * Validates that the given difficulty and size are offered by the ongoing
 * config (present in difficulty_options and size_options). Used to reject
 * client-supplied settings outside the server's configured availability.
 *
 * @param ongoing_config - The game's ongoing.json config
 * @param difficulty - Difficulty to check
 * @param size - Size to check
 * @returns bool - True if both values are offered by the config
 */
bool validate_ongoing_options(const nlohmann::json& ongoing_config, int difficulty, int size);

/**
 * Formats a silver amount (pence) using the old-English system configured in
 * economy.json: 6 pence to a shilling, 20 shillings to a pound.
 * Produces e.g. "1d", "2s 6d", or "£1 3s 4d".
 *
 * @param pence - Amount in silver pence
 * @param currency_config - The "currency" block of economy.json
 * @returns std::string - Old-English formatted amount
 */
std::string format_silver_pence(int pence, const nlohmann::json& currency_config);

struct PoolState {
    int full_pool;
    int half_pool;
};

/**
 * Returns the character's effective reward pool state at `now`, lazily
 * replenishing both pools from time elapsed since last_consumed_at at the
 * configured per-day rate (rounded down, capped at pool maxima). Read-only —
 * never writes to the database.
 *
 * @param db - Game database
 * @param character_id - Character whose pool to inspect
 * @param reward_pools_config - The "reward_pools" block of economy.json
 * @param now - Current unix timestamp
 * @returns PoolState - Effective full/half pool counts
 */
PoolState effective_pool_state(sqlite::database& db, int character_id,
                               const nlohmann::json& reward_pools_config, int64_t now);

struct ConsumeResult {
    int reward_pence;
    double multiplier;
    std::string tier; // "full" | "half" | "quarter"
};

/**
 * Consumes one reward draw from the character's shared pool (used when an
 * ongoing mini-game is won). Replenishes lazily from elapsed time, picks the
 * tier (full > half > quarter), applies the multiplier (rounded down, with a
 * configured minimum for reduced tiers), decrements the pool, and persists
 * last_consumed_at = now. This is the only place pool state is written.
 *
 * @param db - Game database
 * @param character_id - Character earning the reward
 * @param base_pence - Base reward in pence before diminishing returns
 * @param reward_pools_config - The "reward_pools" block of economy.json
 * @param now - Current unix timestamp
 * @returns ConsumeResult - Final reward, multiplier, and tier used
 */
ConsumeResult consume_pool(sqlite::database& db, int character_id, int base_pence,
                           const nlohmann::json& reward_pools_config, int64_t now);

} // namespace ongoing_rewards
