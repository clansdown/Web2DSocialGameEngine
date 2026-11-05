#include "ImageCache.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <sys/stat.h>

using json = nlohmann::json;

namespace {

// POSIX stat() of a directory's mtime (std::filesystem::file_time_type
// comparisons are broken on Linux). Returns 0 when the path is not a directory.
time_t directory_mtime(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    if (!(st.st_mode & S_IFDIR)) return 0;
    return st.st_mtime;
}

// Records the mtime of `dir` and every directory beneath it.
void record_dir_tree(const std::filesystem::path& dir,
                     std::map<std::string, time_t>& out) {
    std::error_code ec;
    out[dir.string()] = directory_mtime(dir.string());
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_directory()) record_dir_tree(entry.path(), out);
    }
}

} // namespace

/**
 * Gets the singleton instance of ImageCache.
 * 
 * @return Reference to the singleton ImageCache instance
 */
ImageCache& ImageCache::getInstance() {
    static ImageCache instance;
    return instance;
}

/**
 * Initializes the ImageCache by scanning the specified images directory.
 * 
 * This method traverses the images directory structure and builds an
 * in-memory cache of all available images grouped by asset type,
 * asset ID, and action.
 * 
 * @param images_dir Path to the images directory
 * @return true if initialization succeeded, false on error
 */
bool ImageCache::initialize(const std::string& images_dir) {
    namespace fs = std::filesystem;
    
    // Reset state
    images_ = json::object();
    loaded_ = false;
    total_image_count_ = 0;
    base_path_ = images_dir;
    watched_dirs_.clear();
    
    // Check if directory exists
    if (!fs::exists(images_dir) || !fs::is_directory(images_dir)) {
        std::cerr << "ImageCache: Directory does not exist: " << images_dir << std::endl;
        return false;
    }
    
    try {
        // Scan top-level directories (asset types: buildings, combatants, heroes, portraits)
        for (const auto& entry : fs::directory_iterator(images_dir)) {
            if (!entry.is_directory()) {
                continue;
            }
            
            std::string asset_type = entry.path().filename().string();
            
            // Skip hidden directories
            if (asset_type.empty() || asset_type[0] == '.') {
                continue;
            }
            
            // Initialize this asset type in our cache
            images_[asset_type] = json::object();
            
            // Scan asset ID directories within this asset type
            for (const auto& asset_entry : fs::directory_iterator(entry.path())) {
                if (!asset_entry.is_directory()) {
                    continue;
                }
                
                std::string asset_id = asset_entry.path().filename().string();
                
                // Skip hidden directories
                if (asset_id.empty() || asset_id[0] == '.') {
                    continue;
                }
                
                // Initialize this asset in our cache
                images_[asset_type][asset_id] = json::object();
                images_[asset_type][asset_id]["images"] = json::object();
                images_[asset_type][asset_id]["total_images"] = 0;
                
                // Scan action directories and image files
                scanDirectory(asset_entry.path().string(), asset_type);
            }
        }
        
        loaded_ = true;
        std::cout << "ImageCache: Loaded " << total_image_count_ << " images from " 
                  << images_dir << std::endl;
        record_watched_dirs();
        
    } catch (const fs::filesystem_error& e) {
        std::cerr << "ImageCache: Filesystem error during scan: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "ImageCache: Error during initialization: " << e.what() << std::endl;
        return false;
    }
    
    return true;
}

/**
 * Recursively scans a directory and adds found images to the cache.
 * 
 * This method handles the directory structure:
 *   {asset_type}/{asset_id}/{action}/{frame}.png
 * 
 * It determines whether a subdirectory is an "action" folder or just
 * contains images directly, and builds the appropriate JSON structure.
 * 
 * @param path The directory path to scan
 * @param asset_type The asset type (buildings, combatants, heroes, portraits)
 */
