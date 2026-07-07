#include "GameConfigCache.hpp"
#include <fstream>
#include <iostream>

GameConfigCache& GameConfigCache::getInstance() {
    static GameConfigCache instance;
    return instance;
}

static std::string readFileToString(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

static void scaleTDPieceValues(nlohmann::json& pieces) {
    for (auto& [id, piece] : pieces.items()) {
        if (piece.contains("range") && piece["range"].is_number()) {
            piece["range"] = piece["range"].get<double>() / 100.0;
        }
        if (piece.contains("area_of_effect") && piece["area_of_effect"].is_object()) {
            auto& aoe = piece["area_of_effect"];
            if (aoe.contains("radius") && aoe["radius"].is_number()) {
                aoe["radius"] = aoe["radius"].get<double>() / 100.0;
            }
        }
    }
}

bool GameConfigCache::loadConfig(const std::string& path, const std::string& name, nlohmann::json& target) {
    std::string content = readFileToString(path);
    if (content.empty()) {
        std::cerr << "Failed to open config file: " << path << std::endl;
        return false;
    }

    try {
        target = nlohmann::json::parse(content, nullptr, true, true);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse " << name << ": " << e.what() << std::endl;
        return false;
    }
}

bool GameConfigCache::initialize(const std::string& config_dir) {
    bool success = true;

    success &= loadConfig(config_dir + "/damage_types.json", "damage_types", damage_types_);
    success &= loadConfig(config_dir + "/fiefdom_building_types.json", "fiefdom_building_types", fiefdom_building_types_);
    success &= loadConfig(config_dir + "/player_combatants.json", "player_combatants", player_combatants_);
    success &= loadConfig(config_dir + "/enemy_combatants.json", "enemy_combatants", enemy_combatants_);
    success &= loadConfig(config_dir + "/heroes.json", "heroes", heroes_);
    success &= loadConfig(config_dir + "/fiefdom_officials.json", "fiefdom_officials", fiefdom_officials_);
    success &= loadConfig(config_dir + "/wall_config.json", "wall_config", wall_config_);
    success &= loadConfig(config_dir + "/mini_games.json", "mini_games", mini_games_);
    success &= loadConfig(config_dir + "/tower_defense/mobs.json", "tower_defense_mobs", tower_defense_mobs_);
    success &= loadConfig(config_dir + "/tower_defense/towers.json", "tower_defense_towers", tower_defense_towers_);
    if (tower_defense_towers_.contains("towers")) {
        scaleTDPieceValues(tower_defense_towers_["towers"]);
    }
    success &= loadConfig(config_dir + "/tower_defense/units.json", "tower_defense_units", tower_defense_units_);
    if (tower_defense_units_.contains("units")) {
        scaleTDPieceValues(tower_defense_units_["units"]);
    }
    success &= loadConfig(config_dir + "/tower_defense/unit_unlocks.json", "tower_defense_unit_unlocks", tower_defense_unit_unlocks_);

    loaded_ = success;
    return success;
}

const nlohmann::json& GameConfigCache::getDamageTypes() const {
    return damage_types_;
}

const nlohmann::json& GameConfigCache::getFiefdomBuildingTypes() const {
    return fiefdom_building_types_;
}

const nlohmann::json& GameConfigCache::getPlayerCombatants() const {
    return player_combatants_;
}

const nlohmann::json& GameConfigCache::getEnemyCombatants() const {
    return enemy_combatants_;
}

const nlohmann::json& GameConfigCache::getHeroes() const {
    return heroes_;
}

const nlohmann::json& GameConfigCache::getFiefdomOfficials() const {
    return fiefdom_officials_;
}

const nlohmann::json& GameConfigCache::getWallConfig() const {
    return wall_config_;
}

const nlohmann::json& GameConfigCache::getMiniGames() const {
    return mini_games_;
}

const nlohmann::json& GameConfigCache::getTowerDefenseMobs() const {
    return tower_defense_mobs_;
}

const nlohmann::json& GameConfigCache::getTowerDefenseTowers() const {
    return tower_defense_towers_;
}

const nlohmann::json& GameConfigCache::getTowerDefenseUnits() const {
    return tower_defense_units_;
}

const nlohmann::json& GameConfigCache::getTowerDefenseUnitUnlocks() const {
    return tower_defense_unit_unlocks_;
}

nlohmann::json GameConfigCache::getAllConfigs() const {
    nlohmann::json result;
    result["damage_types"] = damage_types_;
    result["fiefdom_building_types"] = fiefdom_building_types_;
    result["player_combatants"] = player_combatants_;
    result["enemy_combatants"] = enemy_combatants_;
    result["heroes"] = heroes_;
    result["fiefdom_officials"] = fiefdom_officials_;
    result["wall_config"] = wall_config_;
    result["mini_games"] = mini_games_;
    result["tower_defense_mobs"] = tower_defense_mobs_;
    result["tower_defense_towers"] = tower_defense_towers_;
    result["tower_defense_units"] = tower_defense_units_;
    return result;
}

bool GameConfigCache::isLoaded() const {
    return loaded_;
}