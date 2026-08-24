#include "weeding/weeding_analyzer.hpp"
#include "fmt.hpp"

#include <algorithm>
#include <cmath>
#include <map>

using json = nlohmann::json;

weeding_analyzer::weeding_analyzer(const config_loader& loader) : loader_(loader) {}

namespace {

std::string fmt(double v) { return nfmt::format_number(v); }

} // namespace

balance_report weeding_analyzer::analyze() const {
    balance_report report;

    auto plants = loader_.weeding_plants();
    auto tools = loader_.weeding_tools();

    // Track per-tool aggregate effectiveness across plants.
    std::map<std::string, double> tool_total_damage;
    std::map<std::string, int> tool_uses;

    for (auto pit = plants.begin(); pit != plants.end(); ++pit) {
        const json& plant = pit.value();
        double hp = plant.value("hp", 100.0);
        double spread = plant.value("spread_probability", 0.0);
        bool smother = plant.value("is_smother_crop", false);
        double min_diff = plant.value("min_difficulty", 1.0);

        std::ostringstream det;
        det << "hp=" << fmt(hp) << " spread=" << fmt(spread)
            << " min_difficulty=" << fmt(min_diff)
            << (smother ? " (smother crop)" : "");

        // Per-tool effectiveness: how many actions to clear (hp / damage).
        if (plant.contains("tools") && plant["tools"].is_object()) {
            det << "; tools[";
            for (auto tit = plant["tools"].begin(); tit != plant["tools"].end(); ++tit) {
                const json& t = tit.value();
                double dmg = t.value("damage", 0.0);
                int actions = dmg > 0.0 ? static_cast<int>(std::ceil(hp / dmg)) : -1;
                det << tit.key() << "=" << (actions < 0 ? "inf" : nfmt::format_int(actions)) << " ";
                tool_total_damage[tit.key()] += dmg;
                ++tool_uses[tit.key()];
            }
            det << "]";
        }

        report.add_info("Plant " + pit.key(), det.str(), pit.key());

        // Flag smother crops with very high spread (gameplay risk).
        if (smother && spread > 0.2) {
            report.add_warning("High-spread smother crop " + pit.key(),
                               "Smother crop spreads at " + fmt(spread) + "/tick; may be hard to control.",
                               pit.key());
        }
    }

    // Tool aggregate efficiency.
    for (const auto& [tool, total_dmg] : tool_total_damage) {
        int uses = tool_uses[tool];
        report.add_info("Tool " + tool,
                        "total_damage=" + fmt(total_dmg) + " plants_used_on=" + nfmt::format_int(uses)
                            + " avg_damage=" + (uses ? fmt(total_dmg / uses) : "0"),
                        tool);
    }

    return report;
}
