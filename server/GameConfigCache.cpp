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

nlohmann::json GameConfigCache::getAllConfigs() const {
    nlohmann::json result;
    result["damage_types"] = damage_types_;
    result["fiefdom_building_types"] = fiefdom_building_types_;
    result["player_combatants"] = player_combatants_;
    result["enemy_combatants"] = enemy_combatants_;
    result["heroes"] = heroes_;
    result["fiefdom_officials"] = fiefdom_officials_;
    return result;
}

bool GameConfigCache::isLoaded() const {
    return loaded_;
}