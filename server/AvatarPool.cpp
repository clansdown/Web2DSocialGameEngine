#include "AvatarPool.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>

namespace {

namespace fs = std::filesystem;

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string lower_extension(const fs::path& path) {
    return to_lower(path.extension().string());
}

// Filename stem parsed as an integer, or -1 when it is not purely numeric.
long numeric_stem(const fs::path& path) {
    const std::string stem = path.stem().string();
    if (stem.empty()) return -1;
    long value = 0;
    for (char c : stem) {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
        if (value > 100000000L) return -1;
    }
    return value;
}

// Stable, deterministic order: numeric stems ascending, then non-numeric
// filenames lexicographically. Two files with the same numeric stem (e.g.
// "1.png" and "1.jpg") fall back to filename order so the scan is stable.
bool pool_entry_less(const fs::path& a, const fs::path& b) {
    const long an = numeric_stem(a);
    const long bn = numeric_stem(b);
    if (an >= 0 && bn >= 0 && an != bn) return an < bn;
    if (an >= 0 && bn < 0) return true;
    if (an < 0 && bn >= 0) return false;
    return a.filename().string() < b.filename().string();
}

// Scans one flat directory of images and returns the sorted relative ids
// (id_prefix is prepended to each filename, e.g. "male/").
std::vector<std::string> scan_pool_dir(const fs::path& dir, const std::string& id_prefix) {
    std::vector<std::string> ids;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return ids;

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (!AvatarPool::allowed_extension(lower_extension(entry.path()))) continue;
        files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end(), pool_entry_less);
    ids.reserve(files.size());
    for (const auto& file : files) {
        ids.push_back(id_prefix + file.filename().string());
    }
    return ids;
}

// POSIX stat() of a directory's mtime (std::filesystem::file_time_type
// comparisons are broken on Linux). Returns 0 when the path is not a directory.
time_t directory_mtime(const fs::path& dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) return 0;
    if (!(st.st_mode & S_IFDIR)) return 0;
    return st.st_mtime;
}

} // namespace

AvatarPool& AvatarPool::getInstance() {
    static AvatarPool instance;
    return instance;
}

bool AvatarPool::initialize(const std::string& images_dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    images_dir_ = images_dir;
    rescan_all();

    std::cout << "AvatarPool: " << male_headshots_.size() << " male headshots, "
              << female_headshots_.size() << " female headshots, "
              << safe_avatars_male_.size() << " male safe avatars, "
              << safe_avatars_female_.size() << " female safe avatars" << std::endl;
    return loaded_;
}

void AvatarPool::rescan_all() {
    const fs::path base(images_dir_);
    std::error_code ec;
    if (!fs::is_directory(base, ec)) {
        std::cerr << "AvatarPool: images directory does not exist: " << images_dir_ << std::endl;
        loaded_ = false;
        return;
    }

    const fs::path hs_male = base / "retinue" / "headshots" / "male";
    const fs::path hs_female = base / "retinue" / "headshots" / "female";
    const fs::path sa_male = base / "avatars" / "safe" / "male";
    const fs::path sa_female = base / "avatars" / "safe" / "female";

    male_headshots_ = scan_pool_dir(hs_male, "male/");
    female_headshots_ = scan_pool_dir(hs_female, "female/");
    safe_avatars_male_ = scan_pool_dir(sa_male, "male/");
    safe_avatars_female_ = scan_pool_dir(sa_female, "female/");

    male_headshots_mtime_ = directory_mtime(hs_male);
    female_headshots_mtime_ = directory_mtime(hs_female);
    safe_avatars_male_mtime_ = directory_mtime(sa_male);
    safe_avatars_female_mtime_ = directory_mtime(sa_female);

    loaded_ = true;
}

void AvatarPool::ensure_fresh() {
    if (images_dir_.empty()) return;
    const fs::path base(images_dir_);
    const fs::path hs_male = base / "retinue" / "headshots" / "male";
    const fs::path hs_female = base / "retinue" / "headshots" / "female";
    const fs::path sa_male = base / "avatars" / "safe" / "male";
    const fs::path sa_female = base / "avatars" / "safe" / "female";

    time_t mtime = directory_mtime(hs_male);
    if (mtime != male_headshots_mtime_) {
        male_headshots_ = scan_pool_dir(hs_male, "male/");
        male_headshots_mtime_ = mtime;
    }
    mtime = directory_mtime(hs_female);
    if (mtime != female_headshots_mtime_) {
        female_headshots_ = scan_pool_dir(hs_female, "female/");
        female_headshots_mtime_ = mtime;
    }
    mtime = directory_mtime(sa_male);
    if (mtime != safe_avatars_male_mtime_) {
        safe_avatars_male_ = scan_pool_dir(sa_male, "male/");
        safe_avatars_male_mtime_ = mtime;
    }
    mtime = directory_mtime(sa_female);
    if (mtime != safe_avatars_female_mtime_) {
        safe_avatars_female_ = scan_pool_dir(sa_female, "female/");
        safe_avatars_female_mtime_ = mtime;
    }
}

bool AvatarPool::is_loaded() const {
    return loaded_;
}

const std::vector<std::string>& AvatarPool::headshots_ref(const std::string& gender) const {
    if (gender == "female") return female_headshots_;
    return male_headshots_;
}

const std::vector<std::string>& AvatarPool::safe_avatars_ref(const std::string& gender) const {
    if (gender == "female") return safe_avatars_female_;
    return safe_avatars_male_;
}

std::vector<std::string> AvatarPool::headshots(const std::string& gender) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_fresh();
    return headshots_ref(gender);
}

std::vector<std::string> AvatarPool::safe_avatars(const std::string& gender) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_fresh();
    return safe_avatars_ref(gender);
}

bool AvatarPool::has_headshot(const std::string& gender, const std::string& id) {
    if (id.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_fresh();
    const std::vector<std::string>& pool = headshots_ref(gender);
    return std::find(pool.begin(), pool.end(), id) != pool.end();
}

bool AvatarPool::has_safe_avatar(const std::string& gender, const std::string& id) {
    if (id.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_fresh();
    const std::vector<std::string>& pool = safe_avatars_ref(gender);
    return std::find(pool.begin(), pool.end(), id) != pool.end();
}

std::string AvatarPool::headshot_url(const std::string& id) {
    return "/images/retinue/headshots/" + id;
}

std::string AvatarPool::safe_avatar_url(const std::string& id) {
    return "/images/avatars/safe/" + id;
}

bool AvatarPool::allowed_extension(const std::string& extension) {
    const std::string ext = to_lower(extension);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".webp" || ext == ".avif";
}
