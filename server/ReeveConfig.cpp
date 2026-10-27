#include "ReeveConfig.hpp"

#include <algorithm>

namespace Reeve {

ReeveStyle parse_reeve_style(const std::string& tag, const nlohmann::json& style_json) {
    ReeveStyle style;
    style.tag = style_json.value("tag", tag);
    if (style.tag.empty()) style.tag = tag;
    style.name_text_id = style_json.value("name_text_id", "");
    style.description_text_id = style_json.value("description_text_id", "");

    if (style_json.contains("category_weights") && style_json["category_weights"].is_object()) {
        for (const auto& [key, value] : style_json["category_weights"].items()) {
            if (value.is_number()) style.category_weights[key] = value.get<double>();
        }
    }
    if (style_json.contains("action_weights") && style_json["action_weights"].is_object()) {
        for (const auto& [key, value] : style_json["action_weights"].items()) {
            if (value.is_number()) style.action_weights[key] = value.get<double>();
        }
    }

    style.road_first_weight = style_json.value("road_first_weight", 1.0);
    style.corridor_spacing = style_json.value("corridor_spacing", 6);
    style.max_road_link = style_json.value("max_road_link", 12);
    if (style.corridor_spacing < 1) style.corridor_spacing = 1;
    if (style.max_road_link < 0) style.max_road_link = 0;
    return style;
}

ReeveConfig parse_reeve_config(const nlohmann::json& config) {
    ReeveConfig out;
    if (!config.is_object()) {
        out.default_style = "steady";
        out.styles["steady"] = ReeveStyle{};
        return out;
    }

    out.default_style = config.value("default_style", "steady");

    if (config.contains("hard_limits") && config["hard_limits"].is_object()) {
        const auto& limits = config["hard_limits"];
        out.hard_max_road_link = limits.value("max_road_link", 24);
        out.max_site_scan_radius = limits.value("max_site_scan_radius", 64);
    }
    if (out.hard_max_road_link < 1) out.hard_max_road_link = 1;
    if (out.max_site_scan_radius < 1) out.max_site_scan_radius = 1;

    if (config.contains("tracked_resources") && config["tracked_resources"].is_array()) {
        for (const auto& resource : config["tracked_resources"]) {
            if (resource.is_string()) {
                out.tracked_resources.push_back(resource.get<std::string>());
            }
        }
    }
    if (out.tracked_resources.empty()) {
        out.tracked_resources = {
            "grain", "wood", "charcoal", "iron", "ironwork",
            "beams", "boards", "gold", "silver_pence"
        };
    }

    out.breadth_target = config.value("breadth_target", 2);
    if (out.breadth_target < 1) out.breadth_target = 1;

    if (config.contains("thresholds") && config["thresholds"].is_object()) {
        const auto& t = config["thresholds"];
        out.thresholds.stores_watch_days = t.value("stores_watch_days", out.thresholds.stores_watch_days);
        out.thresholds.stores_critical_days = t.value("stores_critical_days", out.thresholds.stores_critical_days);
        out.thresholds.input_satisfaction_watch = t.value("input_satisfaction_watch", out.thresholds.input_satisfaction_watch);
        out.thresholds.land_usage_watch = t.value("land_usage_watch", out.thresholds.land_usage_watch);
        out.thresholds.morale_watch = t.value("morale_watch", out.thresholds.morale_watch);
        out.thresholds.morale_problem = t.value("morale_problem", out.thresholds.morale_problem);
        out.thresholds.import_heavy_units = t.value("import_heavy_units", out.thresholds.import_heavy_units);
        out.thresholds.surplus_export_units = t.value("surplus_export_units", out.thresholds.surplus_export_units);
        out.thresholds.retinue_unfunded_watch = t.value("retinue_unfunded_watch", out.thresholds.retinue_unfunded_watch);
    }

    if (config.contains("producer_chains") && config["producer_chains"].is_object()) {
        for (const auto& [resource, chain] : config["producer_chains"].items()) {
            if (!chain.is_array()) continue;
            std::vector<std::string> stages;
            for (const auto& stage : chain) {
                if (stage.is_string()) stages.push_back(stage.get<std::string>());
            }
            if (!stages.empty()) out.producer_chains[resource] = std::move(stages);
        }
    }

    if (config.contains("styles") && config["styles"].is_object()) {
        for (const auto& [tag, style_json] : config["styles"].items()) {
            if (!style_json.is_object()) continue;
            out.styles[tag] = parse_reeve_style(tag, style_json);
        }
    }
    if (out.styles.empty()) {
        out.styles["steady"] = ReeveStyle{};
        out.styles["steady"].tag = "steady";
    }
    if (out.default_style.empty() || out.styles.find(out.default_style) == out.styles.end()) {
        out.default_style = out.styles.begin()->first;
    }

    // Clamp every style's road-link length to the hard cap.
    for (auto& [tag, style] : out.styles) {
        (void)tag;
        style.max_road_link = std::min(style.max_road_link, out.hard_max_road_link);
    }

    return out;
}

const ReeveStyle& get_reeve_style(const ReeveConfig& config, const std::string& tag) {
    static const ReeveStyle fallback = [] {
        ReeveStyle style;
        style.tag = "steady";
        style.category_weights = {
            {"safety", 4.0}, {"continuity", 3.0}, {"constraint", 2.0},
            {"growth", 1.0}, {"polish", 0.5}
        };
        style.action_weights = {
            {"build", 1.0}, {"upgrade", 1.0}, {"convert", 1.0},
            {"build_road_link", 0.8}, {"set_reserve", 0.5},
            {"set_import", 0.6}, {"set_output_rate", 0.5}
        };
        style.road_first_weight = 0.5;
        style.corridor_spacing = 6;
        style.max_road_link = 12;
        return style;
    }();

    auto it = config.styles.find(tag);
    if (it == config.styles.end()) {
        it = config.styles.find(config.default_style);
    }
    if (it == config.styles.end()) return fallback;
    return it->second;
}

std::string get_default_reeve_style_tag(const ReeveConfig& config) {
    if (!config.default_style.empty() && config.styles.count(config.default_style)) {
        return config.default_style;
    }
    if (!config.styles.empty()) return config.styles.begin()->first;
    return "steady";
}

} // namespace Reeve
