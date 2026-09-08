#include "GameConfigCache.hpp"
#include "Money.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sys/stat.h>

int64_t GameConfigCache::monotonic_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
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
        target = nlohmann::json::parse(content, nullptr, true, true, true);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse " << name << ": " << e.what() << std::endl;
        return false;
    }
}

bool GameConfigCache::initialize(const std::string& config_dir) {
    config_dir_ = config_dir;
    bool success = true;

    auto add = [&](const std::string& rel_path, void (*pp)(nlohmann::json&) = nullptr) {
        ConfigEntry e;
        e.path = config_dir + "/" + rel_path;
        e.postprocess = pp;
        configs_[rel_path] = std::move(e);
    };

    add("damage_types.json");
    add("fiefdom_building_types.json", money::normalize_money_costs);
    add("player_combatants.json");
    add("enemy_combatants.json");
    add("heroes.json");
    add("fiefdom_officials.json");
    add("wall_config.json", money::normalize_money_costs);
    add("mini_games.json");
    add("tower_defense/mobs.json");
    add("tower_defense/towers.json");
    add("tower_defense/units.json");
    add("tower_defense/unit_unlocks.json");
    add("tower_defense/projectiles.json");
    add("tower_defense/wave_templates.json");
    add("tower_defense/ongoing.json");
    add("weeding/plants.json");
    add("weeding/tools.json");
    add("weeding/specials.json");
    add("weeding/ongoing.json");
    add("economy.json");
    add("retinue.json");
    add("equipment.json");
    add("items.json");
    add("tech_trees.json");
    add("manor_ui.json");
    add("manor_river.json");
    add("combat/rulesets.json");

    for (auto& [name, entry] : configs_) {
        struct stat st;
        if (stat(entry.path.c_str(), &st) == 0) {
            entry.mtime = st.st_mtime;
        }
        success &= loadConfig(entry.path, name, entry.data);
        if (entry.postprocess && !entry.data.is_null()) {
            entry.postprocess(entry.data);
        }
        entry.last_check_ms = monotonic_ms();
    }

    loaded_ = success;
    return success;
}

nlohmann::json& GameConfigCache::getConfig(const std::string& name) {
    auto it = configs_.find(name);
    if (it == configs_.end()) {
        ConfigEntry e;
        e.path = config_dir_ + "/" + name;
        e.last_check_ms = 0;
        it = configs_.emplace(name, std::move(e)).first;
    }
    std::lock_guard<std::mutex> lock(reload_mutex_);
    try_reload(it->first, it->second);
    return it->second.data;
}

void GameConfigCache::try_reload(const std::string& name, ConfigEntry& entry) {
    int64_t now = monotonic_ms();
    if (now - entry.last_check_ms < 5000) return;
    entry.last_check_ms = now;

    struct stat st;
    if (stat(entry.path.c_str(), &st) != 0) {
        // File deleted or doesn't exist — clear data so callers can detect
        entry.data = nullptr;
        entry.mtime = 0;
        return;
    }
    if (st.st_mtime <= entry.mtime) return;
    entry.mtime = st.st_mtime;
    std::cerr << "[GameConfigCache] Hot-reload: " << name << " changed, re-parsing" << std::endl;
    if (loadConfig(entry.path, name, entry.data)) {
        if (entry.postprocess) entry.postprocess(entry.data);
    }
}

