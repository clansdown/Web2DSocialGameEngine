#include "RetinueDB.hpp"
#include <chrono>
#include <iostream>
#include <memory>

namespace retinue_db {

nlohmann::json retinue_member::to_json() const {
    nlohmann::json j;
    j["id"] = id;
    j["character_id"] = character_id;
    j["display_name"] = display_name;
    j["unit_class"] = unit_class;
    j["is_knight"] = is_knight;
    j["level"] = level;
    j["weapons"] = weapons;
    j["armor"] = armor;
    j["abilities"] = abilities;
    j["status"] = status;
    j["created_at"] = created_at;
    return j;
}

namespace {

retinue_member row_to_member(int64_t id, int64_t character_id, std::string display_name,
                             std::string unit_class, int is_knight, int level,
                             std::string weapons, std::string armor, std::string abilities,
                             std::string status, int64_t created_at)
{
    retinue_member m;
    m.id = id;
    m.character_id = character_id;
    m.display_name = std::move(display_name);
    m.unit_class = std::move(unit_class);
    m.is_knight = is_knight != 0;
    m.level = level;
    try {
        m.weapons = nlohmann::json::parse(weapons);
        m.armor = nlohmann::json::parse(armor);
        m.abilities = nlohmann::json::parse(abilities);
    } catch (const std::exception&) {
        m.weapons = nlohmann::json::object();
        m.armor = nlohmann::json::object();
        m.abilities = nlohmann::json::array();
    }
    m.status = std::move(status);
    m.created_at = created_at;
    return m;
}

} // namespace

std::vector<retinue_member> load_retinue(sqlite::database& db, int64_t character_id) {
    std::vector<retinue_member> members;
    db << "SELECT id, character_id, display_name, unit_class, is_knight, level, weapons, armor, abilities, status, created_at "
          "FROM retinue_members WHERE character_id = ? ORDER BY is_knight DESC, id ASC;"
       << character_id
       >> [&](int64_t id, int64_t cid, std::string display_name, std::string unit_class,
              int is_knight, int level, std::string weapons, std::string armor,
              std::string abilities, std::string status, int64_t created_at) {
             members.push_back(row_to_member(id, cid, std::move(display_name),
                                             std::move(unit_class), is_knight, level,
                                             std::move(weapons), std::move(armor),
                                             std::move(abilities), std::move(status),
                                             created_at));
         };
    return members;
}

retinue_member get_or_create_knight(sqlite::database& db, int64_t character_id) {
    std::vector<retinue_member> knights;
    db << "SELECT id, character_id, display_name, unit_class, is_knight, level, weapons, armor, abilities, status, created_at "
          "FROM retinue_members WHERE character_id = ? AND is_knight = 1;"
       << character_id
       >> [&](int64_t id, int64_t cid, std::string display_name, std::string unit_class,
              int is_knight, int level, std::string weapons, std::string armor,
              std::string abilities, std::string status, int64_t created_at) {
             knights.push_back(row_to_member(id, cid, std::move(display_name),
                                             std::move(unit_class), is_knight, level,
                                             std::move(weapons), std::move(armor),
                                             std::move(abilities), std::move(status),
                                             created_at));
         };
    if (!knights.empty()) return knights.front();

    // No knight yet — create one from the character's own identity.
    std::string display_name;
    int level = 1;
    db << "SELECT display_name, level FROM characters WHERE id = ?;"
       << character_id
       >> [&](std::string dn, int l) { display_name = dn; level = l; };
    if (display_name.empty()) {
        display_name = "Knight";
    }

    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    db << "INSERT INTO retinue_members (character_id, display_name, unit_class, is_knight, level, weapons, armor, abilities, status, created_at) "
          "VALUES (?, ?, 'knight', 1, ?, '{}', '{}', '[]', 'active', ?);"
       << character_id << display_name << level << now;

    retinue_member knight;
    knight.id = db.last_insert_rowid();
    knight.character_id = character_id;
    knight.display_name = display_name;
    knight.unit_class = "knight";
    knight.is_knight = true;
    knight.level = level;
    knight.weapons = nlohmann::json::object();
    knight.armor = nlohmann::json::object();
    knight.abilities = nlohmann::json::array();
    knight.status = "active";
    knight.created_at = now;
    return knight;
}

std::vector<retinue_member> load_active_retinue(sqlite::database& db, int64_t character_id) {
    get_or_create_knight(db, character_id);
    std::vector<retinue_member> members;
    db << "SELECT id, character_id, display_name, unit_class, is_knight, level, weapons, armor, abilities, status, created_at "
          "FROM retinue_members WHERE character_id = ? AND status = 'active' ORDER BY is_knight DESC, id ASC;"
       << character_id
       >> [&](int64_t id, int64_t cid, std::string display_name, std::string unit_class,
              int is_knight, int level, std::string weapons, std::string armor,
              std::string abilities, std::string status, int64_t created_at) {
             members.push_back(row_to_member(id, cid, std::move(display_name),
                                             std::move(unit_class), is_knight, level,
                                             std::move(weapons), std::move(armor),
                                             std::move(abilities), std::move(status),
                                             created_at));
         };
    return members;
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

} // namespace retinue_db
