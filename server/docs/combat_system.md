# Combat System Configuration

Configuration files:
- `server/config/damage_types.json` - List of damage types
- `server/config/player_combatants.json` - Player combatant definitions
- `server/config/enemy_combatants.json` - Enemy combatant definitions

All config files use lenient JSON parsing (allows comments, trailing commas) via nlohmann/json with `ignore_comments=true`.

## TypeScript Type Definitions

```typescript
// --- Damage Stats per level ---
interface DamageStats {
    melee: number;
    ranged: number;
    magical: number;
}

// --- Defense Stats per level ---
interface DefenseStats {
    melee: number;
    ranged: number;
    magical: number;
}

// --- Cost Stats per level ---
interface CostStats {
    gold: number;
    grain: number;
    wood: number;
    steel: number;
    bronze: number;
    stone: number;
    leather: number;
}

// --- Combatant Definition ---
interface Combatant {
    // Required fields
    name: string;
    max_level: number;

    // Arrays (index = level - 1, levels start at 1)
    // Empty array = 0 at all levels
    // 1 element = slope of 1 extrapolation
    // 2+ elements = linear extrapolation from last two
    damage: DamageStats[];
    defense: (DefenseStats | null)[];  // null = no defense at that level
    movement_speed: number[];
    costs: CostStats[];
}

// --- Config file format ---
type CombatantConfig = Record<string, Combatant>;  // Combatant ID -> Combatant
```

## Field Descriptions

### Combatant Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Display name (e.g., "Spearman", "Archer") |
| `max_level` | number | Maximum attainable level |
| `damage` | DamageStats[] | Damage output per level |
| `defense` | (DefenseStats \| null)[] | Damage reduction per level (null = no defense) |
| `movement_speed` | number[] | Movement speed per level |
| `costs` | CostStats[] | Resource costs per level |

### Damage Stats

| Field | Type | Description |
|-------|------|-------------|
| `melee` | number | Melee damage amount |
| `ranged` | number | Ranged damage amount |
| `magical` | number | Magical damage amount |

### Defense Stats

| Field | Type | Description |
|-------|------|-------------|
| `melee` | number | Melee damage reduction |
| `ranged` | number | Ranged damage reduction |
| `magical` | number | Magical damage reduction |

### Cost Stats

| Field | Type | Description |
|-------|------|-------------|
| `gold` | number | Gold cost |
| `grain` | number | Grain cost |
| `wood` | number | Wood cost |
| `steel` | number | Steel cost |
| `bronze` | number | Bronze cost |
| `stone` | number | Stone cost |
| `leather` | number | Leather cost |

## Extrapolation Rules

All arrays (`damage`, `defense`, `movement_speed`, `costs`) use the same extrapolation logic:

| Array Length | Behavior |
|--------------|----------|
| 0 elements | Default to 0 (or empty object) at all levels |
| 1 element | Linear extrapolation with slope of 1 |
| 2+ elements | Linear extrapolation from last two elements |

### Extrapolation Formula

For arrays with 2+ elements at index `i >= (length - 2)`:

```
value[i] = value[last] + (i - last) * delta
delta = value[last] - value[last-1]
```

### Example

Given `damage: [{ melee: 10 }, { melee: 15 }, { melee: 20 }]` with `max_level: 5`:

| Level | Melee Damage |
|-------|-------------|
| 1 | 10 |
| 2 | 15 |
| 3 | 20 |
| 4 | 25 |
| 5 | 30 |

## Config File Examples

### Damage Types

```json
[
    "melee",
    "ranged",
    "magical"
]
```

### Player Combatant Example

```json
{
    "spearman": {
        "name": "Spearman",
        "max_level": 10,
        "damage": [
            { "melee": 10, "ranged": 5, "magical": 0 },
            { "melee": 15, "ranged": 7, "magical": 0 },
            { "melee": 20, "ranged": 10, "magical": 0 }
        ],
        "defense": [
            { "melee": 5, "ranged": 3, "magical": 0 },
            null,
            { "melee": 10, "ranged": 5, "magical": 2 }
        ],
        "movement_speed": [1.0, 1.1, 1.2],
        "costs": [
            { "gold": 50, "steel": 10 },
            { "gold": 75, "steel": 15 },
            { "gold": 100, "steel": 20 }
        ]
    }
}
```

### Enemy Combatant Example

```json
{
    "goblin": {
        "name": "Goblin",
        "max_level": 5,
        "damage": [
            { "melee": 8, "ranged": 0, "magical": 0 },
            { "melee": 12, "ranged": 0, "magical": 0 }
        ],
        "defense": [
            { "melee": 2, "ranged": 1, "magical": 0 }
        ],
        "movement_speed": [1.5],
        "costs": []
    }
}
```

## C++ Data Structures

```cpp
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
    
    DamageStats getDamage(int level) const;
    std::optional<DefenseStats> getDefense(int level) const;
    double getMovementSpeed(int level) const;
    CostStats getCosts(int level) const;
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
};

} // namespace Combatants
```

## Usage Example

```cpp
#include "combatants.hpp"

auto& registry = Combatants::CombatantRegistry::getInstance();

registry.loadDamageTypes("config/damage_types.json");
registry.loadPlayerCombatants("config/player_combatants.json");
registry.loadEnemyCombatants("config/enemy_combatants.json");

auto spearman = registry.getPlayerCombatant("spearman");
if (spearman) {
    auto damage = spearman.value()->getDamage(3);
    std::cout << "Level 3 spearman damage: " 
              << damage.melee << " melee, "
              << damage.ranged << " ranged, "
              << damage.magical << " magical" << std::endl;
}

for (const auto& type : registry.getDamageTypes()) {
    std::cout << "Damage type: " << type << std::endl;
}
```

## Image Directory Structure

Combatant images use a directory structure based on combatant ID:

```
assets/combatants/{combatant_id}/
    idle/
        frame_1.png
        frame_2.png
        ...
    attack/
        frame_1.png
        frame_2.png
        ...
    defend/
        frame_1.png
        frame_2.png
        ...
    die/
        frame_1.png
        frame_2.png
        ...
```

The base directory name `{combatant_id}` corresponds to the combatant ID in the JSON config files.

## Default Values

If a field is not specified:

| Field | Default |
|-------|---------|
| Any damage field | 0 |
| Any defense field | 0 |
| Any cost field | 0 |
| `defense` array element | null (no defense at that level) |
| Empty `damage`, `defense`, `movement_speed`, `costs` arrays | 0 at all levels |

## Level 0 Clarification

Level 0 represents a combatant that has not yet been created or is in an invalid state. Combatant stats should always be requested for level >= 1.

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