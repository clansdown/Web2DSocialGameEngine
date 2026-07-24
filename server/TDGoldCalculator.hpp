#pragma once
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

struct GoldCalculationResult {
    int new_gold;
    int placement_cost;
    int refund;
    std::string error;
};

GoldCalculationResult calculateGoldForRound(
    int session_gold,
    int gold_earned,
    const json& old_placements,
    const json& new_placements,
    const json& towers_config,
    const json& units_config,
    const json& map_metadata);