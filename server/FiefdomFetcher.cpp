#include "FiefdomFetcher.hpp"
#include "Database.hpp"
#include <iostream>
#include <cctype>
#include <algorithm>
#include <set>

namespace FiefdomFetcher {

std::optional<FiefdomData> fetchFiefdomById(
    int fiefdom_id,
    bool include_buildings,
    bool include_officials,
    bool include_heroes,
    bool include_combatants
) {
    auto& db = Database::getInstance().gameDB();
    
    FiefdomData fiefdom;
    fiefdom.id = fiefdom_id;
    
    bool found = false;
    db << R"(
        SELECT owner_id, name, x, y, peasants, gold, silver_pence, grain, wood, steel, bronze, stone, leather, mana, charcoal, iron, ironwork, fancy_ironwork, wall_count, morale, last_update_time, manor_level, import_settings, reserves
        FROM fiefdoms WHERE id = ?;
    )" << fiefdom_id
    >> [&](int owner_id, std::string name, int x, int y,
           int peasants, double gold, int silver_pence, int grain, int wood, int steel,
           int bronze, int stone, int leather, int mana, int charcoal, int iron, int ironwork, int fancy_ironwork, int wall_count, double morale, int64_t last_update_time, int manor_level, std::string import_settings_str, std::string reserves_str) {
        fiefdom.owner_id = owner_id;
        fiefdom.name = name;
        fiefdom.x = x;
        fiefdom.y = y;
        fiefdom.peasants = peasants;
        fiefdom.gold = gold;
        fiefdom.silver_pence = silver_pence;
        fiefdom.grain = grain;
        fiefdom.wood = wood;
        fiefdom.steel = steel;
        fiefdom.bronze = bronze;
        fiefdom.stone = stone;
        fiefdom.leather = leather;
        fiefdom.mana = mana;
        fiefdom.charcoal = charcoal;
        fiefdom.iron = iron;
        fiefdom.ironwork = ironwork;
        fiefdom.fancy_ironwork = fancy_ironwork;
        fiefdom.wall_count = wall_count;
        fiefdom.morale = morale;
        fiefdom.last_update_time = last_update_time;
        fiefdom.manor_level = manor_level;
        try { fiefdom.import_settings = nlohmann::json::parse(import_settings_str); }
        catch (...) { fiefdom.import_settings = nlohmann::json::object(); }
        try { fiefdom.reserves = nlohmann::json::parse(reserves_str); }
        catch (...) { fiefdom.reserves = nlohmann::json::object(); }
        found = true;
    };
    
    if (!found) {
        return std::nullopt;
    }

    if (include_buildings) {
        fiefdom.buildings = fetchFiefdomBuildings(fiefdom_id);
    }
    
    if (include_officials) {
        fiefdom.officials = fetchFiefdomOfficials(fiefdom_id);
    }
    
    if (include_heroes) {
        fiefdom.heroes = fetchFiefdomHeroes(fiefdom_id);
    }
    
    if (include_combatants) {
        fiefdom.stationed_combatants = fetchStationedCombatants(fiefdom_id);
    }

    return fiefdom;
}

std::vector<FiefdomData> fetchFiefdomsByOwnerId(int owner_id) {
    auto& db = Database::getInstance().gameDB();
    
    std::vector<FiefdomData> fiefdoms;
    
    db << R"(
        SELECT id FROM fiefdoms WHERE owner_id = ?;
    )" << owner_id
    >> [&](int fiefdom_id) {
        auto fiefdom_opt = fetchFiefdomById(fiefdom_id);
        if (fiefdom_opt.has_value()) {
            fiefdoms.push_back(fiefdom_opt.value());
        }
    };
    
    return fiefdoms;
}

