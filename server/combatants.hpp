#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>

namespace Combatants {

struct DamageStats {
    int melee = 0;
    int ranged = 0;
    int magical = 0;
};

struct DefenseStats {
    int melee = 0;
    int ranged = 0;
    int magical = 0;
};

struct CostStats {
    int gold = 0;
    int grain = 0;
    int wood = 0;
    int steel = 0;
    int bronze = 0;
    int stone = 0;
    int leather = 0;
};

struct Combatant {
    std::string id;
    std::string name;
    int max_level = 1;
    
    std::vector<DamageStats> damage;
    std::vector<std::optional<DefenseStats>> defense;
    std::vector<double> movement_speed;
    std::vector<CostStats> costs;
    
    DamageStats getDamage(int level) const {
        if (damage.empty()) {
            return DamageStats{};
        }
        int idx = level - 1;
        if (idx < damage.size()) {
            return damage[idx];
        }
        int last_idx = damage.size() - 1;
        int delta_melee = damage[last_idx].melee - damage[last_idx - 1].melee;
        int delta_ranged = damage[last_idx].ranged - damage[last_idx - 1].ranged;
        int delta_magical = damage[last_idx].magical - damage[last_idx - 1].magical;
        DamageStats result;
        result.melee = damage[last_idx].melee + (idx - last_idx) * delta_melee;
        result.ranged = damage[last_idx].ranged + (idx - last_idx) * delta_ranged;
        result.magical = damage[last_idx].magical + (idx - last_idx) * delta_magical;
        return result;
    }
    
    std::optional<DefenseStats> getDefense(int level) const {
        if (defense.empty()) {
            return std::nullopt;
        }
        int idx = level - 1;
        if (idx < defense.size()) {
            return defense[idx];
        }
        int last_idx = defense.size() - 1;
        for (int i = last_idx; i >= 0 && !defense[i].has_value(); --i) {
            if (i == 0) {
                return std::nullopt;
            }
        }
        auto last = defense[last_idx];
        auto prev = defense[last_idx - 1];
        if (!last.has_value() || !prev.has_value()) {
            return std::nullopt;
        }
        int delta_melee = last->melee - prev->melee;
        int delta_ranged = last->ranged - prev->ranged;
        int delta_magical = last->magical - prev->magical;
        DefenseStats result;
        result.melee = last->melee + (idx - last_idx) * delta_melee;
        result.ranged = last->ranged + (idx - last_idx) * delta_ranged;
        result.magical = last->magical + (idx - last_idx) * delta_magical;
        return result;
    }
    
    double getMovementSpeed(int level) const {
        if (movement_speed.empty()) {
            return 0.0;
        }
        int idx = level - 1;
        if (idx < movement_speed.size()) {
            return movement_speed[idx];
        }
        int last_idx = movement_speed.size() - 1;
        double delta = movement_speed[last_idx] - movement_speed[last_idx - 1];
        return movement_speed[last_idx] + (idx - last_idx) * delta;
    }
    
    CostStats getCosts(int level) const {
        if (costs.empty()) {
            return CostStats{};
        }
        int idx = level - 1;
        if (idx < costs.size()) {
            return costs[idx];
        }
        int last_idx = costs.size() - 1;
        int delta_gold = costs[last_idx].gold - costs[last_idx - 1].gold;
        int delta_grain = costs[last_idx].grain - costs[last_idx - 1].grain;
        int delta_wood = costs[last_idx].wood - costs[last_idx - 1].wood;
        int delta_steel = costs[last_idx].steel - costs[last_idx - 1].steel;
        int delta_bronze = costs[last_idx].bronze - costs[last_idx - 1].bronze;
        int delta_stone = costs[last_idx].stone - costs[last_idx - 1].stone;
        int delta_leather = costs[last_idx].leather - costs[last_idx - 1].leather;
        CostStats result;
        result.gold = costs[last_idx].gold + (idx - last_idx) * delta_gold;
        result.grain = costs[last_idx].grain + (idx - last_idx) * delta_grain;
        result.wood = costs[last_idx].wood + (idx - last_idx) * delta_wood;
        result.steel = costs[last_idx].steel + (idx - last_idx) * delta_steel;
        result.bronze = costs[last_idx].bronze + (idx - last_idx) * delta_bronze;
        result.stone = costs[last_idx].stone + (idx - last_idx) * delta_stone;
        result.leather = costs[last_idx].leather + (idx - last_idx) * delta_leather;
        return result;
    }
};

class CombatantRegistry {
public:
    static CombatantRegistry& getInstance();
    
    bool loadPlayerCombatants(const std::string& config_path);
    bool loadEnemyCombatants(const std::string& config_path);
    bool loadDamageTypes(const std::string& config_path);
    
    std::optional<const Combatant*> getPlayerCombatant(const std::string& id) const;
    std::optional<const Combatant*> getEnemyCombatant(const std::string& id) const;
    const std::vector<std::string>& getDamageTypes() const;
    
    void forEachPlayerCombatant(const std::function<void(const Combatant&)>& callback) const;
    void forEachEnemyCombatant(const std::function<void(const Combatant&)>& callback) const;
    
private:
    CombatantRegistry() = default;
    CombatantRegistry(const CombatantRegistry&) = delete;
    CombatantRegistry& operator=(const CombatantRegistry&) = delete;
    
    std::unordered_map<std::string, Combatant> player_combatants_;
    std::unordered_map<std::string, Combatant> enemy_combatants_;
    std::vector<std::string> damage_types_;
};

} // namespace Combatants