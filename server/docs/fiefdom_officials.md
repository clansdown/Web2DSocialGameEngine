# Fiefdom Officials Configuration

Configuration file: `server/config/fiefdom_officials.json` - Official template definitions for fiefdom appointments

## Overview

Fiefdom officials are individual characters (Henry, Elrond, etc.) that can be appointed to serve fiefdoms. Each official:
- Has a unique name and backstory
- Is eligible for specific official roles
- Has stats that scale with level using extrapolation
- Has a portrait for UI display
- Can be appointed to multiple fiefdoms (each fiefdom gets an independent copy)

## TypeScript Type Definitions

```typescript
// --- Stat Array with Max Cap ---
interface StatArray {
    values: number[];       // Explicit values (index = level - 1)
    max: number;           // Maximum cap value
}

// --- Fiefdom Official Template ---
interface FiefdomOfficial {
    name: string;                       // Display name (e.g., "Henry")
    max_level: number;                  // Maximum attainable level
    roles: string[];                    // Eligible roles (subset of 8 official roles)
    stats: {
        intelligence: number[];         // Stat values per level (extrapolated)
        intelligence_max: number;       // Max cap for intelligence
        charisma: number[];
        charisma_max: number;
        wisdom: number[];
        wisdom_max: number;
        diligence: number[];
        diligence_max: number;
    };
    portrait_id: number;                // Portrait image ID (references images/portraits/)
    description?: string;               // Optional backstory/flavor text
}

// --- Config file format ---
type FiefdomOfficialsConfig = Record<string, FiefdomOfficial>;
```

## Field Descriptions

### Official Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Display name (e.g., "Henry", "Elrond") |
| `max_level` | number | Maximum attainable level for extrapolation |
| `roles` | array | List of eligible roles this official can fill |
| `stats` | object | Four stat arrays with max caps |
| `portrait_id` | number | Portrait image ID for UI display |
| `description` | string | Optional backstory |

### Stat Fields

| Field | Type | Description |
|-------|------|-------------|
| `intelligence` | number[] | Intelligence per level (extrapolated) |
| `intelligence_max` | number | Maximum cap for intelligence |
| `charisma` | number[] | Charisma per level (extrapolated) |
| `charisma_max` | number | Maximum cap for charisma |
| `wisdom` | number[] | Wisdom per level (extrapolated) |
| `wisdom_max` | number | Maximum cap for wisdom |
| `diligence` | number[] | Diligence per level (extrapolated) |
| `diligence_max` | number | Maximum cap for diligence |

## Official Roles

| Role | Purpose | Best Stat Synergy |
|------|---------|-------------------|
| `bailiff` | Manages fiefdom administration and justice | Charisma, Wisdom |
| `wizard` | Handles magical research and mana resources | Intelligence, Wisdom |
| `architect` | Plans construction and improvements | Intelligence, Diligence |
| `steward` | Manages treasury and resources | Wisdom, Diligence |
| `reeve` | Oversees local affairs and peasant welfare | Charisma, Diligence |
| `beadle` | Religious and ceremonial duties | Wisdom, Charisma |
| `constable` | Maintains order and defense | Diligence, Intelligence |
| `forester` | Manages wood and wilderness resources | Diligence, Wisdom |

## Role-Stat Synergies

Each role benefits from certain stats more than others:

- **Bailiff**: Justice resolution quality → Charisma, moral judgment → Wisdom
- **Wizard**: Research speed → Intelligence, spell power → Wisdom
- **Architect**: Planning quality → Intelligence, oversight → Diligence
- **Steward**: Resource allocation → Wisdom, auditing → Diligence
- **Reeve**: Peasant relations → Charisma, welfare → Diligence
- **Beadle**: Ceremonies → Wisdom, community → Charisma
- **Constable**: Vigilance → Diligence, investigation → Intelligence
- **Forester**: Patrols → Diligence, nature knowledge → Wisdom

## Extrapolation Rules

Stat arrays use the same extrapolation logic as heroes:

| Array Length | Behavior |
|--------------|----------|
| 0 elements | Default to 0 at all levels |
| 1 element | Linear extrapolation with slope of 1 |
| 2+ elements | Linear extrapolation from last two elements |

### Extrapolation Formula with Max Cap

For stat arrays at index `i >= length`:

```
value[i] = value[last] + (i - last) * delta
delta = value[last] - value[last-1]

// Cap at max:
value[i] = min(value[i], max_value)
```

### Example

Given `intelligence: [80, 85, 90]` with `intelligence_max: 150` and `max_level: 15`:

| Level | Intelligence (Raw) | Intelligence (With Cap) |
|-------|-------------------|--------------------------|
| 1 | 80 | 80 |
| 2 | 85 | 85 |
| 3 | 90 | 90 |
| 4 | 95 | 95 |
| 5 | 100 | 100 |
| 6 | 105 | 105 |
| 7 | 110 | 110 |
| 8 | 115 | 115 |
| 9 | 120 | 120 |
| 10 | 125 | 125 |
| 11 | 130 | 130 |
| 12 | 135 | 135 |
| 13 | 140 | 140 |
| 14 | 145 | 145 |
| 15 | 150 | 150 (capped) |

## Portrait Requirements

Each official requires a portrait image for UI display:

```
server/images/portraits/{portrait_id}/
    1.png         # Main portrait (required, non-empty)
    2.png, ...    # Additional frames (optional)
```