std::vector<BuildingData> fetchFiefdomBuildings(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();

    std::vector<BuildingData> buildings;

    db << "SELECT id, name, level, x, y, construction_start_ts, last_updated, action_start_ts, action_tag, pond_type, output_rates FROM fiefdom_buildings WHERE fiefdom_id = ?;"
       << fiefdom_id
       >> [&](int id, std::string name, int level, int x, int y, int64_t construction_start_ts,
              int64_t last_updated, int64_t action_start_ts, std::string action_tag, std::string pond_type, std::string output_rates_str) {
           BuildingData building;
           building.id = id;
           building.name = name;
           building.level = level;
           building.x = x;
           building.y = y;
           building.construction_start_ts = construction_start_ts;
           building.last_updated = last_updated;
           building.action_start_ts = action_start_ts;
           building.action_tag = action_tag;
           building.pond_type = pond_type;
           try { building.output_rates = nlohmann::json::parse(output_rates_str); }
           catch (...) { building.output_rates = nlohmann::json::object(); }
           buildings.push_back(building);
       };

    return buildings;
}

std::vector<OfficialData> fetchFiefdomOfficials(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();

    std::vector<OfficialData> officials;

    db << R"(
        SELECT id, role, template_id, portrait_id, name, level, intelligence, charisma, wisdom, diligence
        FROM officials WHERE fiefdom_id = ?;
    )" << fiefdom_id
    >> [&](int id, std::string role_str, std::string template_id, int portrait_id, std::string name, int level,
           int intelligence, int charisma, int wisdom, int diligence) {
        OfficialData official;
        official.id = id;
        official.template_id = template_id;
        official.portrait_id = portrait_id;
        official.name = name;
        official.level = level;
        official.intelligence = static_cast<uint8_t>(intelligence);
        official.charisma = static_cast<uint8_t>(charisma);
        official.wisdom = static_cast<uint8_t>(wisdom);
        official.diligence = static_cast<uint8_t>(diligence);

        auto role_opt = fiefdom::roleFromString(role_str);
        if (role_opt.has_value()) {
            official.role = role_opt.value();
        } else {
            std::cerr << "Unknown official role: " << role_str << " for fiefdom_id=" << fiefdom_id << std::endl;
            official.role = fiefdom::OfficialRole::Bailiff;
        }

        officials.push_back(official);
    };

    return officials;
}

std::optional<OfficialData> fetchOfficialById(int official_id) {
    auto& db = Database::getInstance().gameDB();

    OfficialData official;
    bool found = false;

    db << R"(
        SELECT id, role, template_id, portrait_id, name, level, intelligence, charisma, wisdom, diligence
        FROM officials WHERE id = ?;
    )" << official_id
    >> [&](int id, std::string role_str, std::string template_id, int portrait_id, std::string name, int level,
           int intelligence, int charisma, int wisdom, int diligence) {
        official.id = id;
        official.template_id = template_id;
        official.portrait_id = portrait_id;
        official.name = name;
        official.level = level;
        official.intelligence = static_cast<uint8_t>(intelligence);
        official.charisma = static_cast<uint8_t>(charisma);
        official.wisdom = static_cast<uint8_t>(wisdom);
        official.diligence = static_cast<uint8_t>(diligence);

        auto role_opt = fiefdom::roleFromString(role_str);
        if (role_opt.has_value()) {
            official.role = role_opt.value();
        } else {
            official.role = fiefdom::OfficialRole::Bailiff;
        }

        found = true;
    };

    if (!found) {
        return std::nullopt;
    }

    return official;
}

bool createBuilding(int fiefdom_id, const std::string& name, int level,
                    int64_t construction_start_ts, int64_t action_start_ts,
                    const std::string& action_tag, int x, int y,
                    const std::string& pond_type) {
    auto& db = Database::getInstance().gameDB();

    try {
        db << R"(
            INSERT INTO fiefdom_buildings
            (fiefdom_id, name, level, x, y, construction_start_ts, last_updated, action_start_ts, action_tag, pond_type)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )" << fiefdom_id << name << level << x << y << construction_start_ts
           << construction_start_ts << action_start_ts << action_tag << pond_type;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create building: " << e.what() << std::endl;
        return false;
    }
}

