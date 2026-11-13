#pragma once
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <ctime>

struct config_reload_result;

// Loads combat map files from game/config/combat/maps/ with a lenient,
// forward-compatible parse: known camelCase fields are normalized to
// snake_case, unknown fields are PRESERVED (not rejected) so the format can
// evolve without breaking existing maps. See docs/combat_maps.md.
//
// Maps are immutable once loaded. A reload builds a complete new state and
// swaps it atomically. Per-file mtimes are tracked so in-place edits reload.
class CombatMapCache {
public:
    struct combat_map_state {
        std::unordered_map<std::string, nlohmann::json> files;
        std::unordered_map<std::string, std::time_t> mtimes;
    };

    static CombatMapCache& get_instance();

    void initialize(const std::string& maps_dir);
    bool is_initialized() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !maps_dir_.empty();
    }

    // Returns a normalized copy of the map, or an empty json if unknown.
    nlohmann::json get_map(const std::string& filename);
    std::vector<std::string> list_maps();

    // All-or-nothing prepare: any unreadable/unparseable map aborts the whole
    // reload and leaves the previous state active.
    std::shared_ptr<const combat_map_state> prepareReload(config_reload_result& result);
    void commitReload(std::shared_ptr<const combat_map_state> next);
    bool reloadChanged(config_reload_result& result);

private:
    void scan_directory_locked();
    bool read_map_file(const std::string& path, nlohmann::json& out,
                       std::string& error) const;
    static void normalize_map_metadata(nlohmann::json& data);
    static void rename_field(nlohmann::json& obj, const char* from, const char* to);

    std::string maps_dir_;
    mutable std::mutex mutex_;
    std::shared_ptr<const combat_map_state> state_;
};
