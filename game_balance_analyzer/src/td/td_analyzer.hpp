#pragma once

#include "config_loader.hpp"
#include "report.hpp"

// Static balance analysis of the tower-defense minigame from towers.json,
// units.json, mobs.json, and wave_templates.json.
//
// Produces findings about:
//   - Tower / unit damage-per-gold and DPS efficiency.
//   - Upgrade cost efficiency.
//   - Mob HP / reward scaling vs speed.
//   - Wave-template difficulty scaling across difficulties.
class td_analyzer {
public:
    explicit td_analyzer(const config_loader& loader);

    balance_report analyze() const;

private:
    const config_loader& loader_;
};
