#include "config_loader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

using json = nlohmann::json;

bool config_loader::set_config_dir(const std::string& config_dir, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(config_dir, ec)) {
        error = "config directory not found: " + config_dir;
        return false;
    }
    config_dir_ = config_dir;
    return true;
}

std::optional<json> config_loader::load(const std::string& relative_path) const {
    auto it = cache_.find(relative_path);
    if (it != cache_.end()) {
        return it->second;
    }

    namespace fs = std::filesystem;
    fs::path root(config_dir_);
    fs::path full = root / relative_path;

    // Guard against path traversal outside the config directory.
    auto rel = full.lexically_relative(root);
    if (rel.empty() || rel.native().rfind("..", 0) == 0) {
        cache_[relative_path] = std::nullopt;
        return std::nullopt;
    }

    std::ifstream in(full);
    if (!in) {
        cache_[relative_path] = std::nullopt;
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    try {
        json parsed = json::parse(buffer.str());
        cache_[relative_path] = parsed;
        return parsed;
    } catch (const std::exception&) {
        cache_[relative_path] = std::nullopt;
        return std::nullopt;
    }
}

json config_loader::building_types() const {
    return load("fiefdom_building_types.json").value_or(json::array());
}

json config_loader::economy() const {
    return load("economy.json").value_or(json::object());
}

json config_loader::player_combatants() const {
    return load("player_combatants.json").value_or(json::object());
}

json config_loader::enemy_combatants() const {
    return load("enemy_combatants.json").value_or(json::object());
}

json config_loader::heroes() const {
    return load("heroes.json").value_or(json::object());
}

json config_loader::td_towers() const {
    return load("tower_defense/towers.json").value_or(json::object());
}

json config_loader::td_units() const {
    return load("tower_defense/units.json").value_or(json::object());
}

json config_loader::td_mobs() const {
    return load("tower_defense/mobs.json").value_or(json::object());
}

json config_loader::td_waves() const {
    return load("tower_defense/wave_templates.json").value_or(json::object());
}

json config_loader::td_unlocks() const {
    return load("tower_defense/unit_unlocks.json").value_or(json::object());
}

json config_loader::weeding_plants() const {
    return load("weeding/plants.json").value_or(json::object());
}

json config_loader::weeding_tools() const {
    return load("weeding/tools.json").value_or(json::object());
}

json config_loader::manor_strategies() const {
    return load("analyzer_manor_strategies.json").value_or(json::object());
}
