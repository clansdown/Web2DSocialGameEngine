#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

// Loads game config JSON files from the config directory.
//
// RAII: owns the resolved config root and loads each requested file on demand,
// caching the parsed result. Paths are validated to stay within the config dir
// to avoid escaping the sandbox.
class config_loader {
public:
    config_loader() = default;

    // Sets the config root directory. The directory must exist.
    // Returns false (and records an error message) if the directory is invalid.
    bool set_config_dir(const std::string& config_dir, std::string& error);

    const std::string& config_dir() const { return config_dir_; }

    // Loads and caches a JSON file relative to the config dir.
    // Returns std::nullopt on parse error / missing file.
    std::optional<nlohmann::json> load(const std::string& relative_path) const;

    // Convenience accessors for the standard config files used by the analyzer.
    nlohmann::json building_types() const;      // fiefdom_building_types.json (array)
    nlohmann::json economy() const;             // economy.json
    nlohmann::json player_combatants() const;   // player_combatants.json
    nlohmann::json enemy_combatants() const;    // enemy_combatants.json
    nlohmann::json heroes() const;              // heroes.json
    nlohmann::json td_towers() const;           // tower_defense/towers.json
    nlohmann::json td_units() const;            // tower_defense/units.json
    nlohmann::json td_mobs() const;             // tower_defense/mobs.json
    nlohmann::json td_waves() const;            // tower_defense/wave_templates.json
    nlohmann::json td_unlocks() const;          // tower_defense/unit_unlocks.json
    nlohmann::json weeding_plants() const;      // weeding/plants.json
    nlohmann::json weeding_tools() const;       // weeding/tools.json
    nlohmann::json manor_strategies() const;    // analyzer_manor_strategies.json (analyzer-only)

private:
    std::string config_dir_;
    std::string error_;
    mutable std::unordered_map<std::string, std::optional<nlohmann::json>> cache_;
};
