#include "GameConfigCache.hpp"
#include "Money.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <sys/stat.h>

namespace {

const nlohmann::json& null_config() {
    static const nlohmann::json value = nlohmann::json();
    return value;
}

std::string read_file_to_string(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

void add_json_files_in_directory(const std::string& directory,
                                 const std::string& prefix,
                                 std::vector<std::string>& names) {
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec);
    if (ec) {
        return;
    }
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        const std::string filename = entry.path().filename().string();
        if (filename.size() < 5) {
            continue;
        }
        if (filename.compare(filename.size() - 5, 5, ".json") != 0) {
            continue;
        }
        names.push_back(prefix + filename);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// config_handle
// ---------------------------------------------------------------------------

config_handle::config_handle() = default;

config_handle::config_handle(config_ptr ptr) : ptr_(std::move(ptr)) {}

const nlohmann::json& config_handle::operator*() const & {
    return ptr_ ? *ptr_ : null_config();
}

const nlohmann::json* config_handle::operator->() const {
    return ptr_ ? ptr_.get() : &null_config();
}

bool config_handle::isNull() const {
    return !ptr_ || ptr_->is_null();
}

bool config_handle::contains(const std::string& key) const {
    return (**this).contains(key);
}

const nlohmann::json& config_handle::operator[](const std::string& key) const {
    return (**this)[key];
}

const nlohmann::json& config_handle::operator[](const char* key) const {
    return (**this)[key];
}

const nlohmann::json& config_handle::operator[](std::size_t index) const {
    return (**this)[index];
}

const nlohmann::json& config_handle::at(const std::string& key) const {
    return (**this).at(key);
}

const nlohmann::json& config_handle::at(std::size_t index) const {
    return (**this).at(index);
}

bool config_handle::is_null() const { return (**this).is_null(); }
bool config_handle::is_object() const { return (**this).is_object(); }
bool config_handle::is_array() const { return (**this).is_array(); }
bool config_handle::is_string() const { return (**this).is_string(); }
bool config_handle::is_number() const { return (**this).is_number(); }
bool config_handle::is_boolean() const { return (**this).is_boolean(); }
bool config_handle::is_discarded() const { return (**this).is_discarded(); }
bool config_handle::empty() const { return (**this).empty(); }
std::size_t config_handle::size() const { return (**this).size(); }
std::string config_handle::dump() const { return (**this).dump(); }

// ---------------------------------------------------------------------------
// config_snapshot
// ---------------------------------------------------------------------------

config_snapshot::config_snapshot(std::shared_ptr<const config_state> state)
    : state_(std::move(state)) {}

config_handle config_snapshot::get(const std::string& name) const {
    if (!state_) {
        return config_handle();
    }
    const auto it = state_->files.find(name);
    if (it == state_->files.end()) {
        return config_handle();
    }
    return config_handle(it->second);
}

config_handle config_snapshot::getDamageTypes() const { return get("damage_types.json"); }
config_handle config_snapshot::getFiefdomBuildingTypes() const { return get("fiefdom_building_types.json"); }
config_handle config_snapshot::getPlayerCombatants() const { return get("player_combatants.json"); }
config_handle config_snapshot::getEnemyCombatants() const { return get("enemy_combatants.json"); }
config_handle config_snapshot::getHeroes() const { return get("heroes.json"); }
config_handle config_snapshot::getFiefdomOfficials() const { return get("fiefdom_officials.json"); }
config_handle config_snapshot::getWallConfig() const { return get("wall_config.json"); }
config_handle config_snapshot::getMiniGames() const { return get("mini_games.json"); }
config_handle config_snapshot::getTowerDefenseMobs() const { return get("tower_defense/mobs.json"); }
config_handle config_snapshot::getTowerDefenseTowers() const { return get("tower_defense/towers.json"); }
config_handle config_snapshot::getTowerDefenseUnits() const { return get("tower_defense/units.json"); }
config_handle config_snapshot::getTowerDefenseUnitUnlocks() const { return get("tower_defense/unit_unlocks.json"); }
config_handle config_snapshot::getTowerDefenseProjectiles() const { return get("tower_defense/projectiles.json"); }
config_handle config_snapshot::getTowerDefenseWaveTemplates() const { return get("tower_defense/wave_templates.json"); }
config_handle config_snapshot::getTowerDefenseOngoing() const { return get("tower_defense/ongoing.json"); }
config_handle config_snapshot::getWeedingPlants() const { return get("weeding/plants.json"); }
config_handle config_snapshot::getWeedingTools() const { return get("weeding/tools.json"); }
config_handle config_snapshot::getWeedingSpecials() const { return get("weeding/specials.json"); }
config_handle config_snapshot::getWeedingOngoing() const { return get("weeding/ongoing.json"); }
config_handle config_snapshot::getEconomyConfig() const { return get("economy.json"); }
config_handle config_snapshot::getRetinueConfig() const { return get("retinue.json"); }
config_handle config_snapshot::getEquipmentConfig() const { return get("equipment.json"); }
config_handle config_snapshot::getItemsConfig() const { return get("items.json"); }
config_handle config_snapshot::getTechTreesConfig() const { return get("tech_trees.json"); }
config_handle config_snapshot::getManorUi() const { return get("manor_ui.json"); }
config_handle config_snapshot::getManorRiver() const { return get("manor_river.json"); }
config_handle config_snapshot::getPortraitFrames() const { return get("portrait_frames.json"); }
config_handle config_snapshot::getCombatRulesets() const { return get("combat/rulesets.json"); }
config_handle config_snapshot::getCombatWarbands() const { return get("combat/warbands.json"); }
config_handle config_snapshot::getCombatStructures() const { return get("combat/structures.json"); }
config_handle config_snapshot::getCombatFormations() const { return get("combat/formations.json"); }
config_handle config_snapshot::getCombatProfiles() const { return get("combat/profiles.json"); }
config_handle config_snapshot::getCombatRewards() const { return get("combat/rewards.json"); }
config_handle config_snapshot::getCombatOfferings() const { return get("combat/offerings.json"); }
config_handle config_snapshot::getCosmetics() const { return get("cosmetics.json"); }
config_handle config_snapshot::getProgression() const { return get("progression.json"); }
config_handle config_snapshot::getChatConfig() const { return get("chat.json"); }
config_handle config_snapshot::getChatAdmins() const { return get("chat_admins.json"); }
config_handle config_snapshot::getSafeMessages() const { return get("safe_messages.json"); }
config_handle config_snapshot::getSafeBaronyNames() const { return get("safe_barony_names.json"); }
config_handle config_snapshot::getSafeBaronyTags() const { return get("safe_barony_tags.json"); }
config_handle config_snapshot::getSafeReactions() const { return get("safe_reactions.json"); }
config_handle config_snapshot::getSafeGifs() const { return get("safe_gifs.json"); }
config_handle config_snapshot::getSafeBanReasons() const { return get("safe_ban_reasons.json"); }
config_handle config_snapshot::getTranslationConfig() const { return get("translation.json"); }
config_handle config_snapshot::getPushConfig() const { return get("push.json"); }
config_handle config_snapshot::getRegistrationConfig() const { return get("registration.json"); }
config_handle config_snapshot::getReeveConfig() const { return get("reeve.json"); }

std::optional<nlohmann::json>
config_snapshot::getTowerDefenseSpawnSchedule(const std::string& filename) const {
    const auto data = get("tower_defense/spawn_schedules/" + filename);
    if (data.is_null()) {
        return std::nullopt;
    }
    return *data;
}

std::optional<nlohmann::json>
config_snapshot::loadWeedingMap(const std::string& filename) const {
    const auto data = get("weeding/maps/" + filename);
    if (data.is_null()) {
        return std::nullopt;
    }
    return *data;
}

std::optional<nlohmann::json>
config_snapshot::loadWeedingLevelConfig(const std::string& filename) const {
    const auto data = get("weeding/" + filename);
    if (data.is_null()) {
        return std::nullopt;
    }
    return *data;
}

int config_snapshot::getRetinueCapacity(int manor_level) const {
    const auto cfg = getRetinueConfig();
    if (cfg.contains("retinue_capacity_by_level") &&
        cfg["retinue_capacity_by_level"].is_array()) {
        const auto& arr = cfg["retinue_capacity_by_level"];
        if (arr.empty()) return 0;
        if (manor_level < 0) manor_level = 0;
        std::size_t idx = static_cast<std::size_t>(manor_level);
        if (idx >= arr.size()) idx = arr.size() - 1;
        if (arr[idx].is_number()) return arr[idx].get<int>();
    }
    return 0;
}

int config_snapshot::getBuildingArableAcres(const std::string& type_id) const {
    if (!state_) return 0;
    const auto it = state_->building_arable_acres.find(type_id);
    return it == state_->building_arable_acres.end() ? 0 : it->second;
}

double config_snapshot::getArableLandByLevel(int manor_level) const {
    const auto economy = getEconomyConfig();
    if (economy.contains("arable_land_by_level") && economy["arable_land_by_level"].is_array()) {
        const auto& arr = economy["arable_land_by_level"];
        if (arr.empty()) return 0.0;
        if (manor_level < 0) manor_level = 0;
        std::size_t idx = static_cast<std::size_t>(manor_level);
        if (idx >= arr.size()) idx = arr.size() - 1;
        if (arr[idx].is_number()) return arr[idx].get<double>();
    }
    return 0.0;
}

int config_snapshot::getBuildingForestAcres(const std::string& type_id) const {
    if (!state_) return 0;
    const auto it = state_->building_forest_acres.find(type_id);
    return it == state_->building_forest_acres.end() ? 0 : it->second;
}

double config_snapshot::getForestLandByLevel(int manor_level) const {
    const auto economy = getEconomyConfig();
    if (economy.contains("forest_land_by_level") && economy["forest_land_by_level"].is_array()) {
        const auto& arr = economy["forest_land_by_level"];
        if (arr.empty()) return 0.0;
        if (manor_level < 0) manor_level = 0;
        std::size_t idx = static_cast<std::size_t>(manor_level);
        if (idx >= arr.size()) idx = arr.size() - 1;
        if (arr[idx].is_number()) return arr[idx].get<double>();
    }
    return 0.0;
}

std::string config_snapshot::getBuildingClass(const std::string& type_id) const {
    if (!state_) return "";
    const auto it = state_->building_classes.find(type_id);
    return it == state_->building_classes.end() ? "" : it->second;
}

std::string config_snapshot::getBuildingExclusiveGroup(const std::string& type_id) const {
    if (!state_) return "";
    const auto it = state_->building_exclusive_groups.find(type_id);
    return it == state_->building_exclusive_groups.end() ? "" : it->second;
}

nlohmann::json config_snapshot::getAllConfigs() const {
    nlohmann::json result;
    auto add = [&](const std::string& key, const std::string& name) {
        const auto cfg = get(name);
        if (!cfg.isNull()) {
            result[key] = *cfg;
        }
    };
    add("damage_types", "damage_types.json");
    add("fiefdom_building_types", "fiefdom_building_types.json");
    add("player_combatants", "player_combatants.json");
    add("enemy_combatants", "enemy_combatants.json");
    add("heroes", "heroes.json");
    add("fiefdom_officials", "fiefdom_officials.json");
    add("wall_config", "wall_config.json");
    add("mini_games", "mini_games.json");
    // These lookups used to reference nonexistent `tower_defense_mobs.json`
    // style names, so the client never received the tower-defense configs.
    add("tower_defense_mobs", "tower_defense/mobs.json");
    add("tower_defense_towers", "tower_defense/towers.json");
    add("tower_defense_units", "tower_defense/units.json");
    add("manor_ui", "manor_ui.json");
    add("safe_messages", "safe_messages.json");
    add("safe_barony_names", "safe_barony_names.json");
    add("safe_barony_tags", "safe_barony_tags.json");
    add("safe_reactions", "safe_reactions.json");
    add("safe_gifs", "safe_gifs.json");
    add("safe_ban_reasons", "safe_ban_reasons.json");
    return result;
}

std::int64_t config_snapshot::getConfigGeneration(const std::string& name) const {
    if (!state_) return 0;
    const auto it = state_->generations.find(name);
    return it == state_->generations.end() ? 0 : it->second;
}

std::int64_t config_snapshot::generation() const {
    return state_ ? state_->generation : 0;
}

// ---------------------------------------------------------------------------
// GameConfigCache
// ---------------------------------------------------------------------------

bool GameConfigCache::initialize(const std::string& config_dir) {
    config_dir_ = config_dir;

    tracked_names_ = {
        "damage_types.json",
        "fiefdom_building_types.json",
        "player_combatants.json",
        "enemy_combatants.json",
        "heroes.json",
        "fiefdom_officials.json",
        "wall_config.json",
        "mini_games.json",
        "tower_defense/mobs.json",
        "tower_defense/towers.json",
        "tower_defense/units.json",
        "tower_defense/unit_unlocks.json",
        "tower_defense/projectiles.json",
        "tower_defense/wave_templates.json",
        "tower_defense/ongoing.json",
        "weeding/plants.json",
        "weeding/tools.json",
        "weeding/specials.json",
        "weeding/ongoing.json",
        "economy.json",
        "retinue.json",
        "equipment.json",
        "items.json",
        "tech_trees.json",
        "manor_ui.json",
        "manor_river.json",
        "portrait_frames.json",
        "combat/rulesets.json",
        "combat/warbands.json",
        "combat/structures.json",
        "combat/formations.json",
        "combat/profiles.json",
        "combat/rewards.json",
        "combat/offerings.json",
        "cosmetics.json",
        "progression.json",
        "chat.json",
        "chat_admins.json",
        "safe_messages.json",
        "safe_barony_names.json",
        "safe_barony_tags.json",
        "safe_reactions.json",
        "safe_gifs.json",
        "safe_ban_reasons.json",
        "translation.json",
        "push.json",
        // Server-only config. It contains the optional shared global
        // registration code, so it is deliberately NOT added to
        // getAllConfigs() (which backs getGameInfo).
        "registration.json",
        // Server-only Reeve tuning (weights/thresholds/placement policy).
        "reeve.json",
    };

    postprocessors_.clear();
    postprocessors_["fiefdom_building_types.json"] = money::normalize_money_costs;
    postprocessors_["wall_config.json"] = money::normalize_money_costs;

    config_reload_result result;
    auto next = prepareReload(true, result);
    if (!next) {
        std::cerr << "[GameConfigCache] Initialization failed: " << result.error << std::endl;
        loaded_ = false;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = std::move(next);
    }
    loaded_ = true;
    return true;
}

config_snapshot GameConfigCache::snapshot() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return config_snapshot(state_);
}

bool GameConfigCache::reloadConfigs(config_reload_result& result) {
    auto next = prepareReload(true, result);
    if (!next) {
        std::cerr << "[GameConfigCache] Reload aborted: " << result.error << std::endl;
        return false;
    }
    commitReload(std::move(next), result);
    return true;
}

bool GameConfigCache::reloadChangedConfigs(config_reload_result& result) {
    auto next = prepareReload(false, result);
    if (!next) {
        std::cerr << "[GameConfigCache] Reload aborted: " << result.error << std::endl;
        return false;
    }
    commitReload(std::move(next), result);
    return true;
}

bool GameConfigCache::isLoaded() const {
    return loaded_.load();
}

void GameConfigCache::addReloadCallback(config_reload_callback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.push_back(std::move(callback));
}

std::shared_ptr<const config_state>
GameConfigCache::prepareReload(bool force_all, config_reload_result& result) {
    std::shared_ptr<const config_state> previous;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        previous = state_;
    }

