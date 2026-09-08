#include "RetinueDB.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>

namespace retinue_db {

nlohmann::json retinue_member::to_json() const {
    nlohmann::json j;
    j["id"] = id;
    j["character_id"] = character_id;
    j["display_name"] = display_name;
    j["unit_class"] = unit_class;
    j["is_knight"] = is_knight;
    j["level"] = level;
    j["gender"] = gender;
    j["health"] = health;
    j["health_updated"] = health_updated;
    j["priority"] = priority;
    j["weapons"] = weapons;
    j["armor"] = armor;
    j["equipment"] = equipment;
    j["abilities"] = abilities;
    j["status"] = status;
    j["maintained"] = maintained;
    j["created_at"] = created_at;
    return j;
}

namespace {

constexpr const char* kRetinueColumns =
    "id, character_id, display_name, unit_class, is_knight, level, gender, health, "
    "health_updated, priority, weapons, armor, equipment, abilities, status, maintained, created_at";

retinue_member row_to_member(int64_t id, int64_t character_id, std::string display_name,
                             std::string unit_class, int is_knight, int level,
                             std::string gender, double health, int64_t health_updated,
                             int priority, std::string weapons, std::string armor,
                             std::string equipment, std::string abilities,
                             std::string status, int maintained, int64_t created_at)
{
    retinue_member m;
    m.id = id;
    m.character_id = character_id;
    m.display_name = std::move(display_name);
    m.unit_class = std::move(unit_class);
    m.is_knight = is_knight != 0;
    m.level = level;
    m.gender = std::move(gender);
    m.health = health;
    m.health_updated = health_updated;
    m.priority = priority;
    m.maintained = maintained != 0;
    try {
        m.weapons = nlohmann::json::parse(weapons);
        m.armor = nlohmann::json::parse(armor);
        m.equipment = nlohmann::json::parse(equipment);
        m.abilities = nlohmann::json::parse(abilities);
    } catch (const std::exception&) {
        m.weapons = nlohmann::json::object();
        m.armor = nlohmann::json::object();
        m.equipment = nlohmann::json::object();
        m.abilities = nlohmann::json::array();
    }
    m.status = std::move(status);
    m.created_at = created_at;
    return m;
}

} // namespace

std::vector<retinue_member> load_retinue(sqlite::database& db, int64_t character_id) {
    std::vector<retinue_member> members;
    db << "SELECT " + std::string(kRetinueColumns) +
              " FROM retinue_members WHERE character_id = ? ORDER BY is_knight DESC, id ASC;"
       << character_id
       >> [&](int64_t id, int64_t cid, std::string display_name, std::string unit_class,
              int is_knight, int level, std::string gender, double health,
              int64_t health_updated, int priority, std::string weapons, std::string armor,
              std::string equipment, std::string abilities, std::string status,
              int maintained, int64_t created_at) {
             members.push_back(row_to_member(id, cid, std::move(display_name),
                                             std::move(unit_class), is_knight, level,
                                             std::move(gender), health, health_updated,
                                             priority, std::move(weapons), std::move(armor),
                                             std::move(equipment), std::move(abilities),
                                             std::move(status), maintained, created_at));
         };
    return members;
}

retinue_member get_or_create_knight(sqlite::database& db, int64_t character_id) {
    std::vector<retinue_member> knights;
    db << "SELECT " + std::string(kRetinueColumns) +
              " FROM retinue_members WHERE character_id = ? AND is_knight = 1;"
       << character_id
       >> [&](int64_t id, int64_t cid, std::string display_name, std::string unit_class,
              int is_knight, int level, std::string gender, double health,
              int64_t health_updated, int priority, std::string weapons, std::string armor,
              std::string equipment, std::string abilities, std::string status,
              int maintained, int64_t created_at) {
             knights.push_back(row_to_member(id, cid, std::move(display_name),
                                             std::move(unit_class), is_knight, level,
                                             std::move(gender), health, health_updated,
                                             priority, std::move(weapons), std::move(armor),
                                             std::move(equipment), std::move(abilities),
                                             std::move(status), maintained, created_at));
         };
    if (!knights.empty()) return knights.front();

    // No knight yet — create one from the character's own identity.
    std::string display_name;
    int level = 1;
    std::string sex;
    db << "SELECT display_name, level, sex FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::string dn, int l, std::string s) {
              display_name = dn;
              level = l;
              sex = s;
          };
    if (display_name.empty()) {
        display_name = "Knight";
    }
    const std::string gender = (sex == "female") ? "female" : "male";

    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    db << "INSERT INTO retinue_members (character_id, display_name, unit_class, is_knight, level, "
          "gender, health, health_updated, priority, weapons, armor, equipment, abilities, status, maintained, created_at) "
          "VALUES (?, ?, 'knight', 1, ?, ?, 100, ?, 0, '{}', '{}', '{}', '[]', 'active', 1, ?);"
       << character_id << display_name << level << gender << now << now;