bool updateBuildingLevel(int building_id, int new_level, int64_t timestamp) {
    auto& db = Database::getInstance().gameDB();

    try {
        db << R"(
            UPDATE fiefdom_buildings
            SET level = ?, construction_start_ts = ?, last_updated = ?
            WHERE id = ?;
        )" << new_level << 0 << timestamp << building_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to update building level: " << e.what() << std::endl;
        return false;
    }
}

bool updateBuildingConstructionStart(int building_id, int64_t construction_start_ts, int64_t timestamp) {
    auto& db = Database::getInstance().gameDB();

    try {
        db << R"(
            UPDATE fiefdom_buildings
            SET construction_start_ts = ?, last_updated = ?
            WHERE id = ?;
        )" << construction_start_ts << timestamp << building_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to update building construction: " << e.what() << std::endl;
        return false;
    }
}

bool updateBuildingPondType(int building_id, const std::string& pond_type, int new_level,
                            int64_t construction_start_ts, int64_t timestamp) {
    auto& db = Database::getInstance().gameDB();

    try {
        db << R"(
            UPDATE fiefdom_buildings
            SET pond_type = ?, level = ?, construction_start_ts = ?, last_updated = ?
            WHERE id = ?;
        )" << pond_type << new_level << construction_start_ts << timestamp << building_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to update building pond type: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::pair<int, int>> fetchRiverCells(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();

    std::vector<std::pair<int, int>> cells;
    db << "SELECT x, y FROM fiefdom_river WHERE fiefdom_id = ? ORDER BY x, y;"
       << fiefdom_id
       >> [&](int x, int y) { cells.push_back({x, y}); };

    return cells;
}

namespace {

// Traces the integer cells a line segment passes through (Bresenham).
std::vector<std::pair<int, int>> traceSegment(int x0, int y0, int x1, int y1) {
    std::vector<std::pair<int, int>> cells;
    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;
    while (true) {
        cells.push_back({x, y});
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
    }
    return cells;
}

} // namespace

// Seeds a fiefdom's river from the manor_river.json templates if none exists
// yet. Template and rotation are chosen deterministically from the fiefdom id:
// template = fiefdom_id % N, rotation = (fiefdom_id / N) % 4 (0/90/180/270 deg
// about the manor origin), so the same fiefdom always gets the same river while
// different fiefdoms vary. Returns false if the config is unusable.
bool ensureFiefdomRiver(int fiefdom_id, const nlohmann::json& river_config) {
    if (!river_config.is_object() || !river_config.contains("templates") ||
        !river_config["templates"].is_array() || river_config["templates"].empty()) {
        return false;
    }

    auto& db = Database::getInstance().gameDB();
    int count = 0;
    db << "SELECT COUNT(*) FROM fiefdom_river WHERE fiefdom_id = ?;" << fiefdom_id
       >> [&](int c) { count = c; };
    if (count > 0) return true;

    auto templates = river_config["templates"];
    size_t template_index = static_cast<size_t>(fiefdom_id) % templates.size();
    int rotation = static_cast<int>(static_cast<size_t>(fiefdom_id) / templates.size()) % 4;

    auto tmpl = templates[template_index];
    if (!tmpl.is_object() || !tmpl.contains("points") || !tmpl["points"].is_array()) return false;
    int width = tmpl.value("width", 1);
    if (width < 1) width = 1;

    // Expand the polyline into a canonical cell band. Width thickens
    // perpendicular to each segment's dominant axis.
    std::vector<std::pair<int, int>> canonical;
    auto add_band = [&](int x, int y, bool horizontal_flow) {
        canonical.push_back({x, y});
        for (int i = 1; i < width; i++) {
            canonical.push_back(horizontal_flow ? std::pair<int, int>{x, y + i}
                                                : std::pair<int, int>{x + i, y});
        }
    };

    auto points = tmpl["points"];
    for (size_t i = 0; i + 1 < points.size(); i++) {
        auto a = points[i], b = points[i + 1];
        if (!a.is_array() || a.size() < 2 || !b.is_array() || b.size() < 2) continue;
        int x0 = a[0].get<int>(), y0 = a[1].get<int>();
        int x1 = b[0].get<int>(), y1 = b[1].get<int>();
        bool horizontal_flow = std::abs(x1 - x0) >= std::abs(y1 - y0);
        for (const auto& [cx, cy] : traceSegment(x0, y0, x1, y1)) {
            add_band(cx, cy, horizontal_flow);
        }
    }

    // Rotate the canonical band about the origin and insert per-fiefdom cells.
    std::set<std::pair<int, int>> seen;
    for (const auto& [cx, cy] : canonical) {
        int rx = cx, ry = cy;
        switch (rotation) {
            case 1: rx = -cy; ry = cx; break;
            case 2: rx = -cx; ry = -cy; break;
            case 3: rx = cy; ry = -cx; break;
            default: break;
        }
        if (seen.insert({rx, ry}).second) {
            db << "INSERT INTO fiefdom_river (fiefdom_id, x, y) VALUES (?, ?, ?);"
               << fiefdom_id << rx << ry;
        }
    }

    return true;
}

