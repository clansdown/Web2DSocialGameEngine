#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <optional>

class GameConfigCache {
public:
    static GameConfigCache& getInstance();

    bool initialize(const std::string& config_dir);

    const nlohmann::json& getDamageTypes() const;
    const nlohmann::json& getFiefdomBuildingTypes() const;
    const nlohmann::json& getPlayerCombatants() const;
    const nlohmann::json& getEnemyCombatants() const;
    const nlohmann::json& getHeroes() const;
    const nlohmann::json& getFiefdomOfficials() const;
    const nlohmann::json& getWallConfig() const;
    const nlohmann::json& getMiniGames() const;
    const nlohmann::json& getTowerDefenseMobs() const;
    const nlohmann::json& getTowerDefenseTowers() const;
    const nlohmann::json& getTowerDefenseUnits() const;
    const nlohmann::json& getTowerDefenseUnitUnlocks() const;
    const nlohmann::json& getTowerDefenseProjectiles() const;

    nlohmann::json getAllConfigs() const;

    bool isLoaded() const;

private:
    GameConfigCache() = default;
    GameConfigCache(const GameConfigCache&) = delete;
    GameConfigCache& operator=(const GameConfigCache&) = delete;

    bool loadConfig(const std::string& path, const std::string& name, nlohmann::json& target);

    nlohmann::json damage_types_;
    nlohmann::json fiefdom_building_types_;
    nlohmann::json player_combatants_;
    nlohmann::json enemy_combatants_;
    nlohmann::json heroes_;
    nlohmann::json fiefdom_officials_;
    nlohmann::json wall_config_;
    nlohmann::json mini_games_;
    nlohmann::json tower_defense_mobs_;
    nlohmann::json tower_defense_towers_;
    nlohmann::json tower_defense_units_;
    nlohmann::json tower_defense_unit_unlocks_;
    nlohmann::json tower_defense_projectiles_;
    bool loaded_ = false;
};