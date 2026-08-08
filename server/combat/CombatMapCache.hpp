#pragma once
#include <nlohmann/json.hpp>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <ctime>

// Loads combat map files from game/config/combat/maps/ with a lenient,
// forward-compatible parse: known camelCase fields are normalized to
// snake_case, unknown fields are PRESERVED (not rejected) so the format can
// evolve without breaking existing maps. See docs/combat_maps.md.
class CombatMapCache {
public:
    static CombatMapCache& get_instance();

    void initialize(const std::string& maps_dir);
    bool is_initialized() const { return !maps_dir_.empty(); }

    // Returns a normalized copy of the map, or an empty json if unknown.
    nlohmann::json get_map(const std::string& filename);
    std::vector<std::string> list_maps();

private:
    void scan_directory();
    static void normalize_map_metadata(nlohmann::json& data);
    static void rename_field(nlohmann::json& obj, const char* from, const char* to);

    std::string maps_dir_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> cache_;
    time_t last_scan_time_ = 0;
};
