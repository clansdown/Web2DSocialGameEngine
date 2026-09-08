#include "RetinueMarket.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <set>

// `recruit_candidate` is declared at global scope in RetinueMarket.hpp, so its
// member functions must be defined at global scope too (not inside the
// retinue_market namespace).
nlohmann::json recruit_candidate::to_json() const {
    nlohmann::json j;
    j["unit_class"] = unit_class;
    j["display_name"] = display_name;
    j["gender"] = gender;
    j["level"] = level;
    j["hour_bucket"] = hour_bucket;
    j["expires_at"] = expires_at;
    j["fee"] = fee;
    return j;
}

namespace retinue_market {

namespace {

using json = nlohmann::json;

uint64_t splitmix64(uint64_t x);

uint64_t bucket_seed(int64_t character_id, int hour_bucket) {
    uint64_t h = static_cast<uint64_t>(character_id);
    h = splitmix64(h ^ (static_cast<uint64_t>(static_cast<uint32_t>(hour_bucket)) * 0x9E3779B97F4A7C15ULL));
    return h;
}

uint64_t splitmix64(uint64_t x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Small deterministic PRNG (splitmix64 stream) so regeneration always agrees.
struct prng {
    uint64_t state;
    explicit prng(uint64_t seed) : state(seed) {}
    uint64_t next() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
};

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> read_name_pool(const std::string& text_dir,
                                        const std::string& unit_class,
                                        const std::string& gender) {
    std::vector<std::string> names;
    std::filesystem::path p(text_dir);
    p /= "en";
    p /= "names_" + unit_class + "_" + gender + ".txt";
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (!t.empty()) names.push_back(std::move(t));
    }
    return names;
}

// Fallback pool if a localized name file is missing (lets the game run before
// translation/locale content exists).
const std::vector<std::string>& fallback_names(bool female) {
    static const std::vector<std::string> kMale = {
        "Aelfric", "Bardulf", "Cedric", "Dunstan", "Eadric",
        "Fenwick", "Godwin", "Hildred", "Kendrick", "Leofric",
    };
    static const std::vector<std::string> kFemale = {
        "Aelfgifu", "Bercia", "Cwenburg", "Edgyth", "Freya",
        "Godiva", "Hilde", "Kenna", "Leofwyn", "Rowena",
    };
    return female ? kFemale : kMale;
}

bool class_unlocked(const json& class_config, int manor_level) {
    if (!class_config.is_object()) return false;
    int req = class_config.value("requires_manor_level", 1);
    return manor_level >= req;
}

} // namespace

int max_candidate_level(const json& retinue_config, int manor_level, int class_max_level) {
    const json market = retinue_config.value("market", json::object());
    int offset = market.value("max_candidate_level_offset", 1);
    int cap = std::max(0, manor_level) + offset;
    if (class_max_level > 0) cap = std::min(cap, class_max_level);
    return std::max(1, cap);
}

json compute_recruit_fee_for(const json& class_config, int level) {
    json fee = json::object();
    if (!class_config.is_object() || !class_config.contains("costs") ||
        !class_config["costs"].is_array() || class_config["costs"].empty()) {
        fee["gold"] = 0;
        return fee;
    }
    const auto& costs = class_config["costs"];
    int idx = std::max(0, level - 1);
    if (idx < static_cast<int>(costs.size())) {
        const auto& entry = costs[idx];
        if (entry.is_object()) return entry;
        if (entry.is_number()) {
            fee["gold"] = entry.get<double>();
            return fee;
        }
        fee["gold"] = 0;
        return fee;
    }
    if (static_cast<int>(costs.size()) == 1) {
        // A single cost entry applies at every level (no second point to
        // extrapolate from — avoid reading costs[-1]).
        const auto& entry = costs[0];
        if (entry.is_object()) return entry;
        if (entry.is_number()) {
            fee["gold"] = entry.get<double>();
            return fee;
        }
        fee["gold"] = 0;
        return fee;
    }

    // Linear extension of the last two entries per numeric field.
    int last_idx = static_cast<int>(costs.size()) - 1;
    const auto& last = costs[last_idx];
    const auto& prev = costs[last_idx - 1];
    if (last.is_object() && prev.is_object()) {
        std::set<std::string> keys;
        for (auto& [k, v] : last.items()) {
            (void)v;
            keys.insert(k);
        }
        for (auto& [k, v] : prev.items()) {
            (void)v;
            keys.insert(k);
        }
        for (const auto& k : keys) {
            double a = prev.value(k, 0.0);
            double b = last.value(k, 0.0);
            fee[k] = b + static_cast<double>(idx - last_idx) * (b - a);
        }
        return fee;
    }
    if (last.is_number() && prev.is_number()) {
        double a = prev.get<double>();
        double b = last.get<double>();
        fee["gold"] = b + static_cast<double>(idx - last_idx) * (b - a);
        return fee;
    }
    fee["gold"] = 0;
    return fee;
}

std::vector<recruit_candidate> candidates_for_bucket(
    const json& player_combatants, const json& retinue_config,
    int64_t character_id, int hour_bucket, int manor_level,
    const std::string& text_dir)
{
    std::vector<recruit_candidate> out;
    if (!player_combatants.is_object()) return out;

    const json market = retinue_config.value("market", json::object());
    int candidates_per_class = market.value("candidates_per_class", 6);
    int grace_buckets = market.value("grace_buckets", 1);

    prng rng(bucket_seed(character_id, hour_bucket));

    for (auto& [class_id, class_config] : player_combatants.items()) {
        if (!class_unlocked(class_config, manor_level)) continue;
        int class_max = class_config.value("max_level", 1);
        int max_lvl = max_candidate_level(retinue_config, manor_level, class_max);

        auto male_pool = read_name_pool(text_dir, class_id, "male");
        auto female_pool = read_name_pool(text_dir, class_id, "female");
        if (male_pool.empty()) male_pool = fallback_names(false);
        if (female_pool.empty()) female_pool = fallback_names(true);

        for (int i = 0; i < candidates_per_class; ++i) {
            recruit_candidate c;
            c.unit_class = class_id;
            c.gender = (rng.next() & 1) ? "female" : "male";
            c.level = 1 + static_cast<int>(rng.next() % static_cast<uint64_t>(max_lvl));
            const auto& pool = (c.gender == "female") ? female_pool : male_pool;
            if (pool.empty()) continue;
            uint64_t base = rng.next() % pool.size();
            c.display_name = pool[(base + static_cast<uint64_t>(i)) % pool.size()];
            c.hour_bucket = hour_bucket;
            c.expires_at = static_cast<int64_t>(hour_bucket + grace_buckets + 1) * 3600LL;
            c.fee = compute_recruit_fee_for(class_config, c.level);
            out.push_back(std::move(c));
        }
    }
    return out;
}

std::vector<recruit_candidate> list_candidates(
    const json& player_combatants, const json& retinue_config,
    int64_t character_id, int64_t now, int manor_level,
    const std::string& text_dir, int& current_bucket, int64_t& next_refresh_at)
{
    current_bucket = static_cast<int>(now / 3600);
    next_refresh_at = static_cast<int64_t>(current_bucket + 1) * 3600LL;

    auto prev = candidates_for_bucket(player_combatants, retinue_config,
                                      character_id, current_bucket - 1,
                                      manor_level, text_dir);
    auto current = candidates_for_bucket(player_combatants, retinue_config,
                                         character_id, current_bucket,
                                         manor_level, text_dir);

    std::vector<recruit_candidate> out;
    out.reserve(prev.size() + current.size());
    for (auto& c : prev) out.push_back(std::move(c));
    for (auto& c : current) out.push_back(std::move(c));
    return out;
}

bool is_valid_candidate(const json& player_combatants, const json& retinue_config,
                        int64_t character_id, int manor_level,
                        const std::string& text_dir, const recruit_candidate& candidate)
{
    const int64_t now = std::time(nullptr);
    const int current = static_cast<int>(now / 3600);

    // Grace window: an offer is hirable in its own hour and the next one.
    if (candidate.hour_bucket != current && candidate.hour_bucket != current - 1) {
        return false;
    }
    if (candidate.gender != "male" && candidate.gender != "female") return false;
    if (candidate.level < 1) return false;
    if (!player_combatants.is_object() ||
        !player_combatants.contains(candidate.unit_class)) {
        return false;
    }
    const auto& class_config = player_combatants[candidate.unit_class];
    if (!class_unlocked(class_config, manor_level)) return false;

    int max_lvl = max_candidate_level(retinue_config, manor_level,
                                      class_config.value("max_level", 1));
    if (candidate.level > max_lvl) return false;

    auto pool = candidates_for_bucket(player_combatants, retinue_config,
                                      character_id, candidate.hour_bucket,
                                      manor_level, text_dir);
    for (const auto& c : pool) {
        if (c.unit_class == candidate.unit_class &&
            c.display_name == candidate.display_name &&
            c.gender == candidate.gender &&
            c.level == candidate.level) {
            return true;
        }
    }
    return false;
}

} // namespace retinue_market