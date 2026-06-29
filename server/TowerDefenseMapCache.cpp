#include "TowerDefenseMapCache.hpp"
#include <fstream>
#include <iostream>

TowerDefenseMapCache& TowerDefenseMapCache::get_instance() {
    static TowerDefenseMapCache instance;
    return instance;
}

void TowerDefenseMapCache::set_maps_directory(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    maps_dir_ = path;
    last_scan_time_ = std::filesystem::file_time_type{};
}

bool TowerDefenseMapCache::initialize(const std::string& path) {
    set_maps_directory(path);
    scan_directory();
    std::cerr << "[TowerDefenseMapCache] Loaded " << cache_.size() << " maps from " << path << std::endl;
    return true;
}

void TowerDefenseMapCache::scan_directory() {
    if (maps_dir_.empty()) return;

    namespace fs = std::filesystem;

    std::error_code ec;
    auto dir_time = fs::last_write_time(maps_dir_, ec);
    if (ec) {
        return;
    }

    if (dir_time <= last_scan_time_) {
        return;
    }

    cache_.clear();

    if (!fs::is_directory(maps_dir_, ec)) {
        std::cerr << "[TowerDefenseMapCache] Not a directory: " << maps_dir_ << std::endl;
        return;
    }

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

    last_scan_time_ = dir_time;
}

std::optional<nlohmann::json> TowerDefenseMapCache::get_map(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    scan_directory();

    auto it = cache_.find(filename);
    if (it != cache_.end()) {
        return it->second;
    }

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