nlohmann::json& GameConfigCache::getDamageTypes() { return getConfig("damage_types.json"); }
nlohmann::json& GameConfigCache::getFiefdomBuildingTypes() { return getConfig("fiefdom_building_types.json"); }
nlohmann::json& GameConfigCache::getPlayerCombatants() { return getConfig("player_combatants.json"); }
nlohmann::json& GameConfigCache::getEnemyCombatants() { return getConfig("enemy_combatants.json"); }
nlohmann::json& GameConfigCache::getHeroes() { return getConfig("heroes.json"); }
nlohmann::json& GameConfigCache::getFiefdomOfficials() { return getConfig("fiefdom_officials.json"); }
nlohmann::json& GameConfigCache::getWallConfig() { return getConfig("wall_config.json"); }
nlohmann::json& GameConfigCache::getMiniGames() { return getConfig("mini_games.json"); }
nlohmann::json& GameConfigCache::getTowerDefenseMobs() { return getConfig("tower_defense/mobs.json"); }
nlohmann::json& GameConfigCache::getTowerDefenseTowers() { return getConfig("tower_defense/towers.json"); }
nlohmann::json& GameConfigCache::getTowerDefenseUnits() { return getConfig("tower_defense/units.json"); }
nlohmann::json& GameConfigCache::getTowerDefenseUnitUnlocks() { return getConfig("tower_defense/unit_unlocks.json"); }
nlohmann::json& GameConfigCache::getTowerDefenseProjectiles() { return getConfig("tower_defense/projectiles.json"); }
nlohmann::json& GameConfigCache::getTowerDefenseWaveTemplates() { return getConfig("tower_defense/wave_templates.json"); }
nlohmann::json& GameConfigCache::getTowerDefenseOngoing() { return getConfig("tower_defense/ongoing.json"); }
nlohmann::json& GameConfigCache::getWeedingPlants() { return getConfig("weeding/plants.json"); }
nlohmann::json& GameConfigCache::getWeedingTools() { return getConfig("weeding/tools.json"); }
nlohmann::json& GameConfigCache::getWeedingSpecials() { return getConfig("weeding/specials.json"); }
nlohmann::json& GameConfigCache::getWeedingOngoing() { return getConfig("weeding/ongoing.json"); }
nlohmann::json& GameConfigCache::getEconomyConfig() { return getConfig("economy.json"); }
nlohmann::json& GameConfigCache::getRetinueConfig() { return getConfig("retinue.json"); }
nlohmann::json& GameConfigCache::getEquipmentConfig() { return getConfig("equipment.json"); }
nlohmann::json& GameConfigCache::getItemsConfig() { return getConfig("items.json"); }
nlohmann::json& GameConfigCache::getTechTreesConfig() { return getConfig("tech_trees.json"); }

int GameConfigCache::getRetinueCapacity(int manor_level) {
    nlohmann::json& cfg = getRetinueConfig();
    if (cfg.contains("retinue_capacity_by_level") &&
        cfg["retinue_capacity_by_level"].is_array()) {
        const auto& arr = cfg["retinue_capacity_by_level"];
        if (arr.empty()) return 0;
        if (manor_level < 0) manor_level = 0;
        size_t idx = static_cast<size_t>(manor_level);
        if (idx >= arr.size()) idx = arr.size() - 1;
        if (arr[idx].is_number()) return arr[idx].get<int>();
    }
    return 0;
}

int GameConfigCache::getBuildingArableAcres(const std::string& type_id) {
    nlohmann::json& buildings = getFiefdomBuildingTypes();
    if (buildings.is_array()) {
        for (auto& obj : buildings) {
            if (obj.is_object() && obj.contains(type_id)) {
                const auto& cfg = obj[type_id];
                if (cfg.is_object() && cfg.contains("arable_acres") && cfg["arable_acres"].is_number()) {
                    return cfg["arable_acres"].get<int>();
                }
                return 0;
            }
        }
    }
    return 0;
}

double GameConfigCache::getArableLandByLevel(int manor_level) {
    nlohmann::json& economy = getEconomyConfig();
    if (economy.contains("arable_land_by_level") && economy["arable_land_by_level"].is_array()) {
        const auto& arr = economy["arable_land_by_level"];
        if (arr.empty()) return 0.0;
        if (manor_level < 0) manor_level = 0;
        size_t idx = static_cast<size_t>(manor_level);
        if (idx >= arr.size()) idx = arr.size() - 1;
        if (arr[idx].is_number()) return arr[idx].get<double>();
    }
    return 0.0;
}

