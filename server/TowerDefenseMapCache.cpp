#include "TowerDefenseMapCache.hpp"
#include "GameConfigCache.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <set>
#include <filesystem>
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

    if (map.contains("spawn_points") && map["spawn_points"].is_array()) {
        for (auto& sp : map["spawn_points"]) {
            rename_field(sp, "intervalMs", "interval_ms");
            rename_field(sp, "initialDelayMs", "initial_delay_ms");
            rename_field(sp, "targetPathId", "target_path_id");
        }
    }

    if (map.contains("paths") && map["paths"].is_array()) {
        for (auto& path : map["paths"]) {
            rename_field(path, "endAtIntersectionId", "end_at_intersection_id");
            rename_field(path, "endAtEndPointId", "end_at_end_point_id");
        }
    }

    if (map.contains("intersections") && map["intersections"].is_array()) {
        for (auto& ints : map["intersections"]) {
            if (ints.contains("branches") && ints["branches"].is_array()) {
                for (auto& br : ints["branches"]) {
                    rename_field(br, "pathId", "path_id");
                }
            }
        }
    }

    if (map.contains("exclusion_zones") && map["exclusion_zones"].is_array()) {
        for (auto& zone : map["exclusion_zones"]) {
            rename_field(zone, "centerX", "center_x");
            rename_field(zone, "centerY", "center_y");
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
    state_.reset();
}

bool TowerDefenseMapCache::read_map_file(const std::string& path,
                                         nlohmann::json& out,
                                         std::string& error) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        error = "cannot open " + path;
        return false;
    }
    try {
        out = nlohmann::json::parse(
            std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>()),
            nullptr, true, true, true);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::shared_ptr<const TowerDefenseMapCache::td_map_state>
TowerDefenseMapCache::prepareReload(config_reload_result& result) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (maps_dir_.empty()) {
        return state_;
    }

    struct stat dir_st {};
    if (stat(maps_dir_.c_str(), &dir_st) != 0 || !(dir_st.st_mode & S_IFDIR)) {
        result.error = "maps directory not found: " + maps_dir_;
        return nullptr;
    }

    auto next = std::make_shared<td_map_state>();
    if (state_) {
        next->files = state_->files;
        next->mtimes = state_->mtimes;
    }

    std::set<std::string> seen;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(maps_dir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;

        const std::string filename = entry.path().filename().string();
        if (filename.size() < 5 ||
            filename.compare(filename.size() - 5, 5, ".json") != 0) {
            continue;
        }
        seen.insert(filename);

        const std::string path = entry.path().string();
        struct stat st {};
        if (stat(path.c_str(), &st) != 0) {
            result.failed_files.push_back(filename);
            if (!result.error.empty()) result.error += "; ";
            result.error += filename + ": cannot stat " + path;
            return nullptr;
        }

        const auto mtime_it = next->mtimes.find(filename);
        const bool unchanged =
            mtime_it != next->mtimes.end() &&
            mtime_it->second == st.st_mtime &&
            next->files.find(filename) != next->files.end();
        if (unchanged) {
            continue;
        }

        nlohmann::json data;
        std::string error;
        if (!read_map_file(path, data, error)) {
            result.failed_files.push_back(filename);
            if (!result.error.empty()) result.error += "; ";
            result.error += filename + ": " + error;
            return nullptr;
        }

        next->files[filename] = std::move(data);
        next->mtimes[filename] = st.st_mtime;
        result.changed_files.push_back(filename);
    }

    if (ec) {
        result.error = "directory iteration error: " + ec.message();
        return nullptr;
    }

    // Drop maps that no longer exist on disk.
    for (auto it = next->files.begin(); it != next->files.end();) {
        if (seen.find(it->first) == seen.end()) {
            next->mtimes.erase(it->first);
            it = next->files.erase(it);
        } else {
            ++it;
        }
    }

    return next;
}

void TowerDefenseMapCache::commitReload(std::shared_ptr<const td_map_state> next) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = std::move(next);
}

