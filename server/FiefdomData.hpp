#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <cctype>
#include <nlohmann/json.hpp>

namespace fiefdom {

enum class OfficialRole : uint8_t {
    Bailiff = 0,
    Wizard = 1,
    Architect = 2,
    Steward = 3,
    Reeve = 4,
    Beadle = 5,
    Constable = 6,
    Forester = 7
};

inline const char* officialRoleToString(OfficialRole role) {
    switch (role) {
        case OfficialRole::Bailiff: return "Bailiff";
        case OfficialRole::Wizard: return "Wizard";
        case OfficialRole::Architect: return "Architect";
        case OfficialRole::Steward: return "Steward";
        case OfficialRole::Reeve: return "Reeve";
        case OfficialRole::Beadle: return "Beadle";
        case OfficialRole::Constable: return "Constable";
        case OfficialRole::Forester: return "Forester";
        default: return "Unknown";
    }
}

inline std::string roleToStringLower(OfficialRole role) {
    switch (role) {
        case OfficialRole::Bailiff: return "bailiff";
        case OfficialRole::Wizard: return "wizard";
        case OfficialRole::Architect: return "architect";
        case OfficialRole::Steward: return "steward";
        case OfficialRole::Reeve: return "reeve";
        case OfficialRole::Beadle: return "beadle";
        case OfficialRole::Constable: return "constable";
        case OfficialRole::Forester: return "forester";
        default: return "unknown";
    }
}

inline std::optional<OfficialRole> roleFromString(const std::string& role_str) {
    std::string lower = role_str;
    for (char& c : lower) c = std::tolower(c);
    
    if (lower == "bailiff") return OfficialRole::Bailiff;
    if (lower == "wizard") return OfficialRole::Wizard;
    if (lower == "architect") return OfficialRole::Architect;
    if (lower == "steward") return OfficialRole::Steward;
    if (lower == "reeve") return OfficialRole::Reeve;
    if (lower == "beadle") return OfficialRole::Beadle;
    if (lower == "constable") return OfficialRole::Constable;
    if (lower == "forester") return OfficialRole::Forester;
    return std::nullopt;
}

} // namespace fiefdom

struct OfficialData {
    int id;
    fiefdom::OfficialRole role;
    std::string template_id;
    int portrait_id;
    std::string name;
    int level;
    uint8_t intelligence;
    uint8_t charisma;
    uint8_t wisdom;
    uint8_t diligence;

    inline nlohmann::json toJson() const {
        nlohmann::json json;
        json["id"] = id;
        json["role"] = fiefdom::officialRoleToString(role);
        json["template_id"] = template_id;
        json["portrait_id"] = portrait_id;
        json["name"] = name;
        json["level"] = level;
        json["intelligence"] = static_cast<int>(intelligence);
        json["charisma"] = static_cast<int>(charisma);
        json["wisdom"] = static_cast<int>(wisdom);
        json["diligence"] = static_cast<int>(diligence);
        return json;
    }
};

struct BuildingData {
    int id;
    std::string name;
    int level = 0;
    int x = 0;
    int y = 0;
    int64_t construction_start_ts = 0;
    int64_t last_updated = 0;
    int64_t action_start_ts = 0;
    std::string action_tag;

    inline nlohmann::json toJson() const {
        nlohmann::json json;
        json["id"] = id;
        json["name"] = name;
        json["level"] = level;
        json["x"] = x;
        json["y"] = y;
        json["construction_start_ts"] = construction_start_ts;
        json["last_updated"] = last_updated;
        json["action_start_ts"] = action_start_ts;
        json["action_tag"] = action_tag;
        return json;
    }
};

struct FiefdomHero {
    int id;
    std::string hero_config_id;
    int level;

    inline nlohmann::json toJson() const {
        nlohmann::json json;
        json["id"] = id;
        json["hero_config_id"] = hero_config_id;
        json["level"] = level;
        return json;
    }
};

struct StationedCombatant {
    int id;
    std::string combatant_config_id;
    int level;

    inline nlohmann::json toJson() const {
        nlohmann::json json;
        json["id"] = id;
        json["combatant_config_id"] = combatant_config_id;
        json["level"] = level;
        return json;
    }
};

struct FiefdomData {
    int id;
    int owner_id;
    std::string name;
    int x;
    int y;
    int peasants;
    int gold;
    int grain;
    int wood;
    int steel;
    int bronze;
    int stone;
    int leather;
    int mana;
    int wall_count;
    double morale = 0.0;
    std::vector<BuildingData> buildings;
    std::vector<OfficialData> officials;
    std::vector<FiefdomHero> heroes;
    std::vector<StationedCombatant> stationed_combatants;

    inline nlohmann::json toJson() const {
        nlohmann::json json;
        json["id"] = id;
        json["owner_id"] = owner_id;
        json["name"] = name;
        json["x"] = x;
        json["y"] = y;
        json["peasants"] = peasants;
        json["gold"] = gold;
        json["grain"] = grain;
        json["wood"] = wood;
        json["steel"] = steel;
        json["bronze"] = bronze;
        json["stone"] = stone;
        json["leather"] = leather;
        json["mana"] = mana;
        json["wall_count"] = wall_count;
        json["morale"] = morale;

        nlohmann::json buildings_arr = nlohmann::json::array();
        for (const auto& b : buildings) {
            buildings_arr.push_back(b.toJson());
        }
        json["buildings"] = buildings_arr;

        nlohmann::json officials_arr = nlohmann::json::array();
        for (const auto& o : officials) {
            officials_arr.push_back(o.toJson());
        }
        json["officials"] = officials_arr;

        nlohmann::json heroes_arr = nlohmann::json::array();
        for (const auto& h : heroes) {
            heroes_arr.push_back(h.toJson());
        }
        json["heroes"] = heroes_arr;

        nlohmann::json combatants_arr = nlohmann::json::array();
        for (const auto& c : stationed_combatants) {
            combatants_arr.push_back(c.toJson());
        }
        json["stationed_combatants"] = combatants_arr;

        return json;
    }
};