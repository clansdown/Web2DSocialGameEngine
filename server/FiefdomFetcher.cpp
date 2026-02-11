#include "FiefdomFetcher.hpp"
#include "Database.hpp"
#include <iostream>
#include <cctype>
#include <algorithm>

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
        SELECT owner_id, name, x, y, peasants, gold, grain, wood, steel, bronze, stone, leather, mana, wall_count, morale
        FROM fiefdoms WHERE id = ?;
    )" << fiefdom_id
    >> [&](int owner_id, std::string name, int x, int y,
           int peasants, int gold, int grain, int wood, int steel,
           int bronze, int stone, int leather, int mana, int wall_count, double morale) {
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
        fiefdom.morale = morale;
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
    
    db << "SELECT id, name, level, construction_start_ts, action_start_ts, action_tag FROM fiefdom_buildings WHERE fiefdom_id = ?;"
       << fiefdom_id
       >> [&](int id, std::string name, int level, int64_t construction_start_ts,
              int64_t action_start_ts, std::string action_tag) {
           BuildingData building;
           building.id = id;
           building.name = name;
           building.level = level;
           building.construction_start_ts = construction_start_ts;
           building.action_start_ts = action_start_ts;
           building.action_tag = action_tag;
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
                    const std::string& action_tag) {
    auto& db = Database::getInstance().gameDB();
    
    try {
        db << R"(
            INSERT INTO fiefdom_buildings (fiefdom_id, name, level, construction_start_ts, action_start_ts, action_tag)
            VALUES (?, ?, ?, ?, ?, ?);
        )" << fiefdom_id << name << level << construction_start_ts << action_start_ts << action_tag;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create building: " << e.what() << std::endl;
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