    auto next = std::make_shared<config_state>();
    std::int64_t next_generation = previous ? previous->generation : 0;

    const std::vector<std::string> names = collectConfigNames();
    for (const std::string& name : names) {
        const std::string path = config_dir_ + "/" + name;

        struct stat st {};
        const bool stat_ok = (stat(path.c_str(), &st) == 0);

        if (!force_all && previous) {
            const auto file_it = previous->files.find(name);
            const auto mtime_it = previous->mtimes.find(name);
            if (file_it != previous->files.end() && mtime_it != previous->mtimes.end() &&
                stat_ok && mtime_it->second == st.st_mtime) {
                next->files[name] = file_it->second;
                next->mtimes[name] = mtime_it->second;
                const auto gen_it = previous->generations.find(name);
                next->generations[name] =
                    (gen_it == previous->generations.end()) ? 0 : gen_it->second;
                continue;
            }
        }

        if (!stat_ok) {
            const std::string error = "cannot stat " + path;
            result.failed_files.push_back(name);
            if (!result.error.empty()) result.error += "; ";
            result.error += error;
            return nullptr;
        }

        config_ptr loaded;
        std::string error;
        if (!loadConfigSnapshot(name, path, loaded, error)) {
            result.failed_files.push_back(name);
            if (!result.error.empty()) result.error += "; ";
            result.error += name + ": " + error;
            return nullptr;
        }

        next->files[name] = std::move(loaded);
        next->mtimes[name] = st.st_mtime;
        next->generations[name] = ++next_generation;
        result.changed_files.push_back(name);
    }

