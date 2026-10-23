#include "MissionOfferings.hpp"
#include "RetinueDB.hpp"
#include "RetinueStrength.hpp"
#include "GameConfigCache.hpp"
#include <algorithm>

namespace mission_offerings {

mission_gen::retinue_army_summary load_retinue_army(sqlite::database& db,
                                                    int64_t character_id,
                                                    GameConfigCache& cache) {
    const std::vector<retinue_db::retinue_member> members =
        retinue_db::load_active_retinue(db, character_id);
    const retinue_strength::strength_tuning tuning =
        retinue_strength::load_strength_tuning(cache.getCombatOfferings());

    mission_gen::retinue_army_summary summary;
    for (const retinue_db::retinue_member& member : members) {
        summary.sum_power += retinue_strength::member_power(member, cache, tuning);
    }
    summary.unit_count = static_cast<int>(members.size());
    return summary;
}

double character_retinue_power(sqlite::database& db, int64_t character_id, GameConfigCache& cache) {
    const mission_gen::retinue_army_summary army =
        load_retinue_army(db, character_id, cache);
    const retinue_strength::strength_tuning tuning =
        retinue_strength::load_strength_tuning(cache.getCombatOfferings());
    return retinue_strength::army_strength(army.sum_power, army.unit_count, tuning);
}

bool character_in_barony(sqlite::database& db, int64_t character_id) {
    int count = 0;
    db << "SELECT COUNT(*) FROM barony_members WHERE character_id = ?;"
       << character_id >> [&](int c) { count = c; };
    return count > 0;
}

std::vector<mission_gen::retinue_army_summary> barony_member_armies(
    sqlite::database& db, int64_t character_id, GameConfigCache& cache) {
    std::vector<mission_gen::retinue_army_summary> armies;
    int64_t barony_id = 0;
    db << "SELECT barony_id FROM barony_members WHERE character_id = ?;"
       << character_id >> [&](int64_t b) { barony_id = b; };
    if (barony_id == 0) return armies;

    std::vector<int64_t> member_ids;
    db << "SELECT character_id FROM barony_members WHERE barony_id = ? AND character_id != ?;"
       << barony_id << character_id >> [&](int64_t cid) { member_ids.push_back(cid); };
    for (int64_t cid : member_ids) {
        armies.push_back(load_retinue_army(db, cid, cache));
    }
    return armies;
}

bool offering_is_completed(sqlite::database& db, int64_t character_id,
                           const std::string& offering_id) {
    if (offering_id.empty()) return false;
    int count = 0;
    db << "SELECT COUNT(*) FROM character_mission_completions "
          "WHERE character_id = ? AND offering_id = ?;"
       << character_id << offering_id >> [&](int c) { count = c; };
    return count > 0;
}

bool validate_offering_authentic(sqlite::database& db, const mission_gen::offering_id_parts& parts,
                                 int64_t character_id, int64_t now, GameConfigCache& cache) {
    if (parts.player_id != character_id) return false;
    const nlohmann::json& cfg = cache.getCombatOfferings();
    if (!cfg.is_object()) return false;
    const int validity_hours =
        std::max(1, cfg.value("board", nlohmann::json::object()).value("validity_hours", 8));
    if (now >= (parts.hour_bucket + static_cast<int64_t>(validity_hours)) * 3600) return false;

    const mission_gen::mission_tier* tier = nullptr;
    for (const auto& t : mission_gen::parse_tiers(cfg)) {
        if (t.id == parts.tier) {
            tier = &t;
            break;
        }
    }
    if (!tier) return false;

    const bool in_barony = character_in_barony(db, character_id);
    if (tier->requires_barony && !in_barony) return false;

    const mission_gen::retinue_army_summary self_army =
        load_retinue_army(db, character_id, cache);
    const std::vector<mission_gen::retinue_army_summary> members =
        in_barony ? barony_member_armies(db, character_id, cache)
                  : std::vector<mission_gen::retinue_army_summary>{};
    const mission_gen::party_army army =
        mission_gen::party_army_for_tier(*tier, self_army, members, cache);

    // The id freezes the party it was generated for. Growth stays valid; a
    // shrink below the frozen party voids the offer.
    const uint32_t power_now = mission_gen::quantize_power(army.power, cfg);
    return parts.players <= army.expected_players && parts.troop_count <= army.troop_count &&
           parts.power_bucket <= power_now;
}

std::vector<mission_gen::mission_offering> load_board(sqlite::database& db, int64_t character_id,
                                                      int64_t now, GameConfigCache& cache) {
    const bool in_barony = character_in_barony(db, character_id);
    const mission_gen::retinue_army_summary self_army =
        load_retinue_army(db, character_id, cache);
    const std::vector<mission_gen::retinue_army_summary> members =
        in_barony ? barony_member_armies(db, character_id, cache)
                  : std::vector<mission_gen::retinue_army_summary>{};
    const int64_t hour = mission_gen::current_hour_bucket(now);

    std::vector<mission_gen::mission_offering> board = mission_gen::generate_board(
        character_id, hour, self_army, members, in_barony, cache);

    std::vector<mission_gen::mission_offering> filtered;
    filtered.reserve(board.size());
    for (auto& off : board) {
        if (!offering_is_completed(db, character_id, off.id)) {
            filtered.push_back(std::move(off));
        }
    }
    return filtered;
}

int64_t record_attempt_start(sqlite::database& db, const std::string& offering_id,
                             int64_t host_character_id, const std::string& match_id, int64_t now) {
    db << "INSERT INTO combat_mission_attempts "
          "(offering_id, host_character_id, match_id, started_at, participants_json, "
          "warband_end_state_json) VALUES (?, ?, ?, ?, '[]', '[]');"
       << offering_id << host_character_id << match_id << now;
    int64_t id = 0;
    db << "SELECT last_insert_rowid();" >> id;
    return id;
}

void record_attempt_end(sqlite::database& db, int64_t attempt_id, const std::string& result,
                        const std::string& participants_json,
                        const std::string& warband_end_state_json, int64_t now) {
    if (attempt_id <= 0) return;
    db << "UPDATE combat_mission_attempts SET ended_at = ?, result = ?, participants_json = ?, "
          "warband_end_state_json = ? WHERE id = ?;"
       << now << result << participants_json << warband_end_state_json << attempt_id;
}

void record_completion(sqlite::database& db, int64_t character_id, const std::string& offering_id,
                       int64_t now) {
    if (offering_id.empty()) return;
    db << "INSERT OR IGNORE INTO character_mission_completions "
          "(character_id, offering_id, completed_at) VALUES (?, ?, ?);"
       << character_id << offering_id << now;
}

void increment_counters(sqlite::database& db, int64_t character_id,
                        const std::map<std::string, int>& deaths_by_class, int64_t now) {
    for (const auto& [unit_class, count] : deaths_by_class) {
        if (unit_class.empty() || count <= 0) continue;
        const std::string key = "slain." + unit_class;
        db << "INSERT INTO character_counters (character_id, counter_key, value, updated_at) "
              "VALUES (?, ?, ?, ?) "
              "ON CONFLICT(character_id, counter_key) DO UPDATE SET "
              "value = value + excluded.value, updated_at = excluded.updated_at;"
           << character_id << key << count << now;
    }
}

} // namespace mission_offerings
