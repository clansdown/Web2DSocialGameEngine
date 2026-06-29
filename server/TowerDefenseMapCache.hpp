#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <filesystem>
#include <mutex>

class TowerDefenseMapCache {
public:
    static TowerDefenseMapCache& get_instance();

    void set_maps_directory(const std::string& path);

    std::optional<nlohmann::json> get_map(const std::string& filename);

    std::vector<std::string> list_maps();

    bool initialize(const std::string& path);

private:
    TowerDefenseMapCache() = default;
    TowerDefenseMapCache(const TowerDefenseMapCache&) = delete;
    TowerDefenseMapCache& operator=(const TowerDefenseMapCache&) = delete;

    void scan_directory();

    std::string maps_dir_;
    std::unordered_map<std::string, nlohmann::json> cache_;
    std::filesystem::file_time_type last_scan_time_;
    std::mutex mutex_;
};
