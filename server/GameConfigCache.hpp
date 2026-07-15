#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <ctime>
#include <time.h>

class GameConfigCache {
public:
    GameConfigCache() = default;

    bool initialize(const std::string& config_dir);

    nlohmann::json& getDamageTypes();
    nlohmann::json& getFiefdomBuildingTypes();
    nlohmann::json& getPlayerCombatants();
    nlohmann::json& getEnemyCombatants();
    nlohmann::json& getHeroes();
    nlohmann::json& getFiefdomOfficials();
    nlohmann::json& getWallConfig();
    nlohmann::json& getMiniGames();
    nlohmann::json& getTowerDefenseMobs();
    nlohmann::json& getTowerDefenseTowers();
    nlohmann::json& getTowerDefenseUnits();
    nlohmann::json& getTowerDefenseUnitUnlocks();
    nlohmann::json& getTowerDefenseProjectiles();
    std::optional<nlohmann::json> getTowerDefenseSpawnSchedule(const std::string& filename);

    nlohmann::json getAllConfigs() const;
    bool isLoaded() const;

private:
    struct ConfigEntry {
        std::string path;
        nlohmann::json data;
        void (*postprocess)(nlohmann::json&) = nullptr;
        time_t mtime = 0;
        int64_t last_check_ms = 0;
    };

    std::string config_dir_;
    std::unordered_map<std::string, ConfigEntry> configs_;
    std::mutex reload_mutex_;
    bool loaded_ = false;

    bool loadConfig(const std::string& path, const std::string& name, nlohmann::json& target);
    nlohmann::json& getConfig(const std::string& name);
    void try_reload(const std::string& name, ConfigEntry& entry);

    static int64_t monotonic_ms();
    static void scaleTDPieceValues(nlohmann::json& pieces);
};
