#pragma once

#include "config_loader.hpp"
#include "report.hpp"

// Static balance analysis of the weeding minigame from plants.json and
// tools.json.
//
// Produces findings about:
//   - Plant HP vs per-tool damage (clears per action / efficiency).
//   - Tool effectiveness across the plant roster.
//   - Spread-probability risk.
class weeding_analyzer {
public:
    explicit weeding_analyzer(const config_loader& loader);

    balance_report analyze() const;

private:
    const config_loader& loader_;
};