    next->generation = next_generation;
    rebuildDerivedMaps(next);
    return next;
}

void GameConfigCache::commitReload(std::shared_ptr<const config_state> next,
                                   config_reload_result& result) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = std::move(next);
    }
    loaded_ = true;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        result.generation = state_ ? state_->generation : 0;
    }

    config_reload_result callback_result = result;

    std::vector<config_reload_callback> callbacks;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks = callbacks_;
    }
    for (const auto& callback : callbacks) {
        try {
            callback(callback_result);
        } catch (const std::exception& e) {
            std::cerr << "[GameConfigCache] Reload callback failed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[GameConfigCache] Reload callback failed with unknown exception"
                      << std::endl;
        }
    }

    std::cout << "[GameConfigCache] Reload complete: changed="
              << callback_result.changed_files.size()
              << " failed=" << callback_result.failed_files.size()
              << " generation=" << callback_result.generation << std::endl;
}

config_handle GameConfigCache::loadDynamicConfig(const std::string& name) const {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (state_) {
            const auto it = state_->files.find(name);
            if (it != state_->files.end()) {
                return config_handle(it->second);
            }
        }
    }

    std::lock_guard<std::mutex> dynamic_lock(dynamic_load_mutex_);

    std::shared_ptr<const config_state> previous;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        previous = state_;
    }
    if (previous) {
        const auto it = previous->files.find(name);
        if (it != previous->files.end()) {
            return config_handle(it->second);
        }
    }

    const std::string path = config_dir_ + "/" + name;
    config_ptr loaded;
    std::string error;
    if (!loadConfigSnapshot(name, path, loaded, error)) {
        std::cerr << "[GameConfigCache] Dynamic config load failed for " << name
                  << ": " << error << std::endl;
        return config_handle();
    }

    auto next = std::make_shared<config_state>();
    if (previous) {
        next->files = previous->files;
        next->mtimes = previous->mtimes;
        next->generations = previous->generations;
        next->building_arable_acres = previous->building_arable_acres;
        next->building_forest_acres = previous->building_forest_acres;
        next->building_classes = previous->building_classes;
        next->building_exclusive_groups = previous->building_exclusive_groups;
        next->generation = previous->generation;
    }

    struct stat st {};
    if (stat(path.c_str(), &st) == 0) {
        next->mtimes[name] = st.st_mtime;
    }
    next->files[name] = loaded;
    next->generations[name] = ++next->generation;
    rebuildDerivedMaps(next);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = next;
    }
    return config_handle(loaded);
}

