#include "CombatMapCache.hpp"
#include "GameConfigCache.hpp"
#include <sys/stat.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        maps_dir_ = maps_dir;
        state_.reset();
    }

    config_reload_result result;
    auto next = prepareReload(result);
    if (!next) {
        std::cerr << "[CombatMapCache] Initialization failed: " << result.error << std::endl;
        return;
    }
    commitReload(std::move(next));

    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = state_ ? state_->files.size() : 0;
    std::cout << "[CombatMapCache] scanned " << count << " map file(s) in "
              << maps_dir << std::endl;
}

bool CombatMapCache::read_map_file(const std::string& path,
                                   nlohmann::json& out,
                                   std::string& error) const {
    std::ifstream file(path);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    try {
        out = nlohmann::json::parse(file, nullptr, true, true, true);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::shared_ptr<const CombatMapCache::combat_map_state>
CombatMapCache::prepareReload(config_reload_result& result) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (maps_dir_.empty()) {
        return state_;
    }

    struct stat dir_st {};
    if (stat(maps_dir_.c_str(), &dir_st) != 0 || !(dir_st.st_mode & S_IFDIR)) {
        result.error = "maps directory not found: " + maps_dir_;
        return nullptr;
    }

    auto next = std::make_shared<combat_map_state>();
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

void CombatMapCache::commitReload(std::shared_ptr<const combat_map_state> next) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = std::move(next);
}

bool CombatMapCache::reloadChanged(config_reload_result& result) {
    auto next = prepareReload(result);
    if (!next) {
        std::cerr << "[CombatMapCache] Reload aborted: " << result.error << std::endl;
        return false;
    }
    commitReload(std::move(next));
    std::cout << "[CombatMapCache] Reload complete: changed="
              << result.changed_files.size() << std::endl;
    return true;
}

void CombatMapCache::scan_directory_locked() {
    if (maps_dir_.empty()) return;

    struct stat dir_st {};
    if (stat(maps_dir_.c_str(), &dir_st) != 0 || !(dir_st.st_mode & S_IFDIR)) {
        std::cerr << "[CombatMapCache] maps directory not found: " << maps_dir_ << std::endl;
        return;
    }

    auto next = std::make_shared<combat_map_state>();
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
        if (stat(path.c_str(), &st) != 0) continue;

        const auto mtime_it = next->mtimes.find(filename);
        if (mtime_it != next->mtimes.end() &&
            mtime_it->second == st.st_mtime &&
            next->files.find(filename) != next->files.end()) {
            continue;
        }

        nlohmann::json data;
        std::string error;
        if (!read_map_file(path, data, error)) {
            std::cerr << "[CombatMapCache] failed to parse map " << path << ": "
                      << error << std::endl;
            continue;
        }
        next->files[filename] = std::move(data);
        next->mtimes[filename] = st.st_mtime;
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
    scan_directory_locked();
    if (!state_) return nlohmann::json();
    const auto it = state_->files.find(filename);
    if (it == state_->files.end()) return nlohmann::json();
    nlohmann::json copy = it->second;
    normalize_map_metadata(copy);
    return copy;
}

std::vector<std::string> CombatMapCache::list_maps() {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_directory_locked();
    std::vector<std::string> names;
    if (!state_) return names;
    names.reserve(state_->files.size());
    for (const auto& [name, data] : state_->files) {
        (void)data;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}
