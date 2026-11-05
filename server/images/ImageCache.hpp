#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <map>
#include <mutex>
#include <ctime>

/**
 * ImageCache - Manages the game asset image directory structure and provides
 * fast queries for image paths grouped by asset type, asset ID, and action.
 * 
 * This class scans the images/ directory at startup and builds an in-memory
 * cache of all available images. The cache enables quick lookups of image
 * paths for game assets without requiring filesystem operations on each request.
 * 
 * Image Directory Structure Expected:
 *   images/
 *   ├── buildings/{building_id}/
 *   │   ├── construction/{frame}.png
 *   │   └── idle/{frame}.png
 *   ├── combatants/{combatant_id}/
 *   │   ├── idle/{frame}.png
 *   │   ├── attack/{frame}.png
 *   │   ├── defend/{frame}.png
 *   │   └── die/{frame}.png
 *   ├── heroes/{hero_id}/
 *   │   ├── idle/{frame}.png
 *   │   ├── attack/{frame}.png
 *   │   └── skills/{skill_id}/
 *   └── portraits/{portrait_id}/
 *       └── {frame}.png
 * 
 * Thread Safety:
 *   This class uses a singleton pattern with internal mutable state.
 *   The initialize() method must be called before any getters.
 *   Getters are internally locked and lazily rescan when the images tree
 *   changed since the last scan (POSIX stat + time_t), so new art appears
 *   without a restart.
 * 
 * Usage:
 *   ImageCache& cache = ImageCache::getInstance();
 *   cache.initialize("./images");
 *   json images = cache.getImages();
 */
class ImageCache {
public:
    /**
     * Gets the singleton instance of ImageCache.
     * 
     * @return ImageCache& Reference to the singleton ImageCache instance
     * 
     * Note: The returned instance must be initialized with initialize()
     *       before calling any getter methods.
     */
    static ImageCache& getInstance();

    /**
     * Initializes the ImageCache by scanning the specified images directory.
     * 
     * This method traverses the images directory structure and builds an
     * in-memory cache of all available images grouped by asset type,
     * asset ID, and action. This scan happens once at server startup.
     * 
     * Supported asset types (determined by top-level directory names):
     *   - buildings: Building asset images
     *   - combatants: Combatant unit images  
     *   - heroes: Hero character images
     *   - portraits: Fiefdom official portrait images
     * 
     * For each asset type directory, the method scans subdirectories to
     * identify actions (e.g., idle, attack, defend) and image files.
     * 
     * @param images_dir Absolute or relative path to the images directory
     *                  (e.g., "./images" or "/var/ravenest/images")
     * 
     * @return bool true if initialization succeeded, false on error
     *              Initialization fails if the directory doesn't exist
     *              or cannot be read
     * 
     * Example:
     *   if (!cache.initialize("./images")) {
     *       std::cerr << "Failed to initialize image cache" << std::endl;
     *   }
     */
    bool initialize(const std::string& images_dir);

    /**
     * Returns all cached image data as a JSON object.
     * 
     * The returned JSON structure contains all images grouped by asset type,
     * with counts and paths for each asset and action.
     * 
     * @return nlohmann::json Complete image cache structure:
     *   {
     *     "total_images": <int>,
     *     "buildings": {
     *       "<building_id>": {
     *         "total_images": <int>,
     *         "images": {
     *           "<action>": {
     *             "count": <int>,
     *             "paths": ["path/to/image1.png", ...]
     *           },
     *           ...
     *         }
     *       },
     *       ...
     *     },
     *     "combatants": {...},
     *     "heroes": {...},
     *     "portraits": {...}
     *   }
     * 
     * Note: Returns an empty object if initialize() has not been called
     *       or if it failed.
     */
    nlohmann::json getImages();

    /**
     * Returns image data for a specific asset type.
     * 
     * @param type The asset type to retrieve (e.g., "buildings", "combatants",
     *             "heroes", "portraits")
     * 
     * @return nlohmann::json Image data for the specified type, or null if
     *         the type doesn't exist in the cache
     * 
     * Example:
     *   json buildings = cache.getImagesByType("buildings");
     *   // Returns: {"home_base": {"total_images": 2, "images": {...}}}, ...)
     */
    nlohmann::json getImagesByType(const std::string& type);

    /**
     * Returns image data for specific asset IDs across all asset types.
     * 
     * This method searches through all asset types for the specified IDs
     * and returns their image data. This is useful when filtering by
     * specific asset IDs (e.g., ["farm", "barracks"]).
     * 
     * @param ids Vector of asset IDs to filter by (e.g., {"farm", "goblin"})
     * 
     * @return nlohmann::json Object containing only the specified asset IDs
     *         and their image data:
     *   {
     *     "farm": {"total_images": 4, "images": {...}},
     *     "barracks": {"total_images": 2, "images": {...}}
     *   }
     * 
     * Note: Returns an empty object if no matching IDs are found.
     * 
     * Example:
     *   json filtered = cache.getImagesByIds({"farm", "spearman", "knight_hero"});
     */
    nlohmann::json getImagesByIds(const std::vector<std::string>& ids);

