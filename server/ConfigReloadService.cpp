#include "ConfigReloadService.hpp"
#include "TowerDefenseMapCache.hpp"
#include "combat/CombatMapCache.hpp"

#include <chrono>
#include <csignal>
#include <iostream>

namespace {

// Async-signal-safe: only a sig_atomic_t write.
volatile sig_atomic_t g_reload_requested = 0;

void handle_sighup(int) {
    g_reload_requested = 1;
}

void install_sighup_handler() {
    struct sigaction action {};
    action.sa_handler = handle_sighup;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &action, nullptr);
}

} // namespace

ConfigReloadService::~ConfigReloadService() {
    shutdown();
}

void ConfigReloadService::start(GameConfigCache& config,
                                TowerDefenseMapCache& td_maps,
                                CombatMapCache& combat_maps) {
    if (started_) {
        return;
    }
    config_ = &config;
    td_maps_ = &td_maps;
    combat_maps_ = &combat_maps;

    install_sighup_handler();
    stopping_ = false;
    started_ = true;
    worker_ = std::thread([this]() { workerLoop(); });
}

void ConfigReloadService::requestReload() {
    reload_requested_ = true;
}

bool ConfigReloadService::reloadNow() {
    config_reload_result result;
    return reloadAll(result);
}

void ConfigReloadService::shutdown() {
    if (!started_) {
        return;
    }
    stopping_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    started_ = false;
}

void ConfigReloadService::workerLoop() {
    while (!stopping_) {
        const bool signal_requested = (g_reload_requested != 0);
        const bool programmatic_requested = reload_requested_.exchange(false);
        if (signal_requested || programmatic_requested) {
            g_reload_requested = 0;
            config_reload_result result;
            reloadAll(result);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

bool ConfigReloadService::reloadAll(config_reload_result& result) {
    if (!config_ || !td_maps_ || !combat_maps_) {
        result.error = "reload service is not started";
        return false;
    }

    config_reload_result config_result;
    config_reload_result td_result;
    config_reload_result combat_result;

    // Prepare everything first. Any failure leaves every cache untouched.
    auto next_config = config_->prepareReload(false, config_result);
    if (!next_config) {
        std::cerr << "[GameConfigCache] Reload aborted: "
                  << config_result.error << std::endl;
        result = config_result;
        return false;
    }

    auto next_td = td_maps_->prepareReload(td_result);
    if (!next_td) {
        std::cerr << "[GameConfigCache] Reload aborted: tower-defense maps: "
                  << td_result.error << std::endl;
        result = td_result;
        return false;
    }

    auto next_combat = combat_maps_->prepareReload(combat_result);
    if (!next_combat) {
        std::cerr << "[GameConfigCache] Reload aborted: combat maps: "
                  << combat_result.error << std::endl;
        result = combat_result;
        return false;
    }

    // Commit all. Each commit is individually atomic; the small cross-cache
    // window is accepted.
    td_maps_->commitReload(std::move(next_td));
    combat_maps_->commitReload(std::move(next_combat));
    config_->commitReload(std::move(next_config), config_result);

    result = config_result;
    result.changed_files.insert(result.changed_files.end(),
                                td_result.changed_files.begin(),
                                td_result.changed_files.end());
    result.changed_files.insert(result.changed_files.end(),
                                combat_result.changed_files.begin(),
                                combat_result.changed_files.end());
    return true;
}