void ImageCache::scanDirectory(const std::string& path, const std::string& asset_type) {
    namespace fs = std::filesystem;
    
    try {
        // Get the parent directory path to determine asset_id
        fs::path parent_path = fs::path(path).parent_path();
        std::string parent_name = parent_path.filename().string();
        
        // The asset_id is the last component of the parent path (images/{asset_type}/{asset_id}/)
        // But we need to find it relative to our base structure
        fs::path images_path = fs::path(base_path_);
        
        // Get relative path from base
        fs::path relative_path;
        try {
            relative_path = fs::relative(parent_path, images_path);
        } catch (...) {
            relative_path = parent_path;
        }
        
        // Extract asset_id from the relative path
        std::string asset_id;
        if (relative_path.string().size() >= 1) {
            auto it = relative_path.begin();
            ++it; // Skip asset_type
            if (it != relative_path.end()) {
                asset_id = it->string();
            }
        }
        
        if (asset_id.empty() || asset_id[0] == '.') {
            return;
        }
        
        // Check if this directory contains image files directly (action = "root")
        // or contains subdirectories (actions)
        bool has_subdirs = false;
        bool has_images = false;
        
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_directory()) {
                has_subdirs = true;
            } else if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), 
                              [](unsigned char c){ return std::tolower(c); });
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".avif") {
                    has_images = true;
                }
            }
        }
        
        if (has_images && !has_subdirs) {
            // Images directly in this directory - use parent folder name as action
            // or "root" if at the asset level
            fs::path action_path = fs::path(path);
            std::string action = action_path.filename().string();
            
            // Count images and collect paths
            int count = 0;
            json paths = json::array();
            
            for (const auto& entry : fs::directory_iterator(path)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                             [](unsigned char c){ return std::tolower(c); });
                
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".avif") {
                    // Build relative path from images base
                    std::string relative_path_str;
                    try {
                        fs::path rel = fs::relative(entry.path(), fs::path(base_path_));
                        relative_path_str = rel.string();
                    } catch (...) {
                        relative_path_str = entry.path().string();
                    }
                    
                    paths.push_back(relative_path_str);
                    count++;
                    total_image_count_++;
                }
            }
            
            // Add to cache
            if (!images_[asset_type].contains(asset_id)) {
                images_[asset_type][asset_id] = json::object();
                images_[asset_type][asset_id]["images"] = json::object();
                images_[asset_type][asset_id]["total_images"] = 0;
            }
            
            images_[asset_type][asset_id]["images"][action] = json::object();
            images_[asset_type][asset_id]["images"][action]["count"] = count;
            images_[asset_type][asset_id]["images"][action]["paths"] = paths;
            
            // Update total for this asset
            int current_total = images_[asset_type][asset_id]["total_images"].get<int>();
            images_[asset_type][asset_id]["total_images"] = current_total + count;
            
        } else if (has_subdirs) {
            // Recurse into subdirectories (actions)
            for (const auto& entry : fs::directory_iterator(path)) {
                if (!entry.is_directory()) {
                    continue;
                }
                
                std::string action = entry.path().filename().string();
                
                // Skip hidden directories
                if (action.empty() || action[0] == '.') {
                    continue;
                }
                
                // Count images in this action directory
                int count = 0;
                json paths = json::array();
                
                for (const auto& img_entry : fs::directory_iterator(entry.path())) {
                    if (!img_entry.is_regular_file()) {
                        continue;
                    }
                    
                    std::string ext = img_entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                 [](unsigned char c){ return std::tolower(c); });
                    
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".avif") {
                        // Build relative path from images base
                        std::string relative_path_str;
                        try {
                            fs::path rel = fs::relative(img_entry.path(), fs::path(base_path_));
                            relative_path_str = rel.string();
                        } catch (...) {
                            relative_path_str = img_entry.path().string();
                        }
                        
                        paths.push_back(relative_path_str);
                        count++;
                        total_image_count_++;
                    }
                }
                
                // Add to cache
                if (!images_[asset_type].contains(asset_id)) {
                    images_[asset_type][asset_id] = json::object();
                    images_[asset_type][asset_id]["images"] = json::object();
                    images_[asset_type][asset_id]["total_images"] = 0;
                }
                
                images_[asset_type][asset_id]["images"][action] = json::object();
                images_[asset_type][asset_id]["images"][action]["count"] = count;
                images_[asset_type][asset_id]["images"][action]["paths"] = paths;
                
                // Update total for this asset
                int current_total = images_[asset_type][asset_id]["total_images"].get<int>();
                images_[asset_type][asset_id]["total_images"] = current_total + count;
            }
        }
        
    } catch (const fs::filesystem_error& e) {
        std::cerr << "ImageCache: Filesystem error scanning " << path << ": " << e.what() << std::endl;
    }
}

