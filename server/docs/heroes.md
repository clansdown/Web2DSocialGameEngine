# Heroes Configuration

Configuration file: `server/config/heroes.json` - Hero definitions with equipment, skills, and status effects

## TypeScript Type Definitions

```typescript
// --- Extrapolated Number Array with Max Cap ---
interface ExtrapolatedArray<T = number> {
    values: T[];      // Explicit values (index = level - 1)
    max?: T | null;   // Optional cap value (null or missing = no cap)
}

// --- Equipment Slot Configuration ---
interface EquipmentSlots {
    slots: number[];  // Array length defines extrapolation behavior
    max: number;      // Cap for slot count
}

// --- Skill Definition ---
interface HeroSkill {
    name: string;
    damage: number[];       // Extrapolated array
    damage_max: number;      // Max cap for damage
    defense: number[];      // Extrapolated array
    defense_max: number;    // Max cap for defense
    healing: number[];      // Extrapolated array
    healing_max: number;    // Max cap for healing
}

// --- Status Effect Definition ---
type StatusEffectType = "stun" | "mute" | "confuse";

interface HeroStatusEffect {
    name: string;
    type: StatusEffectType;
    effect: number[];   // Extrapolated array
    max: number;         // Cap for effect value
}

// --- Hero Definition ---
interface Hero {
    name: string;
    max_level: number;
    equipment: Record<string, EquipmentSlots>;  // Equipment type ID -> slots config
    skills: Record<string, HeroSkill>;          // Skill ID -> skill definition
    status_effects: Record<string, HeroStatusEffect>;  // Effect ID -> effect definition
}

// --- Config file format ---
type HeroesConfig = Record<string, Hero>;  // Hero ID -> Hero definition
```

## Field Descriptions

### Hero Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Display name (e.g., "Sir Galladon") |
| `max_level` | number | Maximum attainable level for extrapolation |
| `equipment` | object | Equipment type configurations |
| `skills` | object | Skill definitions scoped to this hero |
| `status_effects` | object | Status effect definitions |

### Equipment Fields

| Field | Type | Description |
|-------|------|-------------|
| `slots` | number[] | Number of slots per level (extrapolated) |
| `max` | number | Maximum slot count (cap) |

### Skill Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Skill display name |
| `damage` | number[] | Damage output per level (extrapolated) |
| `damage_max` | number | Maximum damage cap |
| `defense` | number[] | Defense bonus per level (extrapolated) |
| `defense_max` | number | Maximum defense cap |
| `healing` | number[] | Healing output per level (extrapolated) |
| `healing_max` | number | Maximum healing cap |

### Status Effect Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Effect display name |
| `type` | string | Effect type: "stun", "mute", or "confuse" |
| `effect` | number[] | Effect value per level (extrapolated) |
| `max` | number | Maximum effect value (cap) |

## Extrapolation Rules

All arrays (`slots`, `damage`, `defense`, `healing`, `effect`) use the same extrapolation logic:

| Array Length | Behavior |
|--------------|----------|
| 0 elements | Default to 0 at all levels |
| 1 element | Linear extrapolation with slope of 1 |
| 2+ elements | Linear extrapolation from last two elements |

### Extrapolation Formula with Max Cap

For arrays at index `i >= length`:

```
value[i] = value[last] + (i - last) * delta
delta = value[last] - value[last-1]

// If max is specified:
value[i] = min(value[i], max_value)
```

### Example: Slots with Max Cap

Given `slots: [1, 2, 3]` with `max: 5` and `max_level: 10`:

| Level | Slots (Raw Extrapolation) | Slots (With Cap) |
|-------|---------------------------|------------------|
| 1 | 1 | 1 |
| 2 | 2 | 2 |
| 3 | 3 | 3 |
| 4 | 4 | 4 |
| 5 | 5 | 5 |
| 6 | 6 | 5 (capped) |
| 7 | 7 | 5 (capped) |
| 8-10 | 8-10 | 5 (capped) |

### Example: Skill with Max Cap

Given `damage: [25, 35, 45]` with `damage_max: 100` and `max_level: 20`:

| Level | Damage (Raw Extrapolation) | Damage (With Cap) |
|-------|----------------------------|-------------------|
| 1 | 25 | 25 |
| 2 | 35 | 35 |
| 3 | 45 | 45 |
| 4 | 55 | 55 |
| 5 | 65 | 65 |
| 6 | 75 | 75 |
| 7 | 85 | 85 |
| 8 | 95 | 95 |
| 9 | 105 | 100 (capped) |
| 10-20 | 115-225 | 100 (capped) |

## Config File Examples

### Knight Hero Example

```json
{
    "knight_hero": {
        "name": "Sir Galladon",
        "max_level": 15,
        "equipment": {
            "sword": {
                "slots": [1, 2, 3],
                "max": 5
            },
            "shield": {
                "slots": [1, 1, 1],
                "max": 2
            }
        },
        "skills": {
            "cleave": {
                "name": "Cleave",
                "damage": [15, 20, 25],
                "damage_max": 50,
                "defense": [0, 0, 0],
                "defense_max": 0,
                "healing": [0, 0, 0],
                "healing_max": 0
            }
        },
        "status_effects": {
            "taunt": {
                "name": "Taunted",
                "type": "stun",
                "effect": [1, 1, 2],
                "max": 3
            }
        }
    }
}
```

