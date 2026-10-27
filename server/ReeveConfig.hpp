#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace Reeve {

/// Tunable thresholds for the Reeve's overview and recommendation rules.
struct ReeveThresholds {
    double stores_watch_days = 5.0;
    double stores_critical_days = 2.0;
    double input_satisfaction_watch = 0.999;
    double land_usage_watch = 0.9;
    double morale_watch = 0.0;
    double morale_problem = -50.0;
    double import_heavy_units = 1.0;
    double surplus_export_units = 1.0;
    int retinue_unfunded_watch = 1;
};

/// A named advising style. Styles only change weights; the recommendation
/// contract is identical for every style.
struct ReeveStyle {
    std::string tag;
    std::string name_text_id;
    std::string description_text_id;
    std::map<std::string, double> category_weights;
    std::map<std::string, double> action_weights;
    double road_first_weight = 1.0;
    int corridor_spacing = 6;
    int max_road_link = 12;
};

/// Parsed server-only `game/config/reeve.json`.
struct ReeveConfig {
    std::string default_style;
    std::map<std::string, ReeveStyle> styles;
    std::map<std::string, std::vector<std::string>> producer_chains;
    std::vector<std::string> tracked_resources;
    ReeveThresholds thresholds;
    int breadth_target = 2;
    int hard_max_road_link = 24;
    int max_site_scan_radius = 64;
};

ReeveConfig parse_reeve_config(const nlohmann::json& config);
ReeveStyle parse_reeve_style(const std::string& tag, const nlohmann::json& style_json);
const ReeveStyle& get_reeve_style(const ReeveConfig& config, const std::string& tag);
std::string get_default_reeve_style_tag(const ReeveConfig& config);

} // namespace Reeve