    /**
     * Gets image file paths for a specific asset and optional action.
     * 
     * This low-level method returns the actual file paths for a single
     * asset, optionally filtered by action. Used for targeted lookups.
     * 
     * @param asset_id The asset identifier (e.g., "home_base", "goblin")
     * @param asset_type The asset type (e.g., "buildings", "combatants")
     * @param action Optional action filter (e.g., "idle", "attack").
     *               If std::nullopt, returns paths for all actions.
     * 
     * @return std::vector<std::string> List of relative image paths:
     *   {"images/buildings/home_base/idle/1.png", ...}
     * 
     * Example:
     *   auto paths = cache.getImagePaths("home_base", "buildings", "idle");
     *   // Returns: {"images/buildings/home_base/idle/1.png"}
     */
    std::vector<std::string> getImagePaths(const std::string& asset_id,
                                           const std::string& asset_type,
                                           const std::optional<std::string>& action = std::nullopt);

    /**
     * Checks whether the ImageCache has been successfully initialized.
     * 
     * @return bool true if initialize() was called and succeeded,
     *              false otherwise
     * 
     * Note: Getter methods may return empty/invalid data if this
     *       returns false.
     */
    bool isLoaded() const;

    /**
     * Returns the total count of all images in the cache.
     * 
     * @return int Total number of image files discovered during initialization
     * 
     * Example:
     *   int total = cache.getTotalImageCount();
     *   std::cout << "Total cached images: " << total << std::endl;
     */
    int getTotalImageCount() const;

private:
    /**
     * Private constructor for singleton pattern.
     * 
     * Use getInstance() to obtain the singleton instance.
     */
    ImageCache() = default;

    /**
     * Deleted copy constructor to enforce singleton.
     */
    ImageCache(const ImageCache&) = delete;

    /**
     * Deleted assignment operator to enforce singleton.
     */
    ImageCache& operator=(const ImageCache&) = delete;

    /**
     * Recursively scans a directory and adds found images to the cache.
     * 
     * This method is called during initialization to traverse the images
     * directory structure. It extracts the asset type from the top-level
     * directory and builds the nested structure.
     * 
     * Directory naming conventions expected:
     *   - Top-level (images/): Must match known asset types
     *   - Second-level: Asset ID (e.g., "home_base", "goblin")
     *   - Third-level: Action name (e.g., "idle", "attack")
     *   - Files: Image frames (e.g., "1.png", "2.png")
     * 
     * @param path Filesystem path to scan
     * @param asset_type The asset type derived from parent directory name
     * 
     * Note: This is a recursive method that traverses all subdirectories.
     *       Image files are identified by .png, .jpg, .jpeg extensions.
     */
    void scanDirectory(const std::string& path, const std::string& asset_type);

    /**
     * Stats every watched directory; if any mtime changed (an image was added,
     * removed, or renamed anywhere in the tree), performs a full rescan. Uses
     * POSIX stat() + time_t (std::filesystem::file_time_type is broken on
     * Linux). Directory mtime bumps on add/remove/rename, not on in-place
     * content edits.
     */
    void ensure_fresh();

    /**
     * Records the mtime of every directory in the tree (the base images dir,
     * each asset type, asset id, and action dir) so ensure_fresh() can detect
     * additions at any depth.
     */
    void record_watched_dirs();

    /**
     * Cached image data structure indexed by asset type -> asset ID -> action.
     * 
     * Structure:
     *   {
     *     "buildings": {
     *       "home_base": {
     *         "total_images": 2,
     *         "images": {
     *           "idle": {"count": 1, "paths": ["images/..."]},
     *           "construction": {"count": 1, "paths": ["images/..."]}
     *         }
     *       }
     *     },
     *     "combatants": {...},
     *     ...
     *   }
     */
    nlohmann::json images_;

    /** Flag indicating whether initialization completed successfully. */
    bool loaded_ = false;

    /** Total count of all image files discovered during scanning. */
    int total_image_count_ = 0;

    /** Base images directory path for constructing relative paths. */
    std::string base_path_;

    /** Directory mtimes at the last scan (path -> mtime), for lazy rescans. */
    std::map<std::string, time_t> watched_dirs_;

    /** Guards lazy rescans from the (non-const) getters. */
    std::mutex mutex_;
};
