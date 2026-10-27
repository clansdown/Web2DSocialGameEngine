#pragma once

#include "FiefdomData.hpp"
#include "ReeveConfig.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class GameConfigCache;

namespace Reeve {

enum class ReeveCategory { Safety, Continuity, Constraint, Growth, Polish };
enum class ReeveActionKind {
    Build,
    Upgrade,
    Convert,
    BuildRoadLink,
    SetReserve,
    SetImport,
    SetOutputRate
};
enum class ReeveTargetKind {
    BuildingCard,
    EconomyPanel,
    ProductionPanel,
    Resource,
    BuildPalette
};
enum class ReeveSeverity { Ok, Watch, Problem, Critical };

struct ReeveText {
    std::string text_id;
    nlohmann::json args = nlohmann::json::object();
};

struct ReeveAction {
    ReeveActionKind kind = ReeveActionKind::Build;
    std::string building_type;
    int building_id = 0;
    int x = 0;
    int y = 0;
    std::vector<std::pair<int, int>> cells;
    std::string resource;
    double value = 0.0;
    bool enabled = false;
    std::string output;
    double rate = 0.0;
};

struct ReeveTarget {
    ReeveTargetKind kind = ReeveTargetKind::EconomyPanel;
    int building_id = 0;
    std::string resource;
    std::string building_type;
};

struct ReeveCandidate {
    std::string id;
    ReeveCategory category = ReeveCategory::Growth;
    ReeveSeverity severity = ReeveSeverity::Watch;
    ReeveText text;
    ReeveText why;
    ReeveAction action;
    ReeveTarget target;
    std::string intent_peer_id;
};

struct ReeveInputStatus {
    std::string resource;
    double required = 0.0;
    double supplied = 0.0;
    double satisfaction = 1.0;
};

struct ReeveOutputStatus {
    std::string resource;
    double rate = 1.0;
    double amount = 0.0;
    double input_satisfaction = 1.0;
    std::vector<ReeveInputStatus> inputs;
};

struct ReeveBuildingStatus {
    int building_id = 0;
    std::string type;
    int level = 0;
    bool water_powered = true;
    std::vector<ReeveOutputStatus> outputs;
};

struct ReeveResourceBalance {
    std::string resource;
    double stock = 0.0;
    double produced = 0.0;
    double consumed = 0.0;
    double demand = 0.0;
    double imported = 0.0;
    double exported = 0.0;
    double net = 0.0;
    double days_cover = 999.0;
};

struct ReeveSnapshot {
    const FiefdomData* fiefdom = nullptr;
    int manor_level = 0;
    nlohmann::json economy_report = nlohmann::json::object();
    std::vector<ReeveResourceBalance> balances;
    std::vector<ReeveBuildingStatus> buildings;
    std::set<std::pair<int, int>> river_cells;
    std::unordered_map<int, bool> water_powered;
    double arable_total = 0.0;
    double arable_used = 0.0;
    double arable_available = 0.0;
    double forest_total = 0.0;
    double forest_used = 0.0;
    double forest_available = 0.0;
    int retinue_funded = 0;
    int retinue_unfunded = 0;
    double morale = 0.0;
};

struct ReeveDetail {
    ReeveText text;
    ReeveSeverity severity = ReeveSeverity::Ok;
    ReeveTarget target;
};

struct ReeveSection {
    std::string id;
    ReeveText title;
    ReeveText status;
    ReeveSeverity severity = ReeveSeverity::Ok;
    std::vector<ReeveDetail> details;
    ReeveTarget target;
};

struct ReeveRecommendation {
    std::string kind = "none"; // action, hold, none
    std::string id;
    std::string category;
    ReeveSeverity severity = ReeveSeverity::Ok;
    ReeveText text;
    ReeveText why;
    ReeveAction action;
    ReeveTarget target;
    std::string reason_text_id;
};

struct ReeveReport {
    std::string style_tag;
    ReeveText headline;
    std::vector<ReeveSection> sections;
    ReeveRecommendation recommendation;
};

ReeveText make_reeve_text(const std::string& text_id, nlohmann::json args = nlohmann::json::object());
nlohmann::json make_reeve_arg_text_id(const std::string& text_id);
nlohmann::json make_reeve_arg_number(double value);

std::string severity_to_string(ReeveSeverity severity);
std::string category_to_string(ReeveCategory category);
std::string action_kind_to_string(ReeveActionKind kind);
std::string target_kind_to_string(ReeveTargetKind kind);

ReeveSnapshot build_reeve_snapshot(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const nlohmann::json& economy_report,
    const ReeveConfig& config);

std::vector<ReeveResourceBalance> build_resource_balances(
    const FiefdomData& fiefdom,
    const nlohmann::json& economy_report,
    const std::vector<std::string>& tracked_resources);

std::vector<ReeveBuildingStatus> parse_building_status(const nlohmann::json& economy_report);

std::vector<ReeveSection> build_reeve_sections(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config);

ReeveText build_reeve_headline(
    const ReeveSnapshot& snapshot,
    const std::vector<ReeveSection>& sections);

std::vector<ReeveCandidate> build_reeve_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style);

std::vector<ReeveCandidate> build_safety_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style);
std::vector<ReeveCandidate> build_continuity_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style);
std::vector<ReeveCandidate> build_constraint_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style);
std::vector<ReeveCandidate> build_growth_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style);
std::vector<ReeveCandidate> build_connect_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style);
std::vector<ReeveCandidate> build_polish_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style);

std::optional<ReeveCandidate> select_reeve_candidate(
    const std::vector<ReeveCandidate>& candidates,
    const ReeveStyle& style);

double score_reeve_candidate(
    const ReeveCandidate& candidate,
    const ReeveStyle& style);

bool is_reeve_action_feasible(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const ReeveAction& action,
    std::string& error_code);

std::string reeve_error_text_id(const std::string& error_code);
std::string reeve_hold_text_id(const std::string& error_code);

std::optional<ReeveAction> next_grow_step(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const std::string& resource,
    const ReeveConfig& config,
    const ReeveStyle& style);
std::optional<ReeveAction> next_upgrade_step(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const std::string& building_type);
std::optional<ReeveAction> next_convert_step(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const std::string& building_type);

ReeveReport build_reeve_report(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const nlohmann::json& economy_report,
    const ReeveConfig& config,
    const ReeveStyle& style);

nlohmann::json reeve_text_to_json(const ReeveText& text);
nlohmann::json reeve_action_to_json(const ReeveAction& action);
nlohmann::json reeve_target_to_json(const ReeveTarget& target);
nlohmann::json reeve_report_to_json(const ReeveReport& report);

} // namespace Reeve