    retinue_member knight;
    knight.id = db.last_insert_rowid();
    knight.character_id = character_id;
    knight.display_name = display_name;
    knight.unit_class = "knight";
    knight.is_knight = true;
    knight.level = level;
    knight.gender = gender;
    knight.health = 100.0;
    knight.health_updated = now;
    knight.priority = 0;
    knight.weapons = nlohmann::json::object();
    knight.armor = nlohmann::json::object();
    knight.equipment = nlohmann::json::object();
    knight.abilities = nlohmann::json::array();
    knight.status = "active";
    knight.created_at = now;
    return knight;
}

std::vector<retinue_member> load_active_retinue(sqlite::database& db, int64_t character_id) {
    get_or_create_knight(db, character_id);
    std::vector<retinue_member> members;
    db << "SELECT " + std::string(kRetinueColumns) +
              " FROM retinue_members WHERE character_id = ? AND status = 'active' ORDER BY is_knight DESC, id ASC;"
       << character_id
       >> [&](int64_t id, int64_t cid, std::string display_name, std::string unit_class,
              int is_knight, int level, std::string gender, double health,
              int64_t health_updated, int priority, std::string weapons, std::string armor,
              std::string equipment, std::string abilities, std::string status,
              int maintained, int64_t created_at) {
             members.push_back(row_to_member(id, cid, std::move(display_name),
                                             std::move(unit_class), is_knight, level,
                                             std::move(gender), health, health_updated,
                                             priority, std::move(weapons), std::move(armor),
                                             std::move(equipment), std::move(abilities),
                                             std::move(status), maintained, created_at));
         };
    return members;
}

retinue_member hire_member(sqlite::database& db, int64_t character_id,
                           const std::string& display_name, const std::string& unit_class,
                           const std::string& gender, int level, int64_t now)
{
    // New members rank below everyone currently in the roster.
    int max_priority = 0;
    db << "SELECT COALESCE(MAX(priority), 0) FROM retinue_members WHERE character_id = ?;"
       << character_id >> [&](int mp) { max_priority = mp; };

    const int64_t created = (now > 0) ? now
        : std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    db << "INSERT INTO retinue_members (character_id, display_name, unit_class, is_knight, level, "
          "gender, health, health_updated, priority, weapons, armor, equipment, abilities, status, maintained, created_at) "
          "VALUES (?, ?, ?, 0, ?, ?, 100, ?, ?, '{}', '{}', '{}', '[]', 'active', 1, ?);"
       << character_id << display_name << unit_class << level << gender << created
       << (max_priority + 1) << created;

    retinue_member m;
    m.id = db.last_insert_rowid();
    m.character_id = character_id;
    m.display_name = display_name;
    m.unit_class = unit_class;
    m.is_knight = false;
    m.level = level;
    m.gender = gender;
    m.health = 100.0;
    m.health_updated = created;
    m.priority = max_priority + 1;
    m.weapons = nlohmann::json::object();
    m.armor = nlohmann::json::object();
    m.equipment = nlohmann::json::object();
    m.abilities = nlohmann::json::array();
    m.status = "active";
    m.created_at = created;
    return m;
}

size_t recruited_member_count(sqlite::database& db, int64_t character_id) {
    int count = 0;
    db << "SELECT COUNT(*) FROM retinue_members WHERE character_id = ? AND is_knight = 0;"
       << character_id >> [&](int c) { count = c; };
    return static_cast<size_t>(count);
}