std::vector<std::string> GameConfigCache::collectConfigNames() const {
    std::vector<std::string> names = tracked_names_;
    std::unordered_set<std::string> seen(names.begin(), names.end());

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (state_) {
            for (const auto& entry : state_->files) {
                if (seen.insert(entry.first).second) {
                    names.push_back(entry.first);
                }
            }
        }
    }

    std::vector<std::string> dynamic_names;
    add_json_files_in_directory(config_dir_ + "/tower_defense/spawn_schedules",
                                "tower_defense/spawn_schedules/", dynamic_names);
    add_json_files_in_directory(config_dir_ + "/weeding/maps",
                                "weeding/maps/", dynamic_names);
    add_json_files_in_directory(config_dir_ + "/weeding",
                                "weeding/", dynamic_names);

    for (const std::string& name : dynamic_names) {
        if (seen.insert(name).second) {
            names.push_back(name);
        }
    }
    return names;
}

bool GameConfigCache::loadConfigSnapshot(const std::string& name,
                                         const std::string& path,
                                         config_ptr& out,
                                         std::string& error) const {
    const std::string content = read_file_to_string(path);
    if (content.empty()) {
        error = "cannot read " + path;
        return false;
    }

    try {
        nlohmann::json parsed = nlohmann::json::parse(content, nullptr, true, true, true);
        const auto postprocess_it = postprocessors_.find(name);
        if (postprocess_it != postprocessors_.end() && postprocess_it->second) {
            postprocess_it->second(parsed);
        }
        out = std::make_shared<const nlohmann::json>(std::move(parsed));
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

void GameConfigCache::rebuildDerivedMaps(
    const std::shared_ptr<config_state>& state) const {    state->building_arable_acres.clear();
    state->building_forest_acres.clear();
    state->building_classes.clear();
    state->building_exclusive_groups.clear();

    const auto it = state->files.find("fiefdom_building_types.json");
    if (it == state->files.end() || !it->second || !it->second->is_array()) {
        return;
    }

    for (const auto& obj : *it->second) {
        if (!obj.is_object() || obj.empty()) continue;
        for (auto entry = obj.begin(); entry != obj.end(); ++entry) {
            if (!entry.value().is_object()) continue;
            const nlohmann::json& cfg = entry.value();
            if (cfg.contains("arable_acres") && cfg["arable_acres"].is_number()) {
                state->building_arable_acres[entry.key()] = cfg["arable_acres"].get<int>();
            }
            if (cfg.contains("forest_acres") && cfg["forest_acres"].is_number()) {
                state->building_forest_acres[entry.key()] = cfg["forest_acres"].get<int>();
            }
            if (cfg.contains("class") && cfg["class"].is_string()) {
                state->building_classes[entry.key()] = cfg["class"].get<std::string>();
            }
            if (cfg.contains("exclusive_group") && cfg["exclusive_group"].is_string()) {
                state->building_exclusive_groups[entry.key()] =
                    cfg["exclusive_group"].get<std::string>();
            }
        }
    }
}

std::optional<nlohmann::json>
GameConfigCache::getTowerDefenseSpawnSchedule(const std::string& filename) const {
    const auto handle = loadDynamicConfig("tower_defense/spawn_schedules/" + filename);
    if (handle.is_null()) {
        return std::nullopt;
    }
    return *handle;
}

std::optional<nlohmann::json>
GameConfigCache::loadWeedingMap(const std::string& filename) const {
    const auto handle = loadDynamicConfig("weeding/maps/" + filename);
    if (handle.is_null()) {
        return std::nullopt;
    }
    return *handle;
}

std::optional<nlohmann::json>
GameConfigCache::loadWeedingLevelConfig(const std::string& filename) const {
    const auto handle = loadDynamicConfig("weeding/" + filename);
    if (handle.is_null()) {
        return std::nullopt;
    }
    return *handle;
}
