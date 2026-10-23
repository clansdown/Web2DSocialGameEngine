#pragma once
#include "CombatTypes.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace combat {

// Default hard-body radius (normalized) used only when a combatant config omits
// `size` (the linter makes `size` required, so this is a crash guard). The real
// per-unit size comes from the class `size` field → stats.radius.
inline constexpr double k_default_unit_radius = 0.03;

// Resolve the per-level stats of one combatant class from a combatant config
// (player or enemy). This is the SINGLE stat-resolution path shared by the
// simulation (unit spawn, CombatMatch) and the strength formula
// (RetinueStrength), so the two can never drift — including the level-indexed
// extrapolation conventions (rts_design.md §5.1) and the `dark_mage` level-1
// null-defense edge case.
combat_unit_stats resolve_combatant_stats(const nlohmann::json& combatants,
                                          const std::string& class_id, int level);

// Highest authored level for a combatant class (`max_level`, clamped to a sane
// range; 1 when the class is missing). Warband assembly scans levels 1..this.
int combatant_max_level(const nlohmann::json& combatants, const std::string& class_id);

} // namespace combat
