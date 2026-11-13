#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <ctime>
#include <atomic>
#include <cstdint>
#include <utility>

// ---------------------------------------------------------------------------
// Immutable config snapshots
// ---------------------------------------------------------------------------
//
// Config data is immutable once loaded. A reload builds a complete new
// `config_state` and swaps it into `GameConfigCache` atomically. Readers take
// one `config_snapshot` per request/operation and never observe a partially
// reloaded configuration. Old snapshots stay alive until their last reader
// releases them (shared ownership).
//
// `config_ptr` is the internal ownership primitive. `config_handle` is a
// restricted one-file view. `config_snapshot` is the per-operation
// consistency boundary. `GameConfigCache` is lifecycle/reload only.
// ---------------------------------------------------------------------------

using config_ptr = std::shared_ptr<const nlohmann::json>;

struct config_reload_result {
    std::vector<std::string> changed_files;
    std::vector<std::string> failed_files;
    std::string error;
    std::int64_t generation = 0;
};

using config_reload_callback = std::function<void(const config_reload_result&)>;

struct config_state {
    std::unordered_map<std::string, config_ptr> files;
    std::unordered_map<std::string, std::time_t> mtimes;
    std::unordered_map<std::string, std::int64_t> generations;

    std::unordered_map<std::string, int> building_arable_acres;
    std::unordered_map<std::string, int> building_forest_acres;
    std::unordered_map<std::string, std::string> building_classes;
    std::unordered_map<std::string, std::string> building_exclusive_groups;

    std::int64_t generation = 0;
};

// A read-only borrowed view of exactly one immutable config file.
//
// Usage:
//   const auto cfg = snapshot.getEconomyConfig();
//   const nlohmann::json& economy = *cfg;
//
// A temporary handle is safe for a single expression:
//   snapshot.getEconomyConfig()->contains("currency");
//
// Dereferencing a temporary handle is a compile error by design:
//   const nlohmann::json& bad = *snapshot.getEconomyConfig(); // does not compile
//
// Do NOT expose `get()`. A raw pointer must not escape the handle lifetime.
// If code needs a raw pointer, bind the handle first and use `&*handle`.
//
// A handle keeps its file alive but does not provide cross-file consistency.
// Use `config_snapshot` for that.
class config_handle {
public:
    config_handle();
    explicit config_handle(config_ptr ptr);

    const nlohmann::json& operator*() const &;
    const nlohmann::json& operator*() && = delete;
    const nlohmann::json* operator->() const;

    // Deep-copy compatibility conversion for legacy call sites that require a
    // `nlohmann::json` value or `const nlohmann::json&`. The conversion copies,
    // so the result can never outlive or alias a superseded snapshot. Prefer
    // binding the handle (`const auto cfg = snapshot.getX();`) for new code.
    operator nlohmann::json() const { return **this; }

    bool isNull() const;

    // Read-only forwarding accessors. These keep existing call sites terse
    // without exposing a raw pointer or an implicit reference that could
    // outlive the snapshot.
    bool contains(const std::string& key) const;
    const nlohmann::json& operator[](const std::string& key) const;
    const nlohmann::json& operator[](const char* key) const;
    const nlohmann::json& operator[](std::size_t index) const;
    const nlohmann::json& at(const std::string& key) const;
    const nlohmann::json& at(std::size_t index) const;

    template <typename ValueType>
    ValueType value(const std::string& key, const ValueType& default_value) const {
        return (**this).value(key, default_value);
    }

    template <typename ValueType>
    ValueType get() const {
        return (**this).template get<ValueType>();
    }

    bool is_null() const;
    bool is_object() const;
    bool is_array() const;
    bool is_string() const;
    bool is_number() const;
    bool is_boolean() const;
    bool is_discarded() const;
    bool empty() const;
    std::size_t size() const;
    std::string dump() const;

private:
    config_ptr ptr_;
};

// An immutable, consistent view of every config file at one generation.
// Take one snapshot per request/operation and pass it by
// `const config_snapshot&`.
class config_snapshot {
public:
    explicit config_snapshot(std::shared_ptr<const config_state> state);

    config_handle get(const std::string& name) const;

