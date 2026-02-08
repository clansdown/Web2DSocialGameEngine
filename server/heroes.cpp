#include "heroes.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace Heroes {

static std::string readFileToString(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

static int extrapolateWithMax(const std::vector<int>& array, int level, int maxVal) {
    if (array.empty()) {
        return maxVal > 0 ? std::min(0, maxVal) : 0;
    }
    
    if (level - 1 < static_cast<int>(array.size())) {
        return array[level - 1];
    }
    
    int lastIdx = array.size() - 1;
    int secondLastIdx = std::max(0, lastIdx - 1);
    int delta = array[lastIdx] - array[secondLastIdx];
    
    int extrapolated = array[lastIdx] + (level - static_cast<int>(array.size())) * delta;
    
    if (maxVal > 0) {
        return std::min(extrapolated, maxVal);
    }
    
    return extrapolated;
}

int EquipmentSlots::getSlots(int level) const {
    return extrapolateWithMax(slots, level, max);
}

int HeroSkill::getDamage(int level) const {
    return extrapolateWithMax(damage, level, damage_max);
}

int HeroSkill::getDefense(int level) const {
    return extrapolateWithMax(defense, level, defense_max);
}

int HeroSkill::getHealing(int level) const {
    return extrapolateWithMax(healing, level, healing_max);
}

int HeroStatusEffect::getEffect(int level) const {
    return extrapolateWithMax(effect, level, max);
}

static StatusEffectType parseStatusEffectType(const std::string& typeStr) {
    if (typeStr == "stun") return StatusEffectType::Stun;
    if (typeStr == "mute") return StatusEffectType::Mute;
    if (typeStr == "confuse") return StatusEffectType::Confuse;
    return StatusEffectType::Stun;
}

HeroRegistry& HeroRegistry::getInstance() {
    static HeroRegistry instance;
    return instance;
}

bool HeroRegistry::loadHeroes(const std::string& config_path) {
    std::string content = readFileToString(config_path);
    if (content.empty()) {
        std::cerr << "Failed to open heroes config: " << config_path << std::endl;
        return false;
    }
    
    try {
        nlohmann::json data = nlohmann::json::parse(content, nullptr, true, true);
        
        for (auto& [heroId, heroJson] : data.items()) {
            Hero hero;
            hero.id = heroId;
            hero.name = heroJson["name"].get<std::string>();
            hero.max_level = heroJson["max_level"].get<int>();
            
            if (heroJson.contains("equipment")) {
                for (auto& [equipType, equipJson] : heroJson["equipment"].items()) {
                    EquipmentSlots equip;
                    if (equipJson.contains("slots")) {
                        for (auto& slotVal : equipJson["slots"]) {
                            equip.slots.push_back(slotVal.get<int>());
                        }
                    }
                    if (equipJson.contains("max")) {
                        equip.max = equipJson["max"].get<int>();
                    }
                    hero.equipment[equipType] = std::move(equip);
                }
            }
            
            if (heroJson.contains("skills")) {
                for (auto& [skillId, skillJson] : heroJson["skills"].items()) {
                    HeroSkill skill;
                    skill.name = skillJson["name"].get<std::string>();
                    
                    if (skillJson.contains("damage")) {
                        for (auto& dmgVal : skillJson["damage"]) {
                            skill.damage.push_back(dmgVal.get<int>());
                        }
                    }
                    if (skillJson.contains("damage_max")) {
                        skill.damage_max = skillJson["damage_max"].get<int>();
                    }
                    
                    if (skillJson.contains("defense")) {
                        for (auto& defVal : skillJson["defense"]) {
                            skill.defense.push_back(defVal.get<int>());
                        }
                    }
                    if (skillJson.contains("defense_max")) {
                        skill.defense_max = skillJson["defense_max"].get<int>();
                    }
                    
                    if (skillJson.contains("healing")) {
                        for (auto& healVal : skillJson["healing"]) {
                            skill.healing.push_back(healVal.get<int>());
                        }
                    }
                    if (skillJson.contains("healing_max")) {
                        skill.healing_max = skillJson["healing_max"].get<int>();
                    }
                    
                    hero.skills[skillId] = std::move(skill);
                }
            }
            
            if (heroJson.contains("status_effects")) {
                for (auto& [effectId, effectJson] : heroJson["status_effects"].items()) {
                    HeroStatusEffect effect;
                    effect.name = effectJson["name"].get<std::string>();
                    
                    if (effectJson.contains("type")) {
                        effect.type = parseStatusEffectType(effectJson["type"].get<std::string>());
                    }
                    
                    if (effectJson.contains("effect")) {
                        for (auto& effVal : effectJson["effect"]) {
                            effect.effect.push_back(effVal.get<int>());
                        }
                    }
                    
                    if (effectJson.contains("max")) {
                        effect.max = effectJson["max"].get<int>();
                    }
                    
                    hero.status_effects[effectId] = std::move(effect);
                }
            }
            
            heroes_[heroId] = std::move(hero);
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse heroes: " << e.what() << std::endl;
        return false;
    }
}

std::optional<const Hero*> HeroRegistry::getHero(const std::string& id) const {
    auto it = heroes_.find(id);
    if (it != heroes_.end()) {
        return std::optional<const Hero*>(&it->second);
    }
    return std::nullopt;
}

const std::unordered_map<std::string, Hero>& HeroRegistry::getAllHeroes() const {
    return heroes_;
}

} // namespace Heroes