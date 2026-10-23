#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <ctime>

// Scans the on-disk portrait pools and answers membership/URL questions for the
// retinue headshot system and the player PFP (safe avatars).
//
// There is deliberately NO catalog config: adding or removing an image file is
// the whole authoring workflow. Files are sorted by their numeric stem (then
// lexicographically) so the deterministic recruit market always sees the same
// pool order. See docs/retinue_design.md and the plan's "Asset layout".
//
// The pools are re-scanned lazily: each accessor stats the pool directories
// (POSIX stat + time_t) and rescans any whose modification time changed, so new
// images appear without a server restart. (Directory mtime bumps on
// add/remove/rename, not on in-place content edits.)
//
// Layout:
//   images/retinue/headshots/male/<id>.<ext>
//   images/retinue/headshots/female/<id>.<ext>
//   images/avatars/safe/male/<id>.<ext>
//   images/avatars/safe/female/<id>.<ext>
//
// A stored id is the gender-qualified path relative to the pool root
// (e.g. "male/03.png") so it is self-describing and the served URL is a simple
// concatenation. Both the retinue headshots and the player safe-avatar pool are
// gender-split: a character only ever picks from their own sex's pool.
class AvatarPool {
public:
    static AvatarPool& getInstance();

    // Scans `images_dir` (relative to the server's working directory, i.e.
    // "images"). Returns false only when the base directory is missing;
    // individual empty pools are allowed (art may not exist yet).
    bool initialize(const std::string& images_dir);

    // True once at least one successful scan has run.
    bool is_loaded() const;

    // Headshot ids for a gender ("male"/"female"); empty vector for anything
    // else or when the pool directory is missing/empty. Rescans if the pool
    // directory changed since the last scan.
    std::vector<std::string> headshots(const std::string& gender);

    // Safe-avatar ids for a gender ("male"/"female"), gender-qualified paths
    // (e.g. "male/3.png"); empty vector for anything else or when the pool
    // directory is missing/empty. Rescans if the pool directory changed.
    std::vector<std::string> safe_avatars(const std::string& gender);

    // Membership checks. An id must be gender-qualified and match a scanned
    // file in that gender's pool exactly (after a freshness rescan).
    bool has_headshot(const std::string& gender, const std::string& id);
    bool has_safe_avatar(const std::string& gender, const std::string& id);

    // URL helpers. Both take ids exactly as scanned/stored.
    static std::string headshot_url(const std::string& id);
    static std::string safe_avatar_url(const std::string& id);

    // True if `extension` (with the leading dot, any case) is an accepted image
    // format. png/jpg/jpeg are the classics; webp/avif are the modern set.
    static bool allowed_extension(const std::string& extension);

private:
    AvatarPool() = default;

    // Stats the four pool directories and rescans any whose mtime advanced.
    void ensure_fresh();
    // Unconditional full scan of the four pools (used by initialize()).
    void rescan_all();

    const std::vector<std::string>& headshots_ref(const std::string& gender) const;
    const std::vector<std::string>& safe_avatars_ref(const std::string& gender) const;

    std::string images_dir_;
    std::vector<std::string> male_headshots_;
    std::vector<std::string> female_headshots_;
    std::vector<std::string> safe_avatars_male_;
    std::vector<std::string> safe_avatars_female_;

    // Directory mtimes at the last scan (POSIX time_t; 0 = never/absent).
    time_t male_headshots_mtime_ = 0;
    time_t female_headshots_mtime_ = 0;
    time_t safe_avatars_male_mtime_ = 0;
    time_t safe_avatars_female_mtime_ = 0;

    bool loaded_ = false;
    std::mutex mutex_;
};