    config_handle getDamageTypes() const;
    config_handle getFiefdomBuildingTypes() const;
    config_handle getPlayerCombatants() const;
    config_handle getEnemyCombatants() const;
    config_handle getHeroes() const;
    config_handle getFiefdomOfficials() const;
    config_handle getWallConfig() const;
    config_handle getMiniGames() const;
    config_handle getTowerDefenseMobs() const;
    config_handle getTowerDefenseTowers() const;
    config_handle getTowerDefenseUnits() const;
    config_handle getTowerDefenseUnitUnlocks() const;
    config_handle getTowerDefenseProjectiles() const;
    config_handle getTowerDefenseWaveTemplates() const;
    config_handle getTowerDefenseOngoing() const;
    config_handle getWeedingPlants() const;
    config_handle getWeedingTools() const;
    config_handle getWeedingSpecials() const;
    config_handle getWeedingOngoing() const;
    config_handle getEconomyConfig() const;
    config_handle getRetinueConfig() const;
    config_handle getEquipmentConfig() const;
    config_handle getItemsConfig() const;
    config_handle getTechTreesConfig() const;
    config_handle getManorUi() const;
    config_handle getManorRiver() const;
    config_handle getPortraitFrames() const;
    config_handle getCombatRulesets() const;
    config_handle getCombatWarbands() const;
    config_handle getCombatStructures() const;
    config_handle getCombatFormations() const;
    config_handle getCombatProfiles() const;
    config_handle getCombatRewards() const;
    config_handle getCombatOfferings() const;
    config_handle getCosmetics() const;
    config_handle getProgression() const;
    config_handle getChatConfig() const;
    config_handle getChatAdmins() const;
    config_handle getSafeMessages() const;
    config_handle getSafeBaronyNames() const;
    config_handle getSafeBaronyTags() const;
    config_handle getSafeReactions() const;
    config_handle getSafeGifs() const;
    config_handle getSafeBanReasons() const;
    config_handle getTranslationConfig() const;
    config_handle getPushConfig() const;
    config_handle getRegistrationConfig() const;
    config_handle getReeveConfig() const;

    // Dynamic configs (discovered during reload). These return copies so a
    // caller can never hold a reference into a superseded snapshot.
    std::optional<nlohmann::json> getTowerDefenseSpawnSchedule(const std::string& filename) const;
    std::optional<nlohmann::json> loadWeedingMap(const std::string& filename) const;
    std::optional<nlohmann::json> loadWeedingLevelConfig(const std::string& filename) const;

    int getRetinueCapacity(int manor_level) const;
    int getBuildingArableAcres(const std::string& type_id) const;
    double getArableLandByLevel(int manor_level) const;
    int getBuildingForestAcres(const std::string& type_id) const;
    double getForestLandByLevel(int manor_level) const;
    std::string getBuildingClass(const std::string& type_id) const;
    std::string getBuildingExclusiveGroup(const std::string& type_id) const;

    nlohmann::json getAllConfigs() const;
    std::int64_t getConfigGeneration(const std::string& name) const;
    std::int64_t generation() const;

private:
    std::shared_ptr<const config_state> state_;
};

// Lifecycle and reload only. There are deliberately no typed config getters
// here: every read must go through an explicit `config_snapshot`.
class GameConfigCache {
public:
    GameConfigCache() = default;

    bool initialize(const std::string& config_dir);
    config_snapshot snapshot() const;

    // Force-reload every tracked file. All-or-nothing: if any file fails to
    // read, parse, or postprocess, the previous state stays active.
    bool reloadConfigs(config_reload_result& result);

    // Reload only files whose mtime changed (plus newly discovered dynamic
    // files). All-or-nothing in the same way.
    bool reloadChangedConfigs(config_reload_result& result);

    bool isLoaded() const;

    void addReloadCallback(config_reload_callback callback);

    // Prepare/commit are public so `ConfigReloadService` can prepare every
    // cache before committing any of them (cross-cache all-or-nothing).
    // Ordinary callers should use `reloadConfigs` / `reloadChangedConfigs`.
    std::shared_ptr<const config_state> prepareReload(bool force_all,
                                                      config_reload_result& result);
    void commitReload(std::shared_ptr<const config_state> next,
                      config_reload_result& result);

