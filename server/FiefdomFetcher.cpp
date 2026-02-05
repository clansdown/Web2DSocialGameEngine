#include "FiefdomFetcher.hpp"
#include "Database.hpp"
#include <iostream>
#include <cctype>
#include <algorithm>

namespace FiefdomFetcher {

std::optional<FiefdomData> fetchFiefdomById(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    
    FiefdomData fiefdom;
    fiefdom.id = fiefdom_id;
    
    bool found = false;
    db << R"(
        SELECT owner_id, name, x, y, peasants, gold, grain, wood, steel, bronze, stone, leather, mana, wall_count
        FROM fiefdoms WHERE id = ?;
    )" << fiefdom_id
    >> [&](int owner_id, std::string name, int x, int y,
           int peasants, int gold, int grain, int wood, int steel,
           int bronze, int stone, int leather, int mana, int wall_count) {
        fiefdom.owner_id = owner_id;
        fiefdom.name = name;
        fiefdom.x = x;
        fiefdom.y = y;
        fiefdom.peasants = peasants;
        fiefdom.gold = gold;
        fiefdom.grain = grain;
        fiefdom.wood = wood;
        fiefdom.steel = steel;
        fiefdom.bronze = bronze;
        fiefdom.stone = stone;
        fiefdom.leather = leather;
        fiefdom.mana = mana;
        fiefdom.wall_count = wall_count;
        found = true;
    };
    
    if (!found) {
        return std::nullopt;
    }
    
    fiefdom.buildings = fetchFiefdomBuildings(fiefdom_id);
    fiefdom.officials = fetchFiefdomOfficials(fiefdom_id);
    
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
    
    db << "SELECT id, name FROM fiefdom_buildings WHERE fiefdom_id = ?;"
       << fiefdom_id
       >> [&](int id, std::string name) {
           BuildingData building;
           building.id = id;
           building.name = name;
           buildings.push_back(building);
       };
    
    return buildings;
}

std::vector<OfficialData> fetchFiefdomOfficials(int fiefdom_id) {
    auto& db = Database::getInstance().gameDB();
    
    std::vector<OfficialData> officials;
    
    db << R"(
        SELECT id, role, portrait_id, name, level, intelligence, charisma, wisdom, diligence
        FROM officials WHERE fiefdom_id = ?;
    )" << fiefdom_id
    >> [&](int id, std::string role_str, int portrait_id, std::string name, int level,
           int intelligence, int charisma, int wisdom, int diligence) {
        OfficialData official;
        official.id = id;
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
        SELECT id, role, portrait_id, name, level, intelligence, charisma, wisdom, diligence
        FROM officials WHERE id = ?;
    )" << official_id
    >> [&](int id, std::string role_str, int portrait_id, std::string name, int level,
           int intelligence, int charisma, int wisdom, int diligence) {
        official.id = id;
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

bool createBuilding(int fiefdom_id, const std::string& name) {
    auto& db = Database::getInstance().gameDB();
    
    try {
        db << "INSERT INTO fiefdom_buildings (fiefdom_id, name) VALUES (?, ?);"
           << fiefdom_id << name;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create building: " << e.what() << std::endl;
        return false;
    }
}

bool createOfficial(int fiefdom_id, fiefdom::OfficialRole role, int portrait_id,
                    const std::string& name, int level,
                    uint8_t intelligence, uint8_t charisma, uint8_t wisdom, uint8_t diligence) {
    auto& db = Database::getInstance().gameDB();
    
    try {
        std::string role_str = fiefdom::roleToStringLower(role);
        db << R"(
            INSERT INTO officials (fiefdom_id, role, portrait_id, name, level, intelligence, charisma, wisdom, diligence)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
        )" << fiefdom_id << role_str << portrait_id << name << level
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
                grain = ?,
                wood = ?,
                steel = ?,
                bronze = ?,
                stone = ?,
                leather = ?,
                mana = ?
            WHERE id = ?;
        )" << resources.gold << resources.grain << resources.wood << resources.steel
           << resources.bronze << resources.stone << resources.leather << resources.mana
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

} // namespace FiefdomFetcher