#include "Progression.hpp"
#include "Letters.hpp"
#include "PlayerStateDB.hpp"
#include <algorithm>

namespace progression {

mesne_lord_requirements parse_mesne_lord_requirements(const nlohmann::json& progression_config) {
    mesne_lord_requirements reqs;
    if (!progression_config.is_object()) return reqs;
    if (!progression_config.contains("mesne_lord")) return reqs;
    const auto& block = progression_config["mesne_lord"];
    if (!block.is_object() || !block.contains("requirements")) return reqs;
    const auto& requirements = block["requirements"];
    if (!requirements.is_object()) return reqs;
    if (requirements.contains("manor_level") && requirements["manor_level"].is_number_integer()) {
        reqs.manor_level = requirements["manor_level"].get<int>();
    }
    if (requirements.contains("milestones") && requirements["milestones"].is_array()) {
        for (const auto& m : requirements["milestones"]) {
            if (m.is_string() && !m.get<std::string>().empty()) {
                reqs.milestones.push_back(m.get<std::string>());
            }
        }
    }
    return reqs;
}

int join_cooldown_seconds(const nlohmann::json& progression_config) {
    int days = 3;
    if (progression_config.is_object() && progression_config.contains("barony") &&
        progression_config["barony"].is_object()) {
        const auto& barony = progression_config["barony"];
        if (barony.contains("join_cooldown_days") && barony["join_cooldown_days"].is_number_integer()) {
            days = barony["join_cooldown_days"].get<int>();
        }
    }
    if (days < 0) days = 0;
    return days * 24 * 60 * 60;
}

int get_manor_level(sqlite::database& db, int character_id) {
    int level = 0;
    db << "SELECT COALESCE(MAX(manor_level), 0) FROM fiefdoms WHERE owner_id = ?;"
       << character_id
       >> [&](int l) { level = l; };
    return level;
}

bool has_fiefdom(sqlite::database& db, int character_id) {
    int count = 0;
    db << "SELECT COUNT(*) FROM fiefdoms WHERE owner_id = ?;"
       << character_id
       >> [&](int c) { count = c; };
    return count > 0;
}

bool has_milestone(sqlite::database& db, int character_id, const std::string& milestone_id) {
    if (milestone_id.empty()) return false;
    int count = 0;
    db << "SELECT COUNT(*) FROM character_milestones WHERE character_id = ? AND milestone_id = ?;"
       << character_id << milestone_id
       >> [&](int c) { count = c; };
    return count > 0;
}

void grant_milestone(sqlite::database& db, int character_id,
                     const std::string& milestone_id, int64_t now) {
    if (milestone_id.empty()) return;
    db << "INSERT OR IGNORE INTO character_milestones (character_id, milestone_id, achieved_at) "
          "VALUES (?, ?, ?);"
       << character_id << milestone_id << now;
}

std::vector<milestone_condition> parse_milestone_conditions(const nlohmann::json& progression_config) {
    std::vector<milestone_condition> conditions;
    if (!progression_config.is_object() || !progression_config.contains("milestones") ||
        !progression_config["milestones"].is_array()) {
        return conditions;
    }
    for (const auto& entry : progression_config["milestones"]) {
        if (!entry.is_object()) continue;
        milestone_condition c;
        c.id = entry.value("id", "");
        c.counter = entry.value("counter", "");
        c.min = entry.value("min", 1);
        if (!c.id.empty() && !c.counter.empty() && c.min > 0) {
            conditions.push_back(std::move(c));
        }
    }
    return conditions;
}

int get_counter(sqlite::database& db, int character_id, const std::string& counter_key) {
    if (counter_key.empty()) return 0;
    int value = 0;
    db << "SELECT value FROM character_counters WHERE character_id = ? AND counter_key = ?;"
       << character_id << counter_key >> [&](int v) { value = v; };
    return value;
}

void evaluate_milestone_conditions(sqlite::database& db, int character_id,
                                   const std::vector<milestone_condition>& conditions, int64_t now) {
    for (const auto& c : conditions) {
        if (get_counter(db, character_id, c.counter) >= c.min) {
            grant_milestone(db, character_id, c.id, now);
        }
    }
}

bool meets_mesne_lord_requirements(sqlite::database& db, int character_id,
                                   const mesne_lord_requirements& reqs) {
    if (get_manor_level(db, character_id) < reqs.manor_level) return false;
    for (const auto& milestone : reqs.milestones) {
        if (!has_milestone(db, character_id, milestone)) return false;
    }
    return true;
}

bool check_freehold_qualification(sqlite::database& db, int character_id, int64_t now,
                                  const mesne_lord_requirements& reqs) {
    std::string phase;
    db << "SELECT game_phase FROM player_game_state WHERE character_id = ?;"
       << character_id
       >> [&](std::string p) { phase = p; };
    if (phase != "freehold") return false;
    if (!meets_mesne_lord_requirements(db, character_id, reqs)) return false;

    player_state_db::enter_freehold_qualified(db, character_id, now);
    letters::send_letter(db, character_id, k_mesne_lord_letter_key, now);
    return true;
}

} // namespace progression
