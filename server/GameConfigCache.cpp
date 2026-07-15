#include "GameConfigCache.hpp"
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

void GameConfigCache::scaleTDPieceValues(nlohmann::json& file_root) {
    auto drill = [](nlohmann::json& pieces) {
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
    };
    if (file_root.contains("towers") && file_root["towers"].is_object()) drill(file_root["towers"]);
    if (file_root.contains("units") && file_root["units"].is_object()) drill(file_root["units"]);
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
    add("fiefdom_building_types.json");
    add("player_combatants.json");
    add("enemy_combatants.json");
    add("heroes.json");
    add("fiefdom_officials.json");
    add("wall_config.json");
    add("mini_games.json");
    add("tower_defense/mobs.json");
    add("tower_defense/towers.json", scaleTDPieceValues);
    add("tower_defense/units.json", scaleTDPieceValues);
    add("tower_defense/unit_unlocks.json");
    add("tower_defense/projectiles.json");

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

std::optional<nlohmann::json> GameConfigCache::getTowerDefenseSpawnSchedule(const std::string& filename) {
    nlohmann::json& data = getConfig("tower_defense/spawn_schedules/" + filename);
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
    return result;
}

bool GameConfigCache::isLoaded() const {
    return loaded_;
}
