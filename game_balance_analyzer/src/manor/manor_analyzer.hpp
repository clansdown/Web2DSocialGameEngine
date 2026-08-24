#pragma once

#include "config_loader.hpp"
#include "report.hpp"
#include "manor/manor_economy.hpp"
#include "manor/manor_model.hpp"

// Static balance analysis of the manor economy from fiefdom_building_types.json
// and economy.json.
//
// Produces findings about:
//   - Building cost-vs-output efficiency (gold-normalized).
//   - Stage-chain efficiency (whether higher stages justify their cost).
//   - Import/export pricing sanity (penny vs gold markets, sell multipliers).
//   - Dependency / prerequisite reachability (chain-aware).
//   - Manor-level gating sanity.
class manor_analyzer {
public:
    explicit manor_analyzer(const config_loader& loader);

    balance_report analyze() const;

private:
    const config_loader& loader_;
    building_registry registry_;
    manor_economy economy_;
};
