#pragma once

#include "config_loader.hpp"
#include "report.hpp"

// Static balance analysis of the realtime-strategy combat system from
// player_combatants.json, enemy_combatants.json, and heroes.json.
//
// Produces findings about:
//   - Combatant cost-vs-power curves (damage/defense/speed per cost & upkeep).
//   - Hero skill damage/healing/defense value.
//   - Damage-type coverage (melee/ranged/magical) across the roster.
class rts_analyzer {
public:
    explicit rts_analyzer(const config_loader& loader);

    balance_report analyze() const;

private:
    const config_loader& loader_;
};
