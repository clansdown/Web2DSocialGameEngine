#include "CombatMapCache.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

namespace {
// Rename a top-level key (used for camelCase -> snake_case normalization).
void rename_field_impl(nlohmann::json& obj, const char* from, const char* to) {
    auto it = obj.find(from);
    if (it != obj.end()) {
        obj[to] = std::move(it.value());
        obj.erase(it);
    }
}
} // namespace

CombatMapCache& CombatMapCache::get_instance() {
    static CombatMapCache instance;
    return instance;
}

void CombatMapCache::initialize(const std::string& maps_dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    maps_dir_ = maps_dir;
    last_scan_time_ = 0;
    cache_.clear();
    scan_directory();
}

void CombatMapCache::scan_directory() {
    if (maps_dir_.empty()) return;

    // POSIX stat(): std::filesystem::file_time_type comparisons are broken on
    // this Linux (see AGENTS.md "Filesystem Caching").
    struct stat st;
    if (stat(maps_dir_.c_str(), &st) != 0) {
        std::cerr << "[CombatMapCache] maps directory not found: " << maps_dir_ << std::endl;
        return;
    }
    if (!cache_.empty() && st.st_mtime <= last_scan_time_) return;
    last_scan_time_ = st.st_mtime;

    DIR* dir = opendir(maps_dir_.c_str());
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name(entry->d_name);
        if (name.size() < 5 || name.substr(name.size() - 5) != ".json") continue;
        std::string path = maps_dir_ + "/" + name;
        try {
            std::ifstream file(path);
            if (!file) continue;
            nlohmann::json data = nlohmann::json::parse(file, nullptr, true, true, true);
            cache_[name] = std::move(data);
        } catch (const std::exception& e) {
            std::cerr << "[CombatMapCache] failed to parse map " << path << ": "
                      << e.what() << std::endl;
        }
    }
    closedir(dir);
    std::cout << "[CombatMapCache] scanned " << cache_.size() << " map file(s) in "
              << maps_dir_ << std::endl;
}

void CombatMapCache::normalize_map_metadata(nlohmann::json& data) {
    if (!data.is_object()) return;
    rename_field_impl(data, "formatVersion", "format_version");
    rename_field_impl(data, "imageFilename", "image_filename");
    rename_field_impl(data, "widthTiles", "width_tiles");
    rename_field_impl(data, "heightTiles", "height_tiles");
    rename_field_impl(data, "tileSizePx", "tile_size_px");
    rename_field_impl(data, "tileCosts", "tile_costs");
    rename_field_impl(data, "spawnPoints", "spawn_points");
    rename_field_impl(data, "startPositions", "start_positions");
    // Unknown/extra fields are intentionally left untouched so the format can
    // grow without breaking the loader (see docs/combat_maps.md).
}

nlohmann::json CombatMapCache::get_map(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_directory();
    auto it = cache_.find(filename);
    if (it == cache_.end()) return nlohmann::json();
    nlohmann::json copy = it->second;
    normalize_map_metadata(copy);
    return copy;
}

std::vector<std::string> CombatMapCache::list_maps() {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_directory();
    std::vector<std::string> names;
    names.reserve(cache_.size());
    for (const auto& [name, data] : cache_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}
