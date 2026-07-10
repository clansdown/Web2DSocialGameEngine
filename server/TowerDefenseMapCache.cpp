#include "TowerDefenseMapCache.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <sys/stat.h>

/** Rename a field in a JSON object if it exists. */
static void rename_field(nlohmann::json& obj, const std::string& from, const std::string& to) {
    auto it = obj.find(from);
    if (it != obj.end()) {
        obj[to] = std::move(*it);
        obj.erase(from);
    }
}

/** Normalize map metadata from camelCase to snake_case for client consumption. */
static void normalize_map_metadata(nlohmann::json& map) {
    rename_field(map, "formatVersion", "format_version");
    rename_field(map, "imageFilename", "image_filename");
    rename_field(map, "spawnPoints", "spawn_points");
    rename_field(map, "exclusionZones", "exclusion_zones");
    rename_field(map, "endPoints", "end_points");

    // spawn_points
    if (map.contains("spawn_points") && map["spawn_points"].is_array()) {
        for (auto& sp : map["spawn_points"]) {
            rename_field(sp, "intervalMs", "interval_ms");
            rename_field(sp, "initialDelayMs", "initial_delay_ms");
            rename_field(sp, "targetPathId", "target_path_id");
        }
    }

    // paths
    if (map.contains("paths") && map["paths"].is_array()) {
        for (auto& path : map["paths"]) {
            rename_field(path, "endAtIntersectionId", "end_at_intersection_id");
            rename_field(path, "endAtEndPointId", "end_at_end_point_id");
            if (path.contains("waypoints")) {
                for (auto& wp : path["waypoints"]) {
                    rename_field(wp, "x", "x");
                    // x and y stay as-is
                }
            }
        }
    }

    // intersections
    if (map.contains("intersections") && map["intersections"].is_array()) {
        for (auto& ints : map["intersections"]) {
            rename_field(ints, "x", "x");
            // branches
            if (ints.contains("branches") && ints["branches"].is_array()) {
                for (auto& br : ints["branches"]) {
                    rename_field(br, "pathId", "path_id");
                }
            }
        }
    }

    // exclusion_zones
    if (map.contains("exclusion_zones") && map["exclusion_zones"].is_array()) {
        for (auto& zone : map["exclusion_zones"]) {
            rename_field(zone, "centerX", "center_x");
            rename_field(zone, "centerY", "center_y");
            if (zone.contains("vertices") && zone["vertices"].is_array()) {
                for (auto& v : zone["vertices"]) {
                    (void)v; // x and y stay
                }
            }
        }
    }

    // end_points
    if (map.contains("end_points") && map["end_points"].is_array()) {
        for (auto& ep : map["end_points"]) {
            (void)ep; // id, label, x, y stay
        }
    }
}

TowerDefenseMapCache& TowerDefenseMapCache::get_instance() {
    static TowerDefenseMapCache instance;
    return instance;
}

void TowerDefenseMapCache::set_maps_directory(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    maps_dir_ = path;
    last_scan_time_ = 0;
}

bool TowerDefenseMapCache::initialize(const std::string& path) {
    set_maps_directory(path);
    scan_directory();
    std::cerr << "[TowerDefenseMapCache] Loaded " << cache_.size()
              << " maps from " << path
              << " (cwd=" << std::filesystem::current_path().string() << ")"
              << std::endl;
    return cache_.size() > 0;
}

void TowerDefenseMapCache::scan_directory() {
    if (maps_dir_.empty()) {
        std::cerr << "[TowerDefenseMapCache] scan_directory: maps_dir_ empty" << std::endl;
        return;
    }

    struct stat st;
    if (stat(maps_dir_.c_str(), &st) != 0) {
        std::cerr << "[TowerDefenseMapCache] Cannot stat: " << maps_dir_
                  << " (cwd=" << std::filesystem::current_path().string()
                  << "): " << strerror(errno) << std::endl;
        return;
    }

    if (!(st.st_mode & S_IFDIR)) {
        std::cerr << "[TowerDefenseMapCache] Not a directory: " << maps_dir_ << std::endl;
        return;
    }

    if (!cache_.empty() && st.st_mtime <= last_scan_time_) {
        std::cerr << "[TowerDefenseMapCache] scan_directory: no change ("
                  << cache_.size() << " cached)" << std::endl;
        return;
    }

    cache_.clear();

    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(maps_dir_, ec)) {
        if (!entry.is_regular_file()) continue;

        std::string path = entry.path().string();
        std::string filename = entry.path().filename().string();

        if (filename.size() < 5 || filename.substr(filename.size() - 5) != ".json") {
            continue;
        }

        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[TowerDefenseMapCache] Failed to open: " << path << std::endl;
            continue;
        }

        try {
            nlohmann::json map_data = nlohmann::json::parse(
                std::string((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>())
            );

            cache_[filename] = std::move(map_data);
        } catch (const std::exception& e) {
            std::cerr << "[TowerDefenseMapCache] Failed to parse " << filename << ": " << e.what() << std::endl;
        }
    }

    std::cerr << "[TowerDefenseMapCache] Scanned " << cache_.size() << " map(s):";
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        std::cerr << " " << it->first;
    }
    std::cerr << std::endl;

    last_scan_time_ = st.st_mtime;
}

std::optional<nlohmann::json> TowerDefenseMapCache::get_map(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_directory();

    auto it = cache_.find(filename);
    if (it != cache_.end()) {
        auto result = it->second;
        normalize_map_metadata(result);
        return result;
    }

    std::cerr << "[TowerDefenseMapCache] get_map('" << filename
              << "') MISS — cache has " << cache_.size() << " entries";
    if (cache_.size() > 0) {
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            std::cerr << " '" << it->first << "'";
        }
    }
    std::cerr << std::endl;

    return std::nullopt;
}

std::vector<std::string> TowerDefenseMapCache::list_maps() {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_directory();

    std::vector<std::string> names;
    names.reserve(cache_.size());
    for (const auto& pair : cache_) {
        names.push_back(pair.first);
    }
    return names;
}
