#include "OngoingRewards.hpp"
#include <algorithm>
#include <iostream>

namespace ongoing_rewards {

int compute_base_reward_pence(const nlohmann::json& ongoing_config, int difficulty, int size) {
    if (ongoing_config.is_null()) return 0;

    int table_reward = 0;
    if (ongoing_config.contains("size_options") && ongoing_config["size_options"].is_array()) {
        for (const auto& opt : ongoing_config["size_options"]) {
            if (opt.value("value", -1) == size) {
                table_reward = opt.value("reward_pence", 0);
                break;
            }
        }
    }

    int coeff = ongoing_config.value("difficulty_coeff_pence", 0);
    if (difficulty < 1) difficulty = 1;

    return table_reward + coeff * (difficulty - 1);
}

bool validate_ongoing_options(const nlohmann::json& ongoing_config, int difficulty, int size) {
    if (ongoing_config.is_null()) return false;

    bool diff_ok = false;
    if (ongoing_config.contains("difficulty_options") && ongoing_config["difficulty_options"].is_array()) {
        for (const auto& d : ongoing_config["difficulty_options"]) {
            if (d.is_number_integer() && d.get<int>() == difficulty) {
                diff_ok = true;
                break;
            }
        }
    } else {
        diff_ok = true;
    }

    bool size_ok = false;
    if (ongoing_config.contains("size_options") && ongoing_config["size_options"].is_array()) {
        for (const auto& s : ongoing_config["size_options"]) {
            if (s.is_object() && s.value("value", -1) == size) {
                size_ok = true;
                break;
            }
        }
    } else {
        size_ok = true;
    }

    return diff_ok && size_ok;
}

std::string format_silver_pence(int pence, const nlohmann::json& currency_config) {
    if (pence < 0) pence = 0;

    int per_shilling = currency_config.value("pence_per_shilling", 12);
    int shillings_per_pound = currency_config.value("shillings_per_pound", 20);
    int per_pound = per_shilling * shillings_per_pound;

    int pounds = pence / per_pound;
    int rem = pence % per_pound;
    int shillings = rem / per_shilling;
    int p = rem % per_shilling;

    std::string out;
    if (pounds > 0) {
        out = "\xC2\xA3" + std::to_string(pounds) + " " + std::to_string(shillings) + "s " + std::to_string(p) + "d";
    } else if (shillings > 0) {
        out = std::to_string(shillings) + "s " + std::to_string(p) + "d";
    } else {
        out = std::to_string(p) + "d";
    }
    return out;
}

PoolState effective_pool_state(sqlite::database& db, int character_id,
                               const nlohmann::json& cfg, int64_t now) {
    int full_max = cfg.value("full_pool_max", 15);
    int half_max = cfg.value("half_pool_max", 5);
    int per_day = cfg.value("replenish_per_day", 5);

    bool found = false;
    int full = full_max;
    int half = half_max;
    int64_t last = 0;
    try {
        db << "SELECT full_pool, half_pool, last_consumed_at FROM reward_pools WHERE character_id = ?;"
           << character_id
           >> [&](int f, int h, int64_t l) {
                full = f;
                half = h;
                last = l;
                found = true;
            };
    } catch (const std::exception& e) {
        std::cerr << "[RewardPool] effective_pool_state read failed: " << e.what() << std::endl;
        return { full_max, half_max };
    }

    if (!found) {
        return { full_max, half_max };
    }

    if (last > 0 && now > last) {
        int64_t elapsed = now - last;
        int64_t replenished = elapsed * per_day / DAY_SECONDS;
        if (replenished > 0) {
            full = std::min(full_max, full + static_cast<int>(replenished));
            half = std::min(half_max, half + static_cast<int>(replenished));
        }
    }

    return { full, half };
}

ConsumeResult consume_pool(sqlite::database& db, int character_id, int base_pence,
                           const nlohmann::json& cfg, int64_t now) {
    int full_max = cfg.value("full_pool_max", 15);
    int half_max = cfg.value("half_pool_max", 5);

    PoolState eff = effective_pool_state(db, character_id, cfg, now);

    double multiplier = 1.0;
    std::string tier = "full";
    int new_full = eff.full_pool;
    int new_half = eff.half_pool;

    if (eff.full_pool > 0) {
        new_full = eff.full_pool - 1;
        multiplier = 1.0;
        tier = "full";
    } else if (eff.half_pool > 0) {
        new_half = eff.half_pool - 1;
        multiplier = cfg.value("half_multiplier", 0.5);
        tier = "half";
    } else {
        multiplier = cfg.value("quarter_multiplier", 0.25);
        tier = "quarter";
    }

    int reward = static_cast<int>(base_pence * multiplier);
    if (multiplier < 1.0) {
        int min_reward = cfg.value("min_reward_pence", 1);
        if (reward < min_reward) reward = min_reward;
    }

    try {
        db << "INSERT OR IGNORE INTO reward_pools (character_id, full_pool, half_pool, last_consumed_at) "
              "VALUES (?, ?, ?, ?);"
           << character_id << full_max << half_max << 0;
        db << "UPDATE reward_pools SET full_pool = ?, half_pool = ?, last_consumed_at = ? WHERE character_id = ?;"
           << new_full << new_half << now << character_id;
    } catch (const std::exception& e) {
        std::cerr << "[RewardPool] consume_pool write failed: " << e.what() << std::endl;
    }

    return { reward, multiplier, tier };
}

} // namespace ongoing_rewards
