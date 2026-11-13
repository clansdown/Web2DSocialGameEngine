#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <memory>
#include <mutex>
#include <ctime>

struct config_reload_result;

// Loads tower-defense map files from game/config/tower_defense/maps/.
//
// Maps are immutable once loaded. A reload builds a complete new state and
// swaps it atomically. Per-file mtimes (not just the directory mtime) are
// tracked so an in-place map edit is detected.
class TowerDefenseMapCache {
public:
    struct td_map_state {
        std::unordered_map<std::string, nlohmann::json> files;
        std::unordered_map<std::string, std::time_t> mtimes;
    };

    static TowerDefenseMapCache& get_instance();

    void set_maps_directory(const std::string& path);

    std::optional<nlohmann::json> get_map(const std::string& filename);

    std::vector<std::string> list_maps();

    bool initialize(const std::string& path);

    // All-or-nothing prepare: any unreadable/unparseable map aborts the whole
    // reload and leaves the previous state active.
    std::shared_ptr<const td_map_state> prepareReload(config_reload_result& result);

    void commitReload(std::shared_ptr<const td_map_state> next);

    bool reloadChanged(config_reload_result& result);

private:
    TowerDefenseMapCache() = default;
    TowerDefenseMapCache(const TowerDefenseMapCache&) = delete;
    TowerDefenseMapCache& operator=(const TowerDefenseMapCache&) = delete;

    // Lazy auto-rescan used by get_map/list_maps. Parse failures keep the old
    // entry (never abort an ordinary read).
    void scan_directory_locked();

    bool read_map_file(const std::string& path, nlohmann::json& out,
                       std::string& error) const;

    std::string maps_dir_;
    std::shared_ptr<const td_map_state> state_;
    std::mutex mutex_;
};