std::vector<WallData> fetchFiefdomWalls(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();

    std::vector<WallData> walls;

    db << "SELECT id, generation, level, hp, construction_start_ts, last_updated FROM fiefdom_walls WHERE fiefdom_id = ?;"
       << fiefdom_id
       >> [&](int id, int generation, int level, int hp, int64_t construction_start_ts, int64_t last_updated) {
           WallData wall;
           wall.id = id;
           wall.fiefdom_id = fiefdom_id;
           wall.generation = generation;
           wall.level = level;
           wall.hp = hp;
           wall.construction_start_ts = construction_start_ts;
           wall.last_updated = last_updated;
           walls.push_back(wall);
       };

    return walls;
}

bool createWall(int fiefdom_id, int generation, int level, int hp, int64_t construction_start_ts) {
    auto& db = Database::getInstance().gameDB();

    try {
        db << R"(
            INSERT INTO fiefdom_walls (fiefdom_id, generation, level, hp, construction_start_ts, last_updated)
            VALUES (?, ?, ?, ?, ?, ?);
        )" << fiefdom_id << generation << level << hp << construction_start_ts << construction_start_ts;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create wall: " << e.what() << std::endl;
        return false;
    }
}

bool updateWallLevel(int wall_id, int new_level, int new_hp, int64_t timestamp) {
    auto& db = Database::getInstance().gameDB();

    try {
        db << R"(
            UPDATE fiefdom_walls
            SET level = ?, hp = ?, construction_start_ts = ?, last_updated = ?
            WHERE id = ?;
        )" << new_level << new_hp << 0 << timestamp << wall_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to update wall level: " << e.what() << std::endl;
        return false;
    }
}

bool updateWallHP(int wall_id, int new_hp) {
    auto& db = Database::getInstance().gameDB();

    try {
        db << "UPDATE fiefdom_walls SET hp = ? WHERE id = ?;" << new_hp << wall_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to update wall HP: " << e.what() << std::endl;
        return false;
    }
}

bool deleteWall(int wall_id) {
    auto& db = Database::getInstance().gameDB();

    try {
        db << "DELETE FROM fiefdom_walls WHERE id = ?;" << wall_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to delete wall: " << e.what() << std::endl;
        return false;
    }
}

bool createOfficial(int fiefdom_id, fiefdom::OfficialRole role, const std::string& template_id,
                    int portrait_id, const std::string& name, int level,
                    uint8_t intelligence, uint8_t charisma, uint8_t wisdom, uint8_t diligence) {
    auto& db = Database::getInstance().gameDB();

    try {
        std::string role_str = fiefdom::roleToStringLower(role);
        db << R"(
            INSERT INTO officials (fiefdom_id, role, template_id, portrait_id, name, level, intelligence, charisma, wisdom, diligence)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )" << fiefdom_id << role_str << template_id << portrait_id << name << level
           << static_cast<int>(intelligence) << static_cast<int>(charisma)
           << static_cast<int>(wisdom) << static_cast<int>(diligence);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create official: " << e.what() << std::endl;
        return false;
    }
}