bool TowerDefenseMapCache::reloadChanged(config_reload_result& result) {
    auto next = prepareReload(result);
    if (!next) {
        std::cerr << "[TowerDefenseMapCache] Reload aborted: " << result.error << std::endl;
        return false;
    }
    commitReload(std::move(next));
    std::cerr << "[TowerDefenseMapCache] Reload complete: changed="
              << result.changed_files.size() << std::endl;
    return true;
}

bool TowerDefenseMapCache::initialize(const std::string& path) {
    set_maps_directory(path);
    config_reload_result result;
    auto next = prepareReload(result);
    if (!next) {
        std::cerr << "[TowerDefenseMapCache] Initialization failed: "
                  << result.error << std::endl;
        return false;
    }
    commitReload(std::move(next));

    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = state_ ? state_->files.size() : 0;
    std::cerr << "[TowerDefenseMapCache] Loaded " << count
              << " maps from " << path
              << " (cwd=" << std::filesystem::current_path().string() << ")"
              << std::endl;
    return count > 0;
}

void TowerDefenseMapCache::scan_directory_locked() {
    if (maps_dir_.empty()) {
        return;
    }

    struct stat dir_st {};
    if (stat(maps_dir_.c_str(), &dir_st) != 0 || !(dir_st.st_mode & S_IFDIR)) {
        std::cerr << "[TowerDefenseMapCache] Cannot stat: " << maps_dir_
                  << " (cwd=" << std::filesystem::current_path().string()
                  << "): " << strerror(errno) << std::endl;
        return;
    }

    auto next = std::make_shared<td_map_state>();
    if (state_) {
        next->files = state_->files;
        next->mtimes = state_->mtimes;
    }

    std::set<std::string> seen;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(maps_dir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;

        const std::string filename = entry.path().filename().string();
        if (filename.size() < 5 ||
            filename.compare(filename.size() - 5, 5, ".json") != 0) {
            continue;
        }
        seen.insert(filename);

        const std::string path = entry.path().string();
        struct stat st {};
        if (stat(path.c_str(), &st) != 0) {
            std::cerr << "[TowerDefenseMapCache] Cannot stat: " << path << std::endl;
            continue;
        }

        const auto mtime_it = next->mtimes.find(filename);
        if (mtime_it != next->mtimes.end() &&
            mtime_it->second == st.st_mtime &&
            next->files.find(filename) != next->files.end()) {
            continue;
        }

        nlohmann::json data;
        std::string error;
        if (!read_map_file(path, data, error)) {
            std::cerr << "[TowerDefenseMapCache] Failed to parse "
                      << filename << ": " << error << std::endl;
            continue;
        }
        next->files[filename] = std::move(data);
        next->mtimes[filename] = st.st_mtime;
    }

    if (ec) {
        std::cerr << "[TowerDefenseMapCache] Warning: directory iteration error: "
                  << ec.message() << std::endl;
    }

    for (auto it = next->files.begin(); it != next->files.end();) {
        if (seen.find(it->first) == seen.end()) {
            next->mtimes.erase(it->first);
            it = next->files.erase(it);
        } else {
            ++it;
        }
    }

    state_ = std::move(next);
}

std::optional<nlohmann::json> TowerDefenseMapCache::get_map(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_directory_locked();

    if (!state_) {
        return std::nullopt;
    }
    const auto it = state_->files.find(filename);
    if (it == state_->files.end()) {
        std::cerr << "[TowerDefenseMapCache] get_map('" << filename
                  << "') MISS — cache has " << state_->files.size() << " entries"
                  << std::endl;
        return std::nullopt;
    }

    nlohmann::json result = it->second;
    normalize_map_metadata(result);
    return result;
}

std::vector<std::string> TowerDefenseMapCache::list_maps() {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_directory_locked();

    std::vector<std::string> names;
    if (!state_) return names;
    names.reserve(state_->files.size());
    for (const auto& pair : state_->files) {
        names.push_back(pair.first);
    }
    return names;
}
