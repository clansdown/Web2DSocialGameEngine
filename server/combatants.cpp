#include "combatants.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace Combatants {

static std::string readFileToString(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

CombatantRegistry& CombatantRegistry::getInstance() {
    static CombatantRegistry instance;
    return instance;
}

bool CombatantRegistry::loadPlayerCombatants(const std::string& config_path) {
    std::string content = readFileToString(config_path);
    if (content.empty()) {
        std::cerr << "Failed to open player combatants config: " << config_path << std::endl;
        return false;
    }
    
    try {
        nlohmann::json data = nlohmann::json::parse(content, nullptr, true, true);
        
        for (auto& [id, combatant_json] : data.items()) {
            Combatant c;
            c.id = id;
            c.name = combatant_json["name"].get<std::string>();
            c.max_level = combatant_json["max_level"].get<int>();

            if (combatant_json.contains("morale_boost")) {
                for (auto& boost : combatant_json["morale_boost"]) {
                    c.morale_boost.push_back(boost.get<double>());
                }
            }

            if (combatant_json.contains("damage")) {
                for (auto& damage_obj : combatant_json["damage"]) {
                    DamageStats ds;
                    if (damage_obj.contains("melee")) ds.melee = damage_obj["melee"];
                    if (damage_obj.contains("ranged")) ds.ranged = damage_obj["ranged"];
                    if (damage_obj.contains("magical")) ds.magical = damage_obj["magical"];
                    c.damage.push_back(ds);
                }
            }

            if (combatant_json.contains("defense")) {
                for (auto& def_obj : combatant_json["defense"]) {
                    if (def_obj.is_null()) {
                        c.defense.push_back(std::nullopt);
                    } else {
                        DefenseStats ds;
                        if (def_obj.contains("melee")) ds.melee = def_obj["melee"];
                        if (def_obj.contains("ranged")) ds.ranged = def_obj["ranged"];
                        if (def_obj.contains("magical")) ds.magical = def_obj["magical"];
                        c.defense.push_back(ds);
                    }
                }
            }

            if (combatant_json.contains("movement_speed")) {
                for (auto& speed : combatant_json["movement_speed"]) {
                    c.movement_speed.push_back(speed.get<double>());
                }
            }

            if (combatant_json.contains("costs")) {
                for (auto& cost_obj : combatant_json["costs"]) {
                    CostStats cs;
                    if (cost_obj.contains("gold")) cs.gold = cost_obj["gold"];
                    if (cost_obj.contains("grain")) cs.grain = cost_obj["grain"];
                    if (cost_obj.contains("wood")) cs.wood = cost_obj["wood"];
                    if (cost_obj.contains("steel")) cs.steel = cost_obj["steel"];
                    if (cost_obj.contains("bronze")) cs.bronze = cost_obj["bronze"];
                    if (cost_obj.contains("stone")) cs.stone = cost_obj["stone"];
                    if (cost_obj.contains("leather")) cs.leather = cost_obj["leather"];
                    c.costs.push_back(cs);
                }
            }

            player_combatants_[id] = std::move(c);
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse player combatants: " << e.what() << std::endl;
        return false;
    }
}

bool CombatantRegistry::loadEnemyCombatants(const std::string& config_path) {
    std::string content = readFileToString(config_path);
    if (content.empty()) {
        std::cerr << "Failed to open enemy combatants config: " << config_path << std::endl;
        return false;
    }
    
    try {
        nlohmann::json data = nlohmann::json::parse(content, nullptr, true, true);
        
        for (auto& [id, combatant_json] : data.items()) {
            Combatant c;
            c.id = id;
            c.name = combatant_json["name"].get<std::string>();
            c.max_level = combatant_json["max_level"].get<int>();

            if (combatant_json.contains("morale_boost")) {
                for (auto& boost : combatant_json["morale_boost"]) {
                    c.morale_boost.push_back(boost.get<double>());
                }
            }

            if (combatant_json.contains("damage")) {
                for (auto& damage_obj : combatant_json["damage"]) {
                    DamageStats ds;
                    if (damage_obj.contains("melee")) ds.melee = damage_obj["melee"];
                    if (damage_obj.contains("ranged")) ds.ranged = damage_obj["ranged"];
                    if (damage_obj.contains("magical")) ds.magical = damage_obj["magical"];
                    c.damage.push_back(ds);
                }
            }

            if (combatant_json.contains("defense")) {
                for (auto& def_obj : combatant_json["defense"]) {
                    if (def_obj.is_null()) {
                        c.defense.push_back(std::nullopt);
                    } else {
                        DefenseStats ds;
                        if (def_obj.contains("melee")) ds.melee = def_obj["melee"];
                        if (def_obj.contains("ranged")) ds.ranged = def_obj["ranged"];
                        if (def_obj.contains("magical")) ds.magical = def_obj["magical"];
                        c.defense.push_back(ds);
                    }
                }
            }

            if (combatant_json.contains("movement_speed")) {
                for (auto& speed : combatant_json["movement_speed"]) {
                    c.movement_speed.push_back(speed.get<double>());
                }
            }

            if (combatant_json.contains("costs")) {
                for (auto& cost_obj : combatant_json["costs"]) {
                    CostStats cs;
                    if (cost_obj.contains("gold")) cs.gold = cost_obj["gold"];
                    if (cost_obj.contains("grain")) cs.grain = cost_obj["grain"];
                    if (cost_obj.contains("wood")) cs.wood = cost_obj["wood"];
                    if (cost_obj.contains("steel")) cs.steel = cost_obj["steel"];
                    if (cost_obj.contains("bronze")) cs.bronze = cost_obj["bronze"];
                    if (cost_obj.contains("stone")) cs.stone = cost_obj["stone"];
                    if (cost_obj.contains("leather")) cs.leather = cost_obj["leather"];
                    c.costs.push_back(cs);
                }
            }

            enemy_combatants_[id] = std::move(c);
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse enemy combatants: " << e.what() << std::endl;
        return false;
    }
}

bool CombatantRegistry::loadDamageTypes(const std::string& config_path) {
    std::string content = readFileToString(config_path);
    if (content.empty()) {
        std::cerr << "Failed to open damage types config: " << config_path << std::endl;
        return false;
    }
    
    try {
        nlohmann::json data = nlohmann::json::parse(content, nullptr, true, true);
        damage_types_ = data.get<std::vector<std::string>>();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse damage types: " << e.what() << std::endl;
        return false;
    }
}

std::optional<const Combatant*> CombatantRegistry::getPlayerCombatant(const std::string& id) const {
    auto it = player_combatants_.find(id);
    if (it != player_combatants_.end()) {
        return &it->second;
    }
    return std::nullopt;
}

std::optional<const Combatant*> CombatantRegistry::getEnemyCombatant(const std::string& id) const {
    auto it = enemy_combatants_.find(id);
    if (it != enemy_combatants_.end()) {
        return &it->second;
    }
    return std::nullopt;
}

const std::vector<std::string>& CombatantRegistry::getDamageTypes() const {
    return damage_types_;
}

void CombatantRegistry::forEachPlayerCombatant(const std::function<void(const Combatant&)>& callback) const {
    for (const auto& [id, combatant] : player_combatants_) {
        callback(combatant);
    }
}

void CombatantRegistry::forEachEnemyCombatant(const std::function<void(const Combatant&)>& callback) const {
    for (const auto& [id, combatant] : enemy_combatants_) {
        callback(combatant);
    }
}

} // namespace Combatants