bool updateFiefdomResources(int fiefdom_id, const FiefdomResources& resources) {
    auto& db = Database::getInstance().gameDB();
    
    try {
        db << R"(
            UPDATE fiefdoms SET
                gold = ?,
                silver_pence = ?,
                grain = ?,
                wood = ?,
                steel = ?,
                bronze = ?,
                stone = ?,
                leather = ?,
                mana = ?,
                charcoal = ?,
                iron = ?,
                ironwork = ?,
                fancy_ironwork = ?
            WHERE id = ?;
        )" << resources.gold << resources.silver_pence << resources.grain << resources.wood << resources.steel
           << resources.bronze << resources.stone << resources.leather << resources.mana
           << resources.charcoal << resources.iron << resources.ironwork << resources.fancy_ironwork
           << fiefdom_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to update fiefdom resources: " << e.what() << std::endl;
        return false;
    }
}

bool updateFiefdomPeasants(int fiefdom_id, int peasants) {
    auto& db = Database::getInstance().gameDB();
    
    try {
        db << "UPDATE fiefdoms SET peasants = ? WHERE id = ?;"
           << peasants << fiefdom_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to update fiefdom peasants: " << e.what() << std::endl;
        return false;
    }
}

bool updateFiefdomWallCount(int fiefdom_id, int wall_count) {
    auto& db = Database::getInstance().gameDB();

    try {
        db << "UPDATE fiefdoms SET wall_count = ? WHERE id = ?;"
           << wall_count << fiefdom_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to update fiefdom wall count: " << e.what() << std::endl;
        return false;
    }
}

bool updateFiefdomMorale(int fiefdom_id, double morale) {
    auto& db = Database::getInstance().gameDB();

    try {
        db << "UPDATE fiefdoms SET morale = ? WHERE id = ?;"
           << morale << fiefdom_id;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to update fiefdom morale: " << e.what() << std::endl;
        return false;
    }
}

std::vector<FiefdomHero> fetchFiefdomHeroes(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    std::vector<FiefdomHero> heroes;

    db << "SELECT id, hero_config_id, level FROM fiefdom_heroes WHERE fiefdom_id = ?;"
       << fiefdom_id
       >> [&](int id, std::string hero_config_id, int level) {
           FiefdomHero hero;
           hero.id = id;
           hero.hero_config_id = hero_config_id;
           hero.level = level;
           heroes.push_back(hero);
       };

    return heroes;
}

std::vector<StationedCombatant> fetchStationedCombatants(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    std::vector<StationedCombatant> combatants;

    db << "SELECT id, combatant_config_id, level FROM stationed_combatants WHERE fiefdom_id = ?;"
       << fiefdom_id
       >> [&](int id, std::string combatant_config_id, int level) {
           StationedCombatant combatant;
           combatant.id = id;
           combatant.combatant_config_id = combatant_config_id;
           combatant.level = level;
           combatants.push_back(combatant);
       };

    return combatants;
}

bool createFiefdomHero(int fiefdom_id, const std::string& hero_config_id, int level) {
    auto& db = Database::getInstance().gameDB();
    try {
        db << "INSERT INTO fiefdom_heroes (fiefdom_id, hero_config_id, level) VALUES (?, ?, ?);"
           << fiefdom_id << hero_config_id << level;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create fiefdom hero: " << e.what() << std::endl;
        return false;
    }
}

bool createStationedCombatant(int fiefdom_id, const std::string& combatant_config_id, int level) {
    auto& db = Database::getInstance().gameDB();
    try {
        db << "INSERT INTO stationed_combatants (fiefdom_id, combatant_config_id, level) VALUES (?, ?, ?);"
           << fiefdom_id << combatant_config_id << level;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create stationed combatant: " << e.what() << std::endl;
        return false;
    }
}

} // namespace FiefdomFetcher