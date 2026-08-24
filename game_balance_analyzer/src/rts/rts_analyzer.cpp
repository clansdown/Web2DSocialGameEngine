#include "rts/rts_analyzer.hpp"
#include "fmt.hpp"

#include <algorithm>

using json = nlohmann::json;

rts_analyzer::rts_analyzer(const config_loader& loader) : loader_(loader) {}

namespace {

// Sums a damage entry {melee, ranged, magical} at the given array index.
double sum_stats(const json& entry, size_t idx) {
    if (!entry.is_array() || entry.empty()) {
        return 0.0;
    }
    size_t i = std::min(idx, entry.size() - 1);
    const json& e = entry[i];
    if (!e.is_object()) {
        return 0.0;
    }
    return e.value("melee", 0.0) + e.value("ranged", 0.0) + e.value("magical", 0.0);
}

double sum_cost(const json& costs, size_t idx) {
    if (!costs.is_array() || costs.empty()) {
        return 0.0;
    }
    size_t i = std::min(idx, costs.size() - 1);
    const json& c = costs[i];
    if (!c.is_object()) {
        return 0.0;
    }
    double total = 0.0;
    for (auto it = c.begin(); it != c.end(); ++it) {
        if (it.value().is_number()) {
            total += it.value().get<double>();
        }
    }
    return total;
}

std::string fmt(double v) { return nfmt::format_number(v); }

} // namespace

balance_report rts_analyzer::analyze() const {
    balance_report report;

    auto players = loader_.player_combatants();
    auto enemies = loader_.enemy_combatants();
    auto heroes = loader_.heroes();

    // 1) Player combatants: level-1 damage/cost and upkeep efficiency.
    struct unit_eff {
        std::string id;
        double damage;
        double cost;
        double upkeep;
        double dmg_per_cost;
        double dmg_per_upkeep;
    };
    std::vector<unit_eff> effs;
    for (auto it = players.begin(); it != players.end(); ++it) {
        const json& e = it.value();
        double dmg = sum_stats(e.value("damage", json::array()), 0);
        double cost = sum_cost(e.value("costs", json::array()), 0);
        double upkeep = sum_cost(e.value("upkeep", json::array()), 0);
        effs.push_back({it.key(), dmg, cost, upkeep,
                        cost > 0 ? dmg / cost : 0.0,
                        upkeep > 0 ? dmg / upkeep : 0.0});
    }
    std::stable_sort(effs.begin(), effs.end(),
                     [](const unit_eff& a, const unit_eff& b) { return a.dmg_per_cost > b.dmg_per_cost; });
    for (const auto& u : effs) {
        report.add_info("Player combatant " + u.id,
                        "dmg=" + fmt(u.damage) + " cost=" + fmt(u.cost)
                            + " upkeep=" + fmt(u.upkeep)
                            + " dmg/cost=" + fmt(u.dmg_per_cost)
                            + " dmg/upkeep=" + fmt(u.dmg_per_upkeep),
                        u.id);
    }
    if (effs.size() >= 2) {
        double best = effs.front().dmg_per_cost;
        double worst = effs.back().dmg_per_cost;
        if (best > 0.0 && worst > 0.0 && worst < best * 0.5) {
            report.add_warning(
                "Player unit cost-efficiency spread",
                "Best '" + effs.front().id + "' " + fmt(best) + " dmg/cost vs worst '"
                    + effs.back().id + "' " + fmt(worst) + " (<50% of best).",
                effs.back().id);
        }
    }

    // 2) Enemy combatants: raw power (for PvE difficulty calibration).
    for (auto it = enemies.begin(); it != enemies.end(); ++it) {
        const json& e = it.value();
        double dmg = sum_stats(e.value("damage", json::array()), 0);
        double def = sum_stats(e.value("defense", json::array()), 0);
        double speed = 0.0;
        if (e.contains("movement_speed") && e["movement_speed"].is_array() && !e["movement_speed"].empty()) {
            speed = e["movement_speed"][0].get<double>();
        }
        report.add_info("Enemy combatant " + it.key(),
                        "dmg=" + fmt(dmg) + " def=" + fmt(def) + " speed=" + fmt(speed),
                        it.key());
    }

    // 3) Heroes: skill damage/healing value.
    for (auto it = heroes.begin(); it != heroes.end(); ++it) {
        const json& h = it.value();
        double total_skill_damage = 0.0;
        double total_skill_heal = 0.0;
        int skill_count = 0;
        if (h.contains("skills") && h["skills"].is_object()) {
            for (auto s = h["skills"].begin(); s != h["skills"].end(); ++s) {
                const json& sk = s.value();
                double dmg = 0.0;
                double heal = 0.0;
                if (sk.contains("damage") && sk["damage"].is_array() && !sk["damage"].empty()) {
                    dmg = sk["damage"].back().get<double>();
                }
                if (sk.contains("healing") && sk["healing"].is_array() && !sk["healing"].empty()) {
                    heal = sk["healing"].back().get<double>();
                }
                total_skill_damage += dmg;
                total_skill_heal += heal;
                ++skill_count;
            }
        }
        report.add_info("Hero " + it.key(),
                        "skills=" + nfmt::format_int(skill_count)
                            + " total_skill_damage=" + fmt(total_skill_damage)
                            + " total_skill_heal=" + fmt(total_skill_heal),
                        it.key());
    }

    // 4) Damage-type coverage check.
    for (auto it = players.begin(); it != players.end(); ++it) {
        const json& e = it.value();
        const json& dmg = e.value("damage", json::array());
        if (!dmg.is_array() || dmg.empty()) {
            continue;
        }
        const json& lvl0 = dmg[0];
        if (lvl0.is_object()) {
            double melee = lvl0.value("melee", 0.0);
            double ranged = lvl0.value("ranged", 0.0);
            double magical = lvl0.value("magical", 0.0);
            if (melee == 0.0 && ranged == 0.0 && magical == 0.0) {
                report.add_warning("Zero damage combatant",
                                   "Player combatant '" + it.key() + "' has no damage at level 1.",
                                   it.key());
            }
        }
    }

    return report;
}