- Portrait IDs are integer values matching `portrait_id` in config
- At minimum, each portrait directory needs `1.png`
- Images use standard naming: `1.png`, `2.png`, `3.png`, etc.
- Supported extensions: `.png`, `.jpg`, `.jpeg`, `.gif`, `.svg`, `.webp`

The server auto-detects portraits at startup from `server/images/portraits/` directory.

## Config File Example

```json
{
    "henry_wise_steward": {
        "name": "Henry",
        "max_level": 15,
        "roles": ["steward", "reeve"],
        "stats": {
            "intelligence": [80, 85, 90],
            "intelligence_max": 150,
            "charisma": [60, 65, 70],
            "charisma_max": 100,
            "wisdom": [90, 95, 100],
            "wisdom_max": 160,
            "diligence": [75, 80, 85],
            "diligence_max": 140
        },
        "portrait_id": 100,
        "description": "A prudent administrator with decades of experience managing large estates."
    },
    "elrond_wizard_master": {
        "name": "Elrond",
        "max_level": 20,
        "roles": ["wizard", "architect"],
        "stats": {
            "intelligence": [95, 100, 105],
            "intelligence_max": 180,
            "charisma": [70, 75, 80],
            "charisma_max": 130,
            "wisdom": [100, 105, 110],
            "wisdom_max": 190,
            "diligence": [65, 70, 75],
            "diligence_max": 120
        },
        "portrait_id": 101,
        "description": "A master of arcane arts, equally skilled in magical research and strategic planning."
    }
}
```

## C++ Data Structures

```cpp
namespace Officials {

struct StatArray {
    std::vector<int> values;
    int max = 0;

    int getValue(int level) const;
};

struct OfficialTemplate {
    std::string id;
    std::string name;
    int max_level = 1;
    std::vector<std::string> eligibleRoles;

    StatArray intelligence;
    StatArray charisma;
    StatArray wisdom;
    StatArray diligence;

    int portrait_id = 0;
    std::string description;

    int getIntelligence(int level) const;
    int getCharisma(int level) const;
    int getWisdom(int level) const;
    int getDiligence(int level) const;
};

class OfficialRegistry {
public:
    static OfficialRegistry& getInstance();

    bool loadOfficials(const std::string& config_path);

    std::optional<const OfficialTemplate*> getOfficial(const std::string& id) const;
    const std::unordered_map<std::string, OfficialTemplate>& getAllOfficials() const;

    std::vector<const OfficialTemplate*> getEligibleOfficialsForRole(const std::string& role) const;
    std::vector<const OfficialTemplate*> getEligibleOfficialsForRoles(const std::vector<std::string>& roles) const;

private:
    std::unordered_map<std::string, OfficialTemplate> officials_;
};

} // namespace Officials
```

## Usage Example

```cpp
#include "fiefdom_officials.hpp"

auto& registry = Officials::OfficialRegistry::getInstance();

registry.loadOfficials("config/fiefdom_officials.json");

// Get a specific official
auto henry = registry.getOfficial("henry_wise_steward");
if (henry) {
    auto& official = *henry.value();
    std::cout << "Henry's wisdom at level 10: " << official.getWisdom(10) << std::endl;
}

// Find officials eligible for steward role
auto stewards = registry.getEligibleOfficialsForRole("steward");
for (const auto& official : stewards) {
    std::cout << "Steward candidate: " << official->name
              << " (diligence: " << official->getDiligence(1) << " at level 1)" << std::endl;
}
```

## Appointment Model

Officials follow an independent copies model:

1. **Config**: Defines official templates (Henry with base stats)
2. **Appointment**: Fiefdom creates an independent instance from the template
3. **Independence**: Each fiefdom's copy levels up independently
4. **Limits**: Maximum one official per role per fiefdom

```cpp
// Creating an independent instance for a fiefdom
int fiefdom_id = 5;
auto official = registry.getOfficial("henry_wise_steward");

// Copy initial stats to database instance
OfficialInstance instance;
instance.name = official->name;
instance.level = 1;
instance.portrait_id = official->portrait_id;
instance.intelligence = official->getIntelligence(1);
instance.charisma = official->getCharisma(1);
instance.wisdom = official->getWisdom(1);
instance.diligence = official->getDiligence(1);

// Fiefdom A and Fiefdom B both appoint Henry
// They are completely independent instances
```

## Default Values

If a field is not specified:

| Field | Default |
|-------|---------|
| `max_level` | 1 |
| `roles` | [] (empty - official cannot be appointed to any role) |
| Any stat array element | 0 |
| `max` for stats | 0 (no progression beyond explicit values) |
| `portrait_id` | 0 |
| `description` | "" (empty) |

## Level 0 Clarification

Level 0 represents an official that has not yet been appointed or is in an invalid state. Official stats should always be requested for level >= 1.

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

## Linter Validation

The `check_configs.py` tool validates fiefdom officials:

- **Required fields**: `name`, `max_level`, `roles` (at least one), all 4 stat arrays with max caps, `portrait_id`
- **Valid roles**: Must be subset of 8 official roles
- **Stat ranges**: Values must be 0-255
- **Portrait validation**: Corresponding `portraits/{portrait_id}/` directory must exist with at least one image
- **Duplicate IDs**: Detected and reported

Run validation:
```bash
./tools/check_configs.py              # Show errors and warnings
./tools/check_configs.py --no-warnings  # Errors only
```