int GameConfigCache::getBuildingForestAcres(const std::string& type_id) {
    nlohmann::json& buildings = getFiefdomBuildingTypes();
    if (buildings.is_array()) {
        for (auto& obj : buildings) {
            if (obj.is_object() && obj.contains(type_id)) {
                const auto& cfg = obj[type_id];
                if (cfg.is_object() && cfg.contains("forest_acres") && cfg["forest_acres"].is_number()) {
                    return cfg["forest_acres"].get<int>();
                }
                return 0;
            }
        }
    }
    return 0;
}

double GameConfigCache::getForestLandByLevel(int manor_level) {
    nlohmann::json& economy = getEconomyConfig();
    if (economy.contains("forest_land_by_level") && economy["forest_land_by_level"].is_array()) {
        const auto& arr = economy["forest_land_by_level"];
        if (arr.empty()) return 0.0;
        if (manor_level < 0) manor_level = 0;
        size_t idx = static_cast<size_t>(manor_level);
        if (idx >= arr.size()) idx = arr.size() - 1;
        if (arr[idx].is_number()) return arr[idx].get<double>();
    }
    return 0.0;
}

std::string GameConfigCache::getBuildingClass(const std::string& type_id) {
    nlohmann::json& buildings = getFiefdomBuildingTypes();
    if (buildings.is_array()) {
        for (auto& obj : buildings) {
            if (obj.is_object() && obj.contains(type_id)) {
                const auto& cfg = obj[type_id];
                if (cfg.is_object() && cfg.contains("class") && cfg["class"].is_string()) {
                    return cfg["class"].get<std::string>();
                }
                return "";
            }
        }
    }
    return "";
}

nlohmann::json& GameConfigCache::getManorUi() { return getConfig("manor_ui.json"); }
nlohmann::json& GameConfigCache::getManorRiver() { return getConfig("manor_river.json"); }
nlohmann::json& GameConfigCache::getCombatRulesets() { return getConfig("combat/rulesets.json"); }

std::optional<nlohmann::json> GameConfigCache::getTowerDefenseSpawnSchedule(const std::string& filename) {
    nlohmann::json& data = getConfig("tower_defense/spawn_schedules/" + filename);
    if (data.is_null()) return std::nullopt;
    return data;
}

std::optional<nlohmann::json> GameConfigCache::loadWeedingMap(const std::string& filename) {
    nlohmann::json& data = getConfig("weeding/maps/" + filename);
    if (data.is_null()) return std::nullopt;
    return data;
}

std::optional<nlohmann::json> GameConfigCache::loadWeedingLevelConfig(const std::string& filename) {
    nlohmann::json& data = getConfig("weeding/" + filename);
    if (data.is_null()) return std::nullopt;
    return data;
}

nlohmann::json GameConfigCache::getAllConfigs() const {
    nlohmann::json result;
    auto get = [&](const std::string& name) -> const nlohmann::json& {
        auto it = configs_.find(name);
        if (it != configs_.end()) return it->second.data;
        static nlohmann::json null_data;
        return null_data;
    };
    result["damage_types"] = get("damage_types.json");
    result["fiefdom_building_types"] = get("fiefdom_building_types.json");
    result["player_combatants"] = get("player_combatants.json");
    result["enemy_combatants"] = get("enemy_combatants.json");
    result["heroes"] = get("heroes.json");
    result["fiefdom_officials"] = get("fiefdom_officials.json");
    result["wall_config"] = get("wall_config.json");
    result["mini_games"] = get("mini_games.json");
    result["tower_defense_mobs"] = get("tower_defense_mobs.json");
    result["tower_defense_towers"] = get("tower_defense_towers.json");
    result["tower_defense_units"] = get("tower_defense_units.json");
    result["manor_ui"] = get("manor_ui.json");
    return result;
}

bool GameConfigCache::isLoaded() const {
    return loaded_;
}
