#include "td/td_analyzer.hpp"
#include "fmt.hpp"

#include <algorithm>

using json = nlohmann::json;

td_analyzer::td_analyzer(const config_loader& loader) : loader_(loader) {}

namespace {

// Computes average DPS for a tower/unit at its base level.
// damage is an array of per-shot damage values; attack_rate is shots/second.
double base_dps(const json& entry) {
    if (!entry.contains("damage") || !entry["damage"].is_array() || entry["damage"].empty()) {
        return 0.0;
    }
    double dmg = entry["damage"][0].get<double>();
    double rate = entry.value("attack_rate", 1.0);
    return dmg * rate;
}

double base_cost_gold(const json& entry) {
    if (!entry.contains("cost") || !entry["cost"].is_array() || entry["cost"].empty()) {
        return 0.0;
    }
    return entry["cost"][0].value("gold", 0.0);
}

std::string fmt(double v) { return nfmt::format_number(v); }

} // namespace

balance_report td_analyzer::analyze() const {
    balance_report report;

    auto towers = loader_.td_towers().value("towers", json::object());
    auto units = loader_.td_units().value("units", json::object());

    // 1) Towers: DPS per gold.
    struct dps_eff {
        std::string id;
        double dps;
        double cost;
        double ratio;
    };
    std::vector<dps_eff> tower_effs;
    for (auto it = towers.begin(); it != towers.end(); ++it) {
        const json& e = it.value();
        double dps = base_dps(e);
        double cost = base_cost_gold(e);
        tower_effs.push_back({it.key(), dps, cost, cost > 0 ? dps / cost : 0.0});
    }
    std::stable_sort(tower_effs.begin(), tower_effs.end(),
                     [](const dps_eff& a, const dps_eff& b) { return a.ratio > b.ratio; });
    for (const auto& t : tower_effs) {
        report.add_info("Tower " + t.id,
                        "dps=" + fmt(t.dps) + " cost=" + fmt(t.cost)
                            + " dps_per_gold=" + fmt(t.ratio),
                        t.id);
    }
    // Flag the worst tower relative to the best (balance check).
    if (tower_effs.size() >= 2) {
        double best = tower_effs.front().ratio;
        double worst = tower_effs.back().ratio;
        if (best > 0.0 && worst > 0.0 && worst < best * 0.5) {
            report.add_warning(
                "Tower DPS efficiency spread too large",
                "Best tower '" + tower_effs.front().id + "' has " + fmt(best)
                    + " dps/gold; worst '" + tower_effs.back().id + "' has " + fmt(worst)
                    + " (<50% of best).",
                tower_effs.back().id);
        }
    }

    // 2) Units: DPS per gold.
    std::vector<dps_eff> unit_effs;
    for (auto it = units.begin(); it != units.end(); ++it) {
        const json& e = it.value();
        double dps = base_dps(e);
        double cost = base_cost_gold(e);
        unit_effs.push_back({it.key(), dps, cost, cost > 0 ? dps / cost : 0.0});
    }
    std::stable_sort(unit_effs.begin(), unit_effs.end(),
                     [](const dps_eff& a, const dps_eff& b) { return a.ratio > b.ratio; });
    for (const auto& u : unit_effs) {
        report.add_info("Unit " + u.id,
                        "dps=" + fmt(u.dps) + " cost=" + fmt(u.cost)
                            + " dps_per_gold=" + fmt(u.ratio),
                        u.id);
    }

    // 3) Mobs: HP/reward scaling.
    auto mobs = loader_.td_mobs().value("mobs", json::object());
    for (auto it = mobs.begin(); it != mobs.end(); ++it) {
        const json& e = it.value();
        double hp = e.value("hp", 0.0);
        double speed = e.value("speed", 0.0);
        double reward = e.value("reward_gold", 0.0);
        double hp_per_reward = reward > 0.0 ? hp / reward : 0.0;
        report.add_info("Mob " + it.key(),
                        "hp=" + fmt(hp) + " speed=" + fmt(speed) + " reward=" + fmt(reward)
                            + " hp_per_reward=" + fmt(hp_per_reward),
                        it.key());
    }

    // 4) Wave-template difficulty scaling: total HP per difficulty level.
    auto waves = loader_.td_waves();
    if (waves.contains("difficulty_templates") && waves["difficulty_templates"].is_object()) {
        for (auto it = waves["difficulty_templates"].begin();
             it != waves["difficulty_templates"].end(); ++it) {
            const json& tpl = it.value();
            if (!tpl.contains("rounds") || !tpl["rounds"].is_array()) {
                continue;
            }
            double total_hp = 0.0;
            size_t groups = 0;
            for (const auto& round : tpl["rounds"]) {
                if (!round.is_array()) {
                    continue;
                }
                for (const auto& group : round) {
                    if (!group.is_object() || !group.contains("mobs") || !group.contains("count")) {
                        continue;
                    }
                    double count = (group["count"].value("min", 0.0) + group["count"].value("max", 0.0)) / 2.0;
                    for (const auto& mob_id : group["mobs"]) {
                        std::string id = mob_id.get<std::string>();
                        const json& mob = mobs.value(id, json::object());
                        total_hp += count * mob.value("hp", 0.0);
                    }
                    ++groups;
                }
            }
            report.add_info("Difficulty " + it.key() + " wave template",
                            "total_hp=" + fmt(total_hp) + " groups=" + nfmt::format_int(groups)
                                + " avg_hp/group=" + fmt(groups ? total_hp / groups : 0.0),
                            it.key());
        }
    }

    return report;
}