### Mage Hero Example

```json
{
    "mage_hero": {
        "name": "Archmage Elara",
        "max_level": 20,
        "equipment": {
            "staff": {
                "slots": [1, 1, 2],
                "max": 4
            }
        },
        "skills": {
            "fireball": {
                "name": "Fireball",
                "damage": [25, 35, 45],
                "damage_max": 100,
                "defense": [0, 0, 0],
                "defense_max": 0,
                "healing": [0, 0, 0],
                "healing_max": 0
            },
            "heal": {
                "name": "Healing Light",
                "damage": [0, 0, 0],
                "damage_max": 0,
                "defense": [0, 0, 0],
                "defense_max": 0,
                "healing": [20, 30, 40],
                "healing_max": 80
            }
        },
        "status_effects": {
            "burning": {
                "name": "Burning",
                "type": "confuse",
                "effect": [8, 10, 12],
                "max": 25
            },
            "silenced": {
                "name": "Silenced",
                "type": "mute",
                "effect": [1, 1, 2],
                "max": 3
            }
        }
    }
}
```

## C++ Data Structures

```cpp
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
```

## Usage Example

```cpp
#include "heroes.hpp"

auto& registry = Heroes::HeroRegistry::getInstance();

registry.loadHeroes("config/heroes.json");

auto knight = registry.getHero("knight_hero");
if (knight) {
    auto& hero = *knight.value();
    
    // Get equipment slots at level 5
    if (hero.equipment.contains("sword")) {
        int slots = hero.equipment.at("sword").getSlots(5);
        std::cout << "Level 5 sword slots: " << slots << std::endl;
    }
    
    // Get skill damage at level 10
    if (hero.skills.contains("cleave")) {
        int damage = hero.skills.at("cleave").getDamage(10);
        std::cout << "Level 10 cleave damage: " << damage << std::endl;
    }
    
    // Get status effect value at level 8
    if (hero.status_effects.contains("taunt")) {
        int effect = hero.status_effects.at("taunt").getEffect(8);
        std::cout << "Level 8 taunt effect: " << effect << std::endl;
    }
}
```

## Image Auto-Detection

Hero images are auto-detected from directory structure:

```
server/images/heroes/{hero_id}/
    idle/1.png, 2.png, ...           # required (non-empty)
    attack/1.png, 2.png, ...          # required (non-empty)
    {skill_id}/
        1.png, 2.png, ...             # icon frames (required, non-empty)
        activate/                     # optional animation subdirectory
            1.png, 2.png, ...         # animation frames (optional, can be empty)
```

- **idle/**: Required - shows hero in idle/standing state
- **attack/**: Required - shows hero attack animations
- **{skill_id}/**: Required per skill - contains skill icon frames
  - Icon files support multiple frames for different UI states (idle, hover, active)
  - At least one frame (1.png) required
- **{skill_id}/activate/**: Optional - skill activation animation frames
  - Empty subdirectory is acceptable (skill may not have animation)

**Filename Convention:**
- Files are named with numeric prefixes: `1.png`, `2.png`, `3.png`, etc.
- Extensions: `.png`, `.jpg`, `.jpeg`, `.gif`, `.svg`, `.webp`
- Server walks this directory at startup to auto-detect available images
- No image filenames need to be specified in config files

**Example Structure:**
```
server/images/heroes/knight_hero/
    idle/1.png, 2.png, 3.png
    attack/1.png, 2.png, 3.png, 4.png
    cleave/1.png, 2.png
        activate/1.png, 2.png, 3.png
    shield_bash/1.png
        activate/1.png
    rally/1.png
```

**Linter Validation:**
The `check_configs.py` tool validates the images directory:
- **Errors:** Empty required directories (idle, attack, skill icon directories)
- **Warnings:** Missing directories, empty optional directories (activate/), orphaned files

## Status Effect Types

| Type | Description |
|------|-------------|
| `stun` | Prevents target from acting |
| `mute` | Prevents target from using magical abilities |
| `confuse` | Causes target to attack random or own team |

## Default Values

If a field is not specified:

| Field | Default |
|-------|---------|
| Any `slots` array element | 0 |
| Any `damage`, `defense`, `healing`, `effect` array element | 0 |
| `max` for stats | 0 (no cap) |
| `max` for equipment slots | 0 (no slots possible) |
| Empty arrays | 0 at all levels |

## Level 0 Clarification

Level 0 represents a hero that has not yet been created or is in an invalid state. Hero stats should always be requested for level >= 1.

## Lenient Parsing

All config files use lenient parsing:

```cpp
nlohmann::json::parse(content, nullptr, true, true);
//                                      ^           ^    ^    ^
//                                      content   cb  allow  ignore
//                                                except comments
```

This allows:
- C-style comments: `// comment` or `/* comment */`
- Trailing commas in arrays and objects
- Relaxed JSON syntax