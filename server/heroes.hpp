#ifndef HEROES_HPP
#define HEROES_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace Heroes {

enum class StatusEffectType {
    Stun,
    Mute,
    Confuse
};

struct EquipmentSlots {
    std::vector<int> slots;
    int max = 0;
    
    int getSlots(int level) const;
};

struct HeroSkill {
    std::string name;
    std::vector<int> damage;
    int damage_max = 0;
    std::vector<int> defense;
    int defense_max = 0;
    std::vector<int> healing;
    int healing_max = 0;
    
    int getDamage(int level) const;
    int getDefense(int level) const;
    int getHealing(int level) const;
};

struct HeroStatusEffect {
    std::string name;
    StatusEffectType type;
    std::vector<int> effect;
    int max = 0;
    
    int getEffect(int level) const;
};

struct Hero {
    std::string id;
    std::string name;
    int max_level = 1;

    std::vector<double> morale_boost;  // Optional: morale boost per level
    std::unordered_map<std::string, EquipmentSlots> equipment;
    std::unordered_map<std::string, HeroSkill> skills;
    std::unordered_map<std::string, HeroStatusEffect> status_effects;
};

class HeroRegistry {
public:
    static HeroRegistry& getInstance();
    
    bool loadHeroes(const std::string& config_path);
    
    std::optional<const Hero*> getHero(const std::string& id) const;
    const std::unordered_map<std::string, Hero>& getAllHeroes() const;

private:
    std::unordered_map<std::string, Hero> heroes_;
};

} // namespace Heroes

#endif // HEROES_HPP