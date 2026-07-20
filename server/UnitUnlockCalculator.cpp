#include "UnitUnlockCalculator.hpp"
#include "GameConfigCache.hpp"
#include <iostream>

void UnitUnlockCalculator::grant_starting_unlocks(
    GameConfigCache& config_cache,
    sqlite::database& db,
    int character_id,
    int64_t timestamp)
{
    // Check if character already has any unlocks
    int existing = 0;
    db << "SELECT COUNT(*) FROM td_player_unlocks WHERE character_id = ?;"
       << character_id
       >> [&](int count) { existing = count; };

    if (existing > 0) {
        return; // Already granted
    }

    const auto& unlocks_config = config_cache.getTowerDefenseUnitUnlocks();

    if (!unlocks_config.contains("starting")) {
        return;
    }

    const auto& starting = unlocks_config["starting"];

    if (starting.contains("units")) {
        for (const auto& unit_id : starting["units"]) {
            db << "INSERT OR IGNORE INTO td_player_unlocks "
                  "(character_id, item_type, item_id, unlocked_at) "
                  "VALUES (?, 'unit', ?, ?);"
               << character_id << unit_id.get<std::string>() << timestamp;
        }
    }

    if (starting.contains("towers")) {
        for (const auto& tower_id : starting["towers"]) {
            db << "INSERT OR IGNORE INTO td_player_unlocks "
                  "(character_id, item_type, item_id, unlocked_at) "
                  "VALUES (?, 'tower', ?, ?);"
               << character_id << tower_id.get<std::string>() << timestamp;
        }
    }
}

nlohmann::json UnitUnlockCalculator::get_player_unlocks(
    sqlite::database& db,
    int character_id)
{
    nlohmann::json result;
    result["units"] = nlohmann::json::array();
    result["towers"] = nlohmann::json::array();

    db << "SELECT item_type, item_id FROM td_player_unlocks "
          "WHERE character_id = ? ORDER BY item_type, item_id;"
       << character_id
       >> [&](std::string item_type, std::string item_id) {
            if (item_type == "unit") {
                result["units"].push_back(item_id);
            } else if (item_type == "tower") {
                result["towers"].push_back(item_id);
            }
        };

    return result;
}

nlohmann::json UnitUnlockCalculator::check_and_grant_milestones(
    GameConfigCache& config_cache,
    sqlite::database& db,
    int character_id,
    int completed_count,
    int64_t timestamp)
{
    nlohmann::json new_unlocks;
    new_unlocks["new_units"] = nlohmann::json::array();
    new_unlocks["new_towers"] = nlohmann::json::array();

    const auto& unlocks_config = config_cache.getTowerDefenseUnitUnlocks();

    if (!unlocks_config.contains("milestones")) {
        return new_unlocks;
    }

    for (const auto& milestone : unlocks_config["milestones"]) {
        const auto& condition = milestone["condition"];
        std::string condition_type = condition.value("type", "");

        bool met = false;

        if (condition_type == "levels_completed") {
            int required = condition.value("count", 0);
            met = (completed_count >= required);
        }

        if (!met) continue;

        // Check if reward items are already unlocked
        std::vector<std::string> to_unlock_units;
        std::vector<std::string> to_unlock_towers;

        auto check_item = [&](const std::string& item_type, const std::string& item_id) {
            int exists = 0;
            db << "SELECT COUNT(*) FROM td_player_unlocks "
                  "WHERE character_id = ? AND item_type = ? AND item_id = ?;"
               << character_id << item_type << item_id
               >> [&](int count) { exists = count; };

            if (exists == 0) {
                if (item_type == "unit") to_unlock_units.push_back(item_id);
                else if (item_type == "tower") to_unlock_towers.push_back(item_id);
            }
        };

        if (milestone["reward"].contains("units")) {
            for (const auto& unit_id : milestone["reward"]["units"]) {
                check_item("unit", unit_id.get<std::string>());
            }
        }

        if (milestone["reward"].contains("towers")) {
            for (const auto& tower_id : milestone["reward"]["towers"]) {
                check_item("tower", tower_id.get<std::string>());
            }
        }

        // Grant any newly discovered items
        for (const auto& unit_id : to_unlock_units) {
            db << "INSERT OR IGNORE INTO td_player_unlocks "
                  "(character_id, item_type, item_id, unlocked_at) "
                  "VALUES (?, 'unit', ?, ?);"
               << character_id << unit_id << timestamp;

            nlohmann::json item;
            item["id"] = unit_id;
            item["text_key"] = "ui_unit_" + unit_id;
            new_unlocks["new_units"].push_back(item);
        }

        for (const auto& tower_id : to_unlock_towers) {
            db << "INSERT OR IGNORE INTO td_player_unlocks "
                  "(character_id, item_type, item_id, unlocked_at) "
                  "VALUES (?, 'tower', ?, ?);"
               << character_id << tower_id << timestamp;

            nlohmann::json titem;
            titem["id"] = tower_id;
            titem["text_key"] = "ui_tower_" + tower_id;
            new_unlocks["new_towers"].push_back(titem);
        }
    }

    return new_unlocks;
}
