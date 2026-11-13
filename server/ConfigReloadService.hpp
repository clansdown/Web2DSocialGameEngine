#pragma once
#include "GameConfigCache.hpp"

#include <atomic>
#include <thread>

class TowerDefenseMapCache;
class CombatMapCache;

// Owns the SIGHUP-driven reload worker. Prepares every cache first, then
// commits them all, so a parse failure in any one leaves the whole previous
// configuration active.
class ConfigReloadService {
public:
    ConfigReloadService() = default;
    ~ConfigReloadService();

    ConfigReloadService(const ConfigReloadService&) = delete;
    ConfigReloadService& operator=(const ConfigReloadService&) = delete;

    void start(GameConfigCache& config,
               TowerDefenseMapCache& td_maps,
               CombatMapCache& combat_maps);

    // Thread-safe programmatic trigger (used by tests and tooling).
    void requestReload();

    // Synchronous reload for tests and the deploy helper's diagnostics.
    bool reloadNow();

    void shutdown();

private:
    void workerLoop();
    bool reloadAll(config_reload_result& result);

    GameConfigCache* config_ = nullptr;
    TowerDefenseMapCache* td_maps_ = nullptr;
    CombatMapCache* combat_maps_ = nullptr;

    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> reload_requested_{false};
    bool started_ = false;
};