void ImageCache::record_watched_dirs() {
    watched_dirs_.clear();
    if (base_path_.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_directory(base_path_, ec)) return;
    record_dir_tree(base_path_, watched_dirs_);
}

void ImageCache::ensure_fresh() {
    if (!loaded_ || base_path_.empty() || watched_dirs_.empty()) return;
    for (const auto& [path, mtime] : watched_dirs_) {
        if (directory_mtime(path) != mtime) {
            // Something changed anywhere in the tree (an image was added,
            // removed, or renamed): rebuild the whole cache.
            initialize(base_path_);
            return;
        }
    }
}

/**
 * Returns all cached image data as a JSON object. Rescans first when the images
 * tree changed since the last scan (so new art appears without a restart).
 * 
 * @return Complete image cache structure with total_images and grouped by asset type
 */
json ImageCache::getImages() {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_fresh();
    if (!loaded_) {
        return json::object();
    }
    
    json result = images_;
    result["total_images"] = total_image_count_;
    
    return result;
}

/**
 * Returns image data for a specific asset type. Rescans first when the images
 * tree changed since the last scan.
 * 
 * @param type The asset type to retrieve
 * @return Image data for the type, or null if not found
 */
json ImageCache::getImagesByType(const std::string& type) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_fresh();
    if (!loaded_) {
        return json();
    }
    
    if (images_.contains(type)) {
        return images_[type];
    }
    
    return json();
}

/**
 * Returns image data for specific asset IDs across all asset types. Rescans
 * first when the images tree changed since the last scan.
 * 
 * @param ids Vector of asset IDs to filter by
 * @return Object containing only the specified asset IDs
 */
json ImageCache::getImagesByIds(const std::vector<std::string>& ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_fresh();
    if (!loaded_) {
        return json::object();
    }
    
    json result = json::object();
    
    for (const auto& id : ids) {
        // Search through all asset types for this ID
        for (auto& [asset_type, assets] : images_.items()) {
            if (assets.contains(id)) {
                result[id] = assets[id];
                break; // Found, don't check other types
            }
        }
    }
    
    return result;
}

/**
 * Gets image file paths for a specific asset and optional action. Rescans first
 * when the images tree changed since the last scan.
 * 
 * @param asset_id The asset identifier
 * @param asset_type The asset type
 * @param action Optional action filter
 * @return List of relative image paths
 */
std::vector<std::string> ImageCache::getImagePaths(const std::string& asset_id,
                                                    const std::string& asset_type,
                                                    const std::optional<std::string>& action) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_fresh();
    std::vector<std::string> result;
    
    if (!loaded_) {
        return result;
    }
    
    if (!images_.contains(asset_type) || !images_[asset_type].contains(asset_id)) {
        return result;
    }
    
    const auto& asset = images_[asset_type][asset_id];
    const auto& images_obj = asset["images"];
    
    if (action.has_value()) {
        // Return paths for specific action
        if (images_obj.contains(*action)) {
            const auto& action_data = images_obj[*action];
            if (action_data.contains("paths")) {
                for (const auto& path : action_data["paths"]) {
                    result.push_back(path.get<std::string>());
                }
            }
        }
    } else {
        // Return all paths for all actions
        for (const auto& [action_name, action_data] : images_obj.items()) {
            if (action_data.contains("paths")) {
                for (const auto& path : action_data["paths"]) {
                    result.push_back(path.get<std::string>());
                }
            }
        }
    }
    
    return result;
}

/**
 * Checks whether the ImageCache has been successfully initialized.
 * 
 * @return true if initialized successfully
 */
bool ImageCache::isLoaded() const {
    return loaded_;
}

/**
 * Returns the total count of all images in the cache.
 * 
 * @return Total number of image files
 */
int ImageCache::getTotalImageCount() const {
    return total_image_count_;
}