bool reorder_priority(sqlite::database& db, int64_t character_id,
                      const std::vector<int64_t>& ordered_member_ids)
{
    // Load every member id + knight flag so we can validate the permutation.
    std::vector<std::pair<int64_t, int>> rows;
    db << "SELECT id, is_knight FROM retinue_members WHERE character_id = ?;"
       << character_id >> [&](int64_t id, int k) { rows.emplace_back(id, k); };

    std::vector<int64_t> current_non_knight;
    for (const auto& [id, is_knight] : rows) {
        if (!is_knight) current_non_knight.push_back(id);
    }

    // The submitted list must be exactly a permutation of the non-knight members.
    if (ordered_member_ids.size() != current_non_knight.size()) return false;
    std::set<int64_t> submitted(ordered_member_ids.begin(), ordered_member_ids.end());
    std::set<int64_t> current(current_non_knight.begin(), current_non_knight.end());
    if (submitted != current) return false;

    // The knight is always first (priority 0); ordered members get 1..n.
    db << "UPDATE retinue_members SET priority = 0 WHERE character_id = ? AND is_knight = 1;"
       << character_id;
    for (size_t i = 0; i < ordered_member_ids.size(); ++i) {
        db << "UPDATE retinue_members SET priority = ? WHERE id = ? AND character_id = ?;"
           << static_cast<int>(i + 1) << ordered_member_ids[i] << character_id;
    }
    return true;
}

void apply_casualties(sqlite::database& db, int64_t character_id,
                      const std::vector<int64_t>& member_ids)
{
    for (int64_t member_id : member_ids) {
        db << "UPDATE retinue_members SET status = 'casualty' WHERE id = ? AND character_id = ?;"
           << member_id << character_id;
    }
    if (!member_ids.empty()) {
        std::cout << "[combat] marked " << member_ids.size() << " retinue member(s) of character "
                  << character_id << " as casualties" << std::endl;
    }
}

void apply_recovery(sqlite::database& db, std::vector<retinue_member>& members,
                    int64_t now, double recovery_multiplier, double max_recovery_hours)
{
    if (max_recovery_hours <= 0.0) max_recovery_hours = 16.0;
    const double hp_per_hour = 100.0 / max_recovery_hours * std::max(0.0, recovery_multiplier);
    if (hp_per_hour <= 0.0) return;

    for (auto& m : members) {
        if (m.health >= 100.0 - 1e-9) {
            m.health = 100.0;
            continue;
        }
        const double elapsed_hours = std::max(0.0, static_cast<double>(now - m.health_updated) / 3600.0);
        const double healed = std::min(100.0, m.health + elapsed_hours * hp_per_hour);
        if (healed <= m.health + 1e-9) continue;
        m.health = healed;
        if (m.health >= 100.0 - 1e-9) {
            m.health = 100.0;
            db << "UPDATE retinue_members SET health = 100, health_updated = ? WHERE id = ?;"
               << now << m.id;
        }
    }
}

void apply_wounds(sqlite::database& db, int64_t character_id,
                  const std::vector<wound_spec>& wounds, int64_t now)
{
    if (wounds.empty()) return;
    const int64_t ts = (now > 0) ? now
        : std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    for (const auto& w : wounds) {
        const double hp = std::max(0.0, std::min(100.0, w.health));
        // Only wound the member if the new floor actually drops their health;
        // an already-worse injury keeps its (later) recovery clock so the
        // accrued recovery isn't lost to a weaker re-fall.
        double cur = -1.0;
        db << "SELECT health FROM retinue_members WHERE id = ? AND character_id = ?;"
           << w.member_id << character_id >> [&](double h) { cur = h; };
        if (cur < 0.0) continue;                       // not this character's member
        if (hp + 1e-9 >= cur) continue;                // no change — keep clock
        db << "UPDATE retinue_members SET health = ?, health_updated = ?, "
              "status = 'active' WHERE id = ? AND character_id = ?;"
           << hp << ts << w.member_id << character_id;
    }
    std::cout << "[combat] wounded " << wounds.size() << " retinue member(s) of character "
              << character_id << " (no-death model; they recover)" << std::endl;
}

} // namespace retinue_db