    // ------------------------------------------------------------------
    // Temporary compatibility wrappers (migration in progress).
    //
    // Each wrapper takes a fresh snapshot and returns a handle. The handle
    // keeps the underlying immutable JSON alive, so these are thread-safe,
    // but a caller that reads several files should take one explicit
    // `snapshot()` for cross-file consistency. These are removed once every
    // call site passes a `config_snapshot` explicitly.
    // ------------------------------------------------------------------
    config_handle getDamageTypes() const { return snapshot().getDamageTypes(); }
    config_handle getFiefdomBuildingTypes() const { return snapshot().getFiefdomBuildingTypes(); }
    config_handle getPlayerCombatants() const { return snapshot().getPlayerCombatants(); }
    config_handle getEnemyCombatants() const { return snapshot().getEnemyCombatants(); }
    config_handle getHeroes() const { return snapshot().getHeroes(); }
    config_handle getFiefdomOfficials() const { return snapshot().getFiefdomOfficials(); }
    config_handle getWallConfig() const { return snapshot().getWallConfig(); }
    config_handle getMiniGames() const { return snapshot().getMiniGames(); }
    config_handle getTowerDefenseMobs() const { return snapshot().getTowerDefenseMobs(); }
    config_handle getTowerDefenseTowers() const { return snapshot().getTowerDefenseTowers(); }
    config_handle getTowerDefenseUnits() const { return snapshot().getTowerDefenseUnits(); }
    config_handle getTowerDefenseUnitUnlocks() const { return snapshot().getTowerDefenseUnitUnlocks(); }
    config_handle getTowerDefenseProjectiles() const { return snapshot().getTowerDefenseProjectiles(); }
    config_handle getTowerDefenseWaveTemplates() const { return snapshot().getTowerDefenseWaveTemplates(); }
    config_handle getTowerDefenseOngoing() const { return snapshot().getTowerDefenseOngoing(); }
    config_handle getWeedingPlants() const { return snapshot().getWeedingPlants(); }
    config_handle getWeedingTools() const { return snapshot().getWeedingTools(); }
    config_handle getWeedingSpecials() const { return snapshot().getWeedingSpecials(); }
    config_handle getWeedingOngoing() const { return snapshot().getWeedingOngoing(); }
    config_handle getEconomyConfig() const { return snapshot().getEconomyConfig(); }
    config_handle getRetinueConfig() const { return snapshot().getRetinueConfig(); }
    config_handle getEquipmentConfig() const { return snapshot().getEquipmentConfig(); }
    config_handle getItemsConfig() const { return snapshot().getItemsConfig(); }
    config_handle getTechTreesConfig() const { return snapshot().getTechTreesConfig(); }
    config_handle getManorUi() const { return snapshot().getManorUi(); }
    config_handle getManorRiver() const { return snapshot().getManorRiver(); }
    config_handle getPortraitFrames() const { return snapshot().getPortraitFrames(); }
    config_handle getCombatRulesets() const { return snapshot().getCombatRulesets(); }
    config_handle getCombatWarbands() const { return snapshot().getCombatWarbands(); }
    config_handle getCombatStructures() const { return snapshot().getCombatStructures(); }
    config_handle getCombatFormations() const { return snapshot().getCombatFormations(); }
    config_handle getCombatProfiles() const { return snapshot().getCombatProfiles(); }
    config_handle getCombatRewards() const { return snapshot().getCombatRewards(); }
    config_handle getCombatOfferings() const { return snapshot().getCombatOfferings(); }
    config_handle getCosmetics() const { return snapshot().getCosmetics(); }
    config_handle getProgression() const { return snapshot().getProgression(); }
    config_handle getChatConfig() const { return snapshot().getChatConfig(); }
    config_handle getChatAdmins() const { return snapshot().getChatAdmins(); }
    config_handle getSafeMessages() const { return snapshot().getSafeMessages(); }
    config_handle getSafeBaronyNames() const { return snapshot().getSafeBaronyNames(); }
    config_handle getSafeBaronyTags() const { return snapshot().getSafeBaronyTags(); }
    config_handle getSafeReactions() const { return snapshot().getSafeReactions(); }
    config_handle getSafeGifs() const { return snapshot().getSafeGifs(); }
    config_handle getSafeBanReasons() const { return snapshot().getSafeBanReasons(); }
    config_handle getTranslationConfig() const { return snapshot().getTranslationConfig(); }
    config_handle getPushConfig() const { return snapshot().getPushConfig(); }
    config_handle getRegistrationConfig() const { return snapshot().getRegistrationConfig(); }
    config_handle getReeveConfig() const { return snapshot().getReeveConfig(); }

    std::optional<nlohmann::json> getTowerDefenseSpawnSchedule(const std::string& filename) const;
    std::optional<nlohmann::json> loadWeedingMap(const std::string& filename) const;
    std::optional<nlohmann::json> loadWeedingLevelConfig(const std::string& filename) const;

    int getRetinueCapacity(int manor_level) const { return snapshot().getRetinueCapacity(manor_level); }
    int getBuildingArableAcres(const std::string& type_id) const { return snapshot().getBuildingArableAcres(type_id); }
    double getArableLandByLevel(int manor_level) const { return snapshot().getArableLandByLevel(manor_level); }
    int getBuildingForestAcres(const std::string& type_id) const { return snapshot().getBuildingForestAcres(type_id); }
    double getForestLandByLevel(int manor_level) const { return snapshot().getForestLandByLevel(manor_level); }
    std::string getBuildingClass(const std::string& type_id) const { return snapshot().getBuildingClass(type_id); }
    std::string getBuildingExclusiveGroup(const std::string& type_id) const { return snapshot().getBuildingExclusiveGroup(type_id); }
    nlohmann::json getAllConfigs() const { return snapshot().getAllConfigs(); }
    std::int64_t getConfigGeneration(const std::string& name) const { return snapshot().getConfigGeneration(name); }

private:
    config_handle loadDynamicConfig(const std::string& name) const;
    std::vector<std::string> collectConfigNames() const;

    bool loadConfigSnapshot(const std::string& name,
                            const std::string& path,
                            config_ptr& out,
                            std::string& error) const;

    void rebuildDerivedMaps(const std::shared_ptr<config_state>& state) const;

    std::string config_dir_;
    std::vector<std::string> tracked_names_;
    std::unordered_map<std::string, void (*)(nlohmann::json&)> postprocessors_;

    mutable std::mutex state_mutex_;
    mutable std::shared_ptr<const config_state> state_;
    mutable std::mutex dynamic_load_mutex_;

    mutable std::mutex callbacks_mutex_;
    std::vector<config_reload_callback> callbacks_;

    std::atomic<bool> loaded_{false};
};
