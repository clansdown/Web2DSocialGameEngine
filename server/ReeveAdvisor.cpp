#include "ReeveAdvisor.hpp"

#include "ActionHandlers.hpp"
#include "FiefdomFetcher.hpp"
#include "GameConfigCache.hpp"
#include "GridCollision.hpp"
#include "ReevePlacement.hpp"
#include "WaterNetwork.hpp"
#include "game_logic.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Reeve {
namespace {

constexpr double kEpsilon = 0.0001;

double round1(double value) {
    return std::round(value * 10.0) / 10.0;
}

double report_days(const nlohmann::json& report) {
    double seconds = report.value("elapsed_seconds", 0.0);
    if (seconds <= 0.0) return 1.0 / 24.0;
    return seconds / 86400.0;
}

double stock_for(const FiefdomData& fiefdom, const std::string& resource) {
    if (resource == "gold") return fiefdom.gold;
    if (resource == "silver_pence") return fiefdom.silver_pence;
    if (resource == "grain") return fiefdom.grain;
    if (resource == "wood") return fiefdom.wood;
    if (resource == "steel") return fiefdom.steel;
    if (resource == "bronze") return fiefdom.bronze;
    if (resource == "stone") return fiefdom.stone;
    if (resource == "leather") return fiefdom.leather;
    if (resource == "mana") return fiefdom.mana;
    if (resource == "charcoal") return fiefdom.charcoal;
    if (resource == "iron") return fiefdom.iron;
    if (resource == "ironwork") return fiefdom.ironwork;
    if (resource == "fancy_ironwork") return fiefdom.fancy_ironwork;
    if (resource == "beams") return fiefdom.beams;
    if (resource == "boards") return fiefdom.boards;
    return 0.0;
}

std::string resource_label_id(const std::string& resource) {
    return "ui_resource_" + resource;
}

std::string building_label_id(const std::string& building_type) {
    return "ui_building_" + building_type;
}

bool is_valid_resource(const std::string& resource) {
    static const std::set<std::string> resources = {
        "gold", "silver_pence", "grain", "wood", "steel", "bronze", "stone",
        "leather", "mana", "charcoal", "iron", "ironwork", "fancy_ironwork",
        "beams", "boards"
    };
    return resources.count(resource) > 0;
}

ReeveSeverity severity_for_days(double days, const ReeveThresholds& thresholds) {
    if (days <= thresholds.stores_critical_days) return ReeveSeverity::Critical;
    if (days <= thresholds.stores_watch_days) return ReeveSeverity::Problem;
    return ReeveSeverity::Ok;
}

ReeveSeverity max_severity(ReeveSeverity a, ReeveSeverity b) {
    return static_cast<int>(a) >= static_cast<int>(b) ? a : b;
}

bool has_home_base(const FiefdomData& fiefdom) {
    for (const auto& building : fiefdom.buildings) {
        if (building.name == "home_base" && building.level > 0) return true;
    }
    return false;
}

const BuildingData* find_building(const FiefdomData& fiefdom, int building_id) {
    for (const auto& building : fiefdom.buildings) {
        if (building.id == building_id) return &building;
    }
    return nullptr;
}

bool import_enabled(const FiefdomData& fiefdom, const std::string& resource) {
    if (!fiefdom.import_settings.is_object()) return true;
    if (!fiefdom.import_settings.contains(resource)) return true;
    const auto& value = fiefdom.import_settings[resource];
    return !value.is_boolean() || value.get<bool>();
}

std::string worst_input_resource(const ReeveOutputStatus& output, double& satisfaction) {
    std::string worst;
    satisfaction = 1.0;
    for (const auto& input : output.inputs) {
        if (input.satisfaction < satisfaction) {
            satisfaction = input.satisfaction;
            worst = input.resource;
        }
    }
    return worst;
}

double config_max_level(GameConfigCache& cache, const std::string& building_type) {
    auto config = GameLogic::Validation::getBuildingConfig(cache, building_type);
    if (!config) return 0.0;
    return config->value("max_level", 0);
}

bool building_type_has_max_reached(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const std::string& building_type) {
    auto config = GameLogic::Validation::getBuildingConfig(cache, building_type);
    if (!config) return true;
    if (!config->contains("max_per_fiefdom") || !(*config)["max_per_fiefdom"].is_number()) {
        return false;
    }
    int max_count = (*config)["max_per_fiefdom"].get<int>();
    if (max_count < 1) return false;
    int count = GameLogic::Validation::countBuildingsByType(
        cache, fiefdom.id, building_type, 1);
    return count >= max_count;
}

bool has_successor(GameConfigCache& cache, const std::string& building_type) {
    auto types = cache.getFiefdomBuildingTypes();
    if (!types.is_array()) return false;
    for (const auto& type_obj : types) {
        if (!type_obj.is_object()) continue;
        for (const auto& [id, config] : type_obj.items()) {
            (void)id;
            if (config.is_object() && config.value("built_from", "") == building_type) {
                return true;
            }
        }
    }
    return false;
}

bool has_road_morale_source(GameConfigCache& cache, const FiefdomData& fiefdom) {
    auto types = cache.getFiefdomBuildingTypes();
    if (!types.is_array()) return false;
    for (const auto& building : fiefdom.buildings) {
        if (building.level <= 0) continue;
        for (const auto& type_obj : types) {
            if (!type_obj.is_object() || !type_obj.contains(building.name)) continue;
            const auto& config = type_obj[building.name];
            if (config.is_object() && config.contains("road_morale")) return true;
            break;
        }
    }
    return false;
}

ReeveText recommendation_text_for_action(const ReeveAction& action) {
    switch (action.kind) {
        case ReeveActionKind::Build:
            return make_reeve_text("reeve_rec_build_stage", {
                {"building", make_reeve_arg_text_id(building_label_id(action.building_type))}
            });
        case ReeveActionKind::Upgrade: {
            ReeveText text = make_reeve_text("reeve_rec_upgrade_stage");
            if (!action.building_type.empty()) {
                text.args["building"] = make_reeve_arg_text_id(building_label_id(action.building_type));
            }
            return text;
        }
        case ReeveActionKind::Convert: {
            ReeveText text = make_reeve_text("reeve_rec_convert_stage");
            if (!action.building_type.empty()) {
                text.args["building"] = make_reeve_arg_text_id(building_label_id(action.building_type));
            }
            return text;
        }
        case ReeveActionKind::BuildRoadLink:
            return make_reeve_text("reeve_rec_build_road");
        case ReeveActionKind::SetReserve:
            return make_reeve_text("reeve_rec_set_reserve", {
                {"resource", make_reeve_arg_text_id(resource_label_id(action.resource))},
                {"amount", round1(action.value)}
            });
        case ReeveActionKind::SetImport:
            return make_reeve_text("reeve_rec_set_import", {
                {"resource", make_reeve_arg_text_id(resource_label_id(action.resource))}
            });
        case ReeveActionKind::SetOutputRate:
            return make_reeve_text("reeve_rec_set_output_rate", {
                {"building", make_reeve_arg_text_id(building_label_id(action.building_type))},
                {"resource", make_reeve_arg_text_id(resource_label_id(action.output))}
            });
    }
    return make_reeve_text("reeve_rec_none");
}

ReeveTarget target_for_action(const ReeveAction& action) {
    ReeveTarget target;
    switch (action.kind) {
        case ReeveActionKind::Build:
            target.kind = ReeveTargetKind::BuildPalette;
            target.building_type = action.building_type;
            break;
        case ReeveActionKind::Upgrade:
        case ReeveActionKind::Convert:
        case ReeveActionKind::SetOutputRate:
            target.kind = ReeveTargetKind::BuildingCard;
            target.building_id = action.building_id;
            break;
        case ReeveActionKind::BuildRoadLink:
            target.kind = ReeveTargetKind::BuildPalette;
            target.building_type = action.building_type.empty() ? "road" : action.building_type;
            break;
        case ReeveActionKind::SetReserve:
        case ReeveActionKind::SetImport:
            target.kind = ReeveTargetKind::Resource;
            target.resource = action.resource;
            break;
    }
    return target;
}

void add_candidate(std::vector<ReeveCandidate>& candidates, ReeveCandidate candidate) {
    for (const auto& existing : candidates) {
        if (existing.id == candidate.id) return;
    }
    candidates.push_back(std::move(candidate));
}

int category_order(ReeveCategory category) {
    switch (category) {
        case ReeveCategory::Safety: return 0;
        case ReeveCategory::Continuity: return 1;
        case ReeveCategory::Constraint: return 2;
        case ReeveCategory::Growth: return 3;
        case ReeveCategory::Polish: return 4;
    }
    return 5;
}

} // namespace

ReeveText make_reeve_text(const std::string& text_id, nlohmann::json args) {
    ReeveText text;
    text.text_id = text_id;
    text.args = args.is_object() ? std::move(args) : nlohmann::json::object();
    return text;
}

nlohmann::json make_reeve_arg_text_id(const std::string& text_id) {
    nlohmann::json arg = nlohmann::json::object();
    arg["text_id"] = text_id;
    return arg;
}

nlohmann::json make_reeve_arg_number(double value) {
    return round1(value);
}

std::string severity_to_string(ReeveSeverity severity) {
    switch (severity) {
        case ReeveSeverity::Ok: return "ok";
        case ReeveSeverity::Watch: return "watch";
        case ReeveSeverity::Problem: return "problem";
        case ReeveSeverity::Critical: return "critical";
    }
    return "ok";
}

std::string category_to_string(ReeveCategory category) {
    switch (category) {
        case ReeveCategory::Safety: return "safety";
        case ReeveCategory::Continuity: return "continuity";
        case ReeveCategory::Constraint: return "constraint";
        case ReeveCategory::Growth: return "growth";
        case ReeveCategory::Polish: return "polish";
    }
    return "polish";
}

std::string action_kind_to_string(ReeveActionKind kind) {
    switch (kind) {
        case ReeveActionKind::Build: return "build";
        case ReeveActionKind::Upgrade: return "upgrade";
        case ReeveActionKind::Convert: return "convert";
        case ReeveActionKind::BuildRoadLink: return "build_road_link";
        case ReeveActionKind::SetReserve: return "set_reserve";
        case ReeveActionKind::SetImport: return "set_import";
        case ReeveActionKind::SetOutputRate: return "set_output_rate";
    }
    return "build";
}

std::string target_kind_to_string(ReeveTargetKind kind) {
    switch (kind) {
        case ReeveTargetKind::BuildingCard: return "building_card";
        case ReeveTargetKind::EconomyPanel: return "economy_panel";
        case ReeveTargetKind::ProductionPanel: return "production_panel";
        case ReeveTargetKind::Resource: return "resource";
        case ReeveTargetKind::BuildPalette: return "build_palette";
    }
    return "economy_panel";
}

std::vector<ReeveResourceBalance> build_resource_balances(
    const FiefdomData& fiefdom,
    const nlohmann::json& economy_report,
    const std::vector<std::string>& tracked_resources) {
    std::vector<ReeveResourceBalance> balances;
    const double days = report_days(economy_report);
    const nlohmann::json produced = economy_report.value("produced", nlohmann::json::object());
    const nlohmann::json consumed = economy_report.value("consumed", nlohmann::json::object());
    const nlohmann::json demand = economy_report.value("demand", nlohmann::json::object());
    const nlohmann::json imported = economy_report.value("imported", nlohmann::json::object());
    const nlohmann::json exported = economy_report.value("exported", nlohmann::json::object());

    for (const auto& resource : tracked_resources) {
        ReeveResourceBalance balance;
        balance.resource = resource;
        balance.stock = stock_for(fiefdom, resource);
        if (produced.is_object() && produced.contains(resource) && produced[resource].is_number()) {
            balance.produced = produced[resource].get<double>() / days;
        }
        if (consumed.is_object() && consumed.contains(resource) && consumed[resource].is_number()) {
            balance.consumed = consumed[resource].get<double>() / days;
        }
        if (demand.is_object() && demand.contains(resource) && demand[resource].is_number()) {
            balance.demand = demand[resource].get<double>() / days;
        } else {
            balance.demand = balance.consumed;
        }
        if (imported.is_object() && imported.contains(resource) && imported[resource].is_number()) {
            balance.imported = imported[resource].get<double>() / days;
        }
        if (exported.is_object() && exported.contains(resource) && exported[resource].is_object()) {
            balance.exported = exported[resource].value("amount", 0.0) / days;
        }
        balance.net = balance.produced + balance.imported - balance.demand - balance.exported;
        balance.days_cover = balance.net < -kEpsilon
            ? balance.stock / (-balance.net)
            : 999.0;
        balances.push_back(std::move(balance));
    }
    return balances;
}

std::vector<ReeveBuildingStatus> parse_building_status(const nlohmann::json& economy_report) {
    std::vector<ReeveBuildingStatus> buildings;
    if (!economy_report.contains("building_status") ||
        !economy_report["building_status"].is_array()) {
        return buildings;
    }
    for (const auto& entry : economy_report["building_status"]) {
        if (!entry.is_object()) continue;
        ReeveBuildingStatus building;
        building.building_id = entry.value("building_id", 0);
        building.type = entry.value("type", "");
        building.level = entry.value("level", 0);
        building.water_powered = entry.value("water_powered", true);
        if (entry.contains("outputs") && entry["outputs"].is_array()) {
            for (const auto& output_entry : entry["outputs"]) {
                if (!output_entry.is_object()) continue;
                ReeveOutputStatus output;
                output.resource = output_entry.value("resource", "");
                output.rate = output_entry.value("rate", 1.0);
                output.amount = output_entry.value("amount", 0.0);
                output.input_satisfaction = output_entry.value("input_satisfaction", 1.0);
                if (output_entry.contains("inputs") && output_entry["inputs"].is_array()) {
                    for (const auto& input_entry : output_entry["inputs"]) {
                        if (!input_entry.is_object()) continue;
                        ReeveInputStatus input;
                        input.resource = input_entry.value("resource", "");
                        input.required = input_entry.value("required", 0.0);
                        input.supplied = input_entry.value("supplied", 0.0);
                        input.satisfaction = input_entry.value("satisfaction", 1.0);
                        output.inputs.push_back(std::move(input));
                    }
                }
                building.outputs.push_back(std::move(output));
            }
        }
        buildings.push_back(std::move(building));
    }
    return buildings;
}

ReeveSnapshot build_reeve_snapshot(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const nlohmann::json& economy_report,
    const ReeveConfig& config) {
    ReeveSnapshot snapshot;
    snapshot.fiefdom = &fiefdom;
    snapshot.manor_level = fiefdom.manor_level;
    snapshot.economy_report = economy_report.is_object()
        ? economy_report
        : nlohmann::json::object();
    snapshot.balances = build_resource_balances(
        fiefdom, snapshot.economy_report, config.tracked_resources);
    snapshot.buildings = parse_building_status(snapshot.economy_report);
    snapshot.morale = fiefdom.morale;

    for (const auto& [x, y] : FiefdomFetcher::fetchRiverCells(fiefdom.id)) {
        snapshot.river_cells.insert({x, y});
    }
    auto water = Water::computeWaterPower(
        cache.getFiefdomBuildingTypes(), fiefdom.buildings, snapshot.river_cells);
    snapshot.water_powered = water.powered;

    snapshot.arable_total = GameLogic::Validation::getTotalArableAcres(cache, fiefdom.id);
    snapshot.arable_used = GameLogic::Validation::getUsedArableAcres(cache, fiefdom.id);
    snapshot.arable_available = GameLogic::Validation::getAvailableArableAcres(cache, fiefdom.id);
    snapshot.forest_total = GameLogic::Validation::getTotalForestAcres(cache, fiefdom.id);
    snapshot.forest_used = GameLogic::Validation::getUsedForestAcres(cache, fiefdom.id);
    snapshot.forest_available = GameLogic::Validation::getAvailableForestAcres(cache, fiefdom.id);

    if (snapshot.economy_report.contains("retinue") &&
        snapshot.economy_report["retinue"].is_object()) {
        const auto& retinue = snapshot.economy_report["retinue"];
        snapshot.retinue_funded = retinue.value("funded", 0);
        snapshot.retinue_unfunded = retinue.value("unfunded", 0);
    }

    return snapshot;
}

std::vector<ReeveSection> build_reeve_sections(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config) {
    (void)cache;
    std::vector<ReeveSection> sections;
    const auto& thresholds = config.thresholds;

    // Treasury
    {
        ReeveSection section;
        section.id = "treasury";
        section.title = make_reeve_text("reeve_section_treasury");
        section.status = make_reeve_text("reeve_status_treasury_ok");
        section.severity = ReeveSeverity::Ok;
        ReeveDetail detail;
        detail.text = make_reeve_text("reeve_detail_treasury", {
            {"gold", round1(snapshot.fiefdom->gold)},
            {"pence", round1(snapshot.fiefdom->silver_pence)}
        });
        detail.target.kind = ReeveTargetKind::EconomyPanel;
        section.details.push_back(std::move(detail));
        section.target.kind = ReeveTargetKind::EconomyPanel;
        sections.push_back(std::move(section));
    }

    // Stores
    {
        std::vector<ReeveResourceBalance> low;
        for (const auto& balance : snapshot.balances) {
            if (balance.days_cover <= thresholds.stores_watch_days) {
                low.push_back(balance);
            }
        }
        std::sort(low.begin(), low.end(), [](const auto& a, const auto& b) {
            if (a.days_cover != b.days_cover) return a.days_cover < b.days_cover;
            if (a.net != b.net) return a.net < b.net;
            return a.resource < b.resource;
        });

        ReeveSection section;
        section.id = "stores";
        section.title = make_reeve_text("reeve_section_stores");
        section.target.kind = ReeveTargetKind::EconomyPanel;
        if (low.empty()) {
            section.status = make_reeve_text("reeve_status_stores_ok");
            section.severity = ReeveSeverity::Ok;
        } else {
            const auto& worst = low.front();
            section.status = make_reeve_text("reeve_status_stores_low", {
                {"resource", make_reeve_arg_text_id(resource_label_id(worst.resource))},
                {"days", round1(worst.days_cover)}
            });
            section.severity = severity_for_days(worst.days_cover, thresholds);
            for (size_t i = 0; i < low.size() && i < 5; ++i) {
                const auto& balance = low[i];
                ReeveDetail detail;
                detail.text = make_reeve_text("reeve_detail_resource_balance", {
                    {"resource", make_reeve_arg_text_id(resource_label_id(balance.resource))},
                    {"stock", round1(balance.stock)},
                    {"net", round1(balance.net)},
                    {"days", round1(balance.days_cover)}
                });
                detail.severity = severity_for_days(balance.days_cover, thresholds);
                detail.target.kind = ReeveTargetKind::Resource;
                detail.target.resource = balance.resource;
                section.details.push_back(std::move(detail));
            }
        }
        sections.push_back(std::move(section));
    }

    // Production / input satisfaction
    {
        struct Starved {
            int building_id;
            std::string type;
            ReeveOutputStatus output;
            double satisfaction;
        };
        std::vector<Starved> starved;
        for (const auto& building : snapshot.buildings) {
            for (const auto& output : building.outputs) {
                if (output.inputs.empty()) continue;
                if (output.input_satisfaction + 0.0001 < thresholds.input_satisfaction_watch) {
                    starved.push_back({building.building_id, building.type, output,
                                       output.input_satisfaction});
                }
            }
        }
        std::sort(starved.begin(), starved.end(), [](const auto& a, const auto& b) {
            if (a.satisfaction != b.satisfaction) return a.satisfaction < b.satisfaction;
            if (a.building_id != b.building_id) return a.building_id < b.building_id;
            if (a.output.resource != b.output.resource) {
                return a.output.resource < b.output.resource;
            }
            return a.type < b.type;
        });

        ReeveSection section;
        section.id = "production";
        section.title = make_reeve_text("reeve_section_production");
        section.target.kind = ReeveTargetKind::ProductionPanel;
        if (starved.empty()) {
            section.status = make_reeve_text("reeve_status_ok");
            section.severity = ReeveSeverity::Ok;
        } else {
            section.status = make_reeve_text("reeve_status_inputs_starved", {
                {"count", static_cast<int>(starved.size())}
            });
            section.severity = starved.front().satisfaction < 0.5
                ? ReeveSeverity::Problem
                : ReeveSeverity::Watch;
            for (size_t i = 0; i < starved.size() && i < 5; ++i) {
                const auto& item = starved[i];
                double satisfaction = 1.0;
                std::string input_resource = worst_input_resource(item.output, satisfaction);
                if (input_resource.empty()) input_resource = item.output.resource;
                ReeveDetail detail;
                detail.text = make_reeve_text("reeve_detail_building_starved", {
                    {"building", make_reeve_arg_text_id(building_label_id(item.type))},
                    {"resource", make_reeve_arg_text_id(resource_label_id(input_resource))},
                    {"percent", static_cast<int>(std::round(item.satisfaction * 100.0))}
                });
                detail.severity = item.satisfaction < 0.5
                    ? ReeveSeverity::Problem
                    : ReeveSeverity::Watch;
                detail.target.kind = ReeveTargetKind::BuildingCard;
                detail.target.building_id = item.building_id;
                section.details.push_back(std::move(detail));
            }
        }
        sections.push_back(std::move(section));
    }

    // Land
    {
        double arable_ratio = snapshot.arable_total > 0.0
            ? snapshot.arable_used / snapshot.arable_total
            : 0.0;
        double forest_ratio = snapshot.forest_total > 0.0
            ? snapshot.forest_used / snapshot.forest_total
            : 0.0;
        bool low = (snapshot.arable_total > 0.0 &&
                    (snapshot.arable_available <= 0.0001 ||
                     arable_ratio >= thresholds.land_usage_watch)) ||
                   (snapshot.forest_total > 0.0 &&
                    (snapshot.forest_available <= 0.0001 ||
                     forest_ratio >= thresholds.land_usage_watch));

        ReeveSection section;
        section.id = "land";
        section.title = make_reeve_text("reeve_section_land");
        section.target.kind = ReeveTargetKind::EconomyPanel;
        ReeveSeverity land_severity = ReeveSeverity::Ok;
        if (low) {
            land_severity = (snapshot.arable_available <= 0.0001 &&
                             snapshot.arable_total > 0.0) ||
                            (snapshot.forest_available <= 0.0001 &&
                             snapshot.forest_total > 0.0)
                ? ReeveSeverity::Problem
                : ReeveSeverity::Watch;
        }
        ReeveDetail detail;
        detail.text = make_reeve_text("reeve_detail_land_usage", {
            {"arable_used", round1(snapshot.arable_used)},
            {"arable_total", round1(snapshot.arable_total)},
            {"forest_used", round1(snapshot.forest_used)},
            {"forest_total", round1(snapshot.forest_total)}
        });
        detail.severity = land_severity;
        detail.target.kind = ReeveTargetKind::EconomyPanel;
        section.details.push_back(std::move(detail));
        if (low) {
            section.status = make_reeve_text("reeve_status_land_low");
        } else {
            section.status = make_reeve_text("reeve_status_ok");
        }
        section.severity = land_severity;
        sections.push_back(std::move(section));
    }

    // Water
    {
        std::vector<const ReeveBuildingStatus*> unpowered;
        for (const auto& building : snapshot.buildings) {
            if (!building.water_powered) unpowered.push_back(&building);
        }
        ReeveSection section;
        section.id = "water";
        section.title = make_reeve_text("reeve_section_water");
        section.target.kind = ReeveTargetKind::ProductionPanel;
        if (unpowered.empty()) {
            section.status = make_reeve_text("reeve_status_ok");
            section.severity = ReeveSeverity::Ok;
        } else {
            section.status = make_reeve_text("reeve_status_water_unpowered", {
                {"count", static_cast<int>(unpowered.size())}
            });
            section.severity = ReeveSeverity::Watch;
            for (size_t i = 0; i < unpowered.size() && i < 5; ++i) {
                ReeveDetail detail;
                detail.text = make_reeve_text("reeve_detail_water_building", {
                    {"building", make_reeve_arg_text_id(building_label_id(unpowered[i]->type))}
                });
                detail.severity = ReeveSeverity::Watch;
                detail.target.kind = ReeveTargetKind::BuildingCard;
                detail.target.building_id = unpowered[i]->building_id;
                section.details.push_back(std::move(detail));
            }
        }
        sections.push_back(std::move(section));
    }

    // Morale
    {
        ReeveSeverity severity = ReeveSeverity::Ok;
        if (snapshot.morale < thresholds.morale_problem) {
            severity = ReeveSeverity::Problem;
        } else if (snapshot.morale < thresholds.morale_watch) {
            severity = ReeveSeverity::Watch;
        }
        ReeveSection section;
        section.id = "morale";
        section.title = make_reeve_text("reeve_section_morale");
        section.target.kind = ReeveTargetKind::EconomyPanel;
        if (severity == ReeveSeverity::Ok) {
            section.status = make_reeve_text("reeve_status_ok");
        } else {
            section.status = make_reeve_text("reeve_status_morale_low", {
                {"morale", round1(snapshot.morale)}
            });
        }
        section.severity = severity;
        ReeveDetail detail;
        detail.text = make_reeve_text("reeve_detail_morale", {
            {"morale", round1(snapshot.morale)},
            {"unfunded", snapshot.retinue_unfunded}
        });
        detail.severity = severity;
        detail.target.kind = ReeveTargetKind::EconomyPanel;
        section.details.push_back(std::move(detail));
        sections.push_back(std::move(section));
    }

    // Retinue
    {
        bool unfunded = snapshot.retinue_unfunded >= thresholds.retinue_unfunded_watch;
        ReeveSection section;
        section.id = "retinue";
        section.title = make_reeve_text("reeve_section_retinue");
        section.target.kind = ReeveTargetKind::EconomyPanel;
        if (unfunded) {
            section.status = make_reeve_text("reeve_status_retinue_unfunded", {
                {"unfunded", snapshot.retinue_unfunded}
            });
            section.severity = ReeveSeverity::Problem;
        } else {
            section.status = make_reeve_text("reeve_status_ok");
            section.severity = ReeveSeverity::Ok;
        }
        ReeveDetail detail;
        detail.text = make_reeve_text("reeve_detail_retinue", {
            {"funded", snapshot.retinue_funded},
            {"unfunded", snapshot.retinue_unfunded}
        });
        detail.severity = section.severity;
        detail.target.kind = ReeveTargetKind::EconomyPanel;
        section.details.push_back(std::move(detail));
        sections.push_back(std::move(section));
    }

    // Trade
    {
        std::vector<ReeveResourceBalance> imports;
        std::vector<ReeveResourceBalance> exports;
        for (const auto& balance : snapshot.balances) {
            if (balance.imported > 0.0001) imports.push_back(balance);
            if (balance.exported > 0.0001) exports.push_back(balance);
        }
        std::sort(imports.begin(), imports.end(), [](const auto& a, const auto& b) {
            if (a.imported != b.imported) return a.imported > b.imported;
            return a.resource < b.resource;
        });
        std::sort(exports.begin(), exports.end(), [](const auto& a, const auto& b) {
            if (a.exported != b.exported) return a.exported > b.exported;
            return a.resource < b.resource;
        });

        ReeveSection section;
        section.id = "trade";
        section.title = make_reeve_text("reeve_section_trade");
        section.target.kind = ReeveTargetKind::EconomyPanel;
        if (!imports.empty() && imports.front().imported >= thresholds.import_heavy_units) {
            section.status = make_reeve_text("reeve_status_imports_heavy", {
                {"resource", make_reeve_arg_text_id(resource_label_id(imports.front().resource))},
                {"amount", round1(imports.front().imported)}
            });
            section.severity = ReeveSeverity::Watch;
        } else {
            section.status = make_reeve_text("reeve_status_trade_ok");
            section.severity = ReeveSeverity::Ok;
        }
        for (size_t i = 0; i < imports.size() && i < 3; ++i) {
            ReeveDetail detail;
            detail.text = make_reeve_text("reeve_detail_import", {
                {"resource", make_reeve_arg_text_id(resource_label_id(imports[i].resource))},
                {"amount", round1(imports[i].imported)}
            });
            detail.severity = ReeveSeverity::Ok;
            detail.target.kind = ReeveTargetKind::Resource;
            detail.target.resource = imports[i].resource;
            section.details.push_back(std::move(detail));
        }
        for (size_t i = 0; i < exports.size() && i < 3; ++i) {
            ReeveDetail detail;
            detail.text = make_reeve_text("reeve_detail_export", {
                {"resource", make_reeve_arg_text_id(resource_label_id(exports[i].resource))},
                {"amount", round1(exports[i].exported)}
            });
            detail.severity = ReeveSeverity::Ok;
            detail.target.kind = ReeveTargetKind::Resource;
            detail.target.resource = exports[i].resource;
            section.details.push_back(std::move(detail));
        }
        sections.push_back(std::move(section));
    }

    return sections;
}

ReeveText build_reeve_headline(
    const ReeveSnapshot& snapshot,
    const std::vector<ReeveSection>& sections) {
    if (!has_home_base(*snapshot.fiefdom)) {
        return make_reeve_text("reeve_headline_no_manor");
    }
    ReeveSeverity worst = ReeveSeverity::Ok;
    for (const auto& section : sections) {
        worst = max_severity(worst, section.severity);
    }
    switch (worst) {
        case ReeveSeverity::Critical:
            return make_reeve_text("reeve_headline_critical");
        case ReeveSeverity::Problem:
            return make_reeve_text("reeve_headline_problem");
        case ReeveSeverity::Watch:
            return make_reeve_text("reeve_headline_watch");
        case ReeveSeverity::Ok:
            return make_reeve_text("reeve_headline_ok");
    }
    return make_reeve_text("reeve_headline_ok");
}

std::optional<ReeveAction> next_upgrade_step(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const std::string& building_type) {
    std::vector<const BuildingData*> candidates;
    for (const auto& building : snapshot.fiefdom->buildings) {
        if (building.name != building_type) continue;
        if (building.level <= 0) continue;
        double max_level = config_max_level(cache, building_type);
        if (building.level >= static_cast<int>(max_level)) continue;
        candidates.push_back(&building);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto* a, const auto* b) {
        if (a->level != b->level) return a->level < b->level;
        return a->id < b->id;
    });
    if (candidates.empty()) return std::nullopt;
    ReeveAction action;
    action.kind = ReeveActionKind::Upgrade;
    action.building_type = building_type;
    action.building_id = candidates.front()->id;
    return action;
}

std::optional<ReeveAction> next_convert_step(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const std::string& building_type) {
    if (!has_successor(cache, building_type)) return std::nullopt;
    std::vector<const BuildingData*> candidates;
    for (const auto& building : snapshot.fiefdom->buildings) {
        if (building.name != building_type) continue;
        if (building.level <= 0) continue;
        double max_level = config_max_level(cache, building_type);
        if (building.level < static_cast<int>(max_level)) continue;
        candidates.push_back(&building);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto* a, const auto* b) {
        return a->id < b->id;
    });
    if (candidates.empty()) return std::nullopt;
    ReeveAction action;
    action.kind = ReeveActionKind::Convert;
    action.building_type = building_type;
    action.building_id = candidates.front()->id;
    return action;
}

std::optional<ReeveAction> next_grow_step(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const std::string& resource,
    const ReeveConfig& config,
    const ReeveStyle& style) {
    auto chain_it = config.producer_chains.find(resource);
    if (chain_it == config.producer_chains.end()) return std::nullopt;
    const auto& chain = chain_it->second;
    if (chain.empty()) return std::nullopt;

    int top_index = -1;
    for (size_t i = 0; i < chain.size(); ++i) {
        for (const auto& building : snapshot.fiefdom->buildings) {
            if (building.name == chain[i] && building.level > 0) {
                top_index = static_cast<int>(i);
                break;
            }
        }
    }
    int current_index = top_index >= 0 ? top_index : 0;
    const std::string& current_type = chain[current_index];
    const int current_count = GameLogic::Validation::countBuildingsByType(
        cache, snapshot.fiefdom->id, current_type, 1);

    // 1. Breadth: build out the current top stage up to the configured target.
    if (current_count < config.breadth_target &&
        !building_type_has_max_reached(cache, *snapshot.fiefdom, current_type)) {
        if (auto site = find_building_site(
                cache, *snapshot.fiefdom, current_type, style,
                config.max_site_scan_radius)) {
            ReeveAction action;
            action.kind = ReeveActionKind::Build;
            action.building_type = current_type;
            action.x = site->first;
            action.y = site->second;
            return action;
        }
    }

    // 2. Depth: upgrade the current top stage.
    if (auto upgrade = next_upgrade_step(cache, snapshot, current_type)) {
        return upgrade;
    }

    // 3. Tier: build the next stage, or convert a maxed current stage.
    if (current_index + 1 < static_cast<int>(chain.size())) {
        const std::string& next_type = chain[current_index + 1];
        if (!building_type_has_max_reached(cache, *snapshot.fiefdom, next_type)) {
            if (auto site = find_building_site(
                    cache, *snapshot.fiefdom, next_type, style,
                    config.max_site_scan_radius)) {
                ReeveAction action;
                action.kind = ReeveActionKind::Build;
                action.building_type = next_type;
                action.x = site->first;
                action.y = site->second;
                return action;
            }
        }
        if (auto convert = next_convert_step(cache, snapshot, current_type)) {
            return convert;
        }
    }

    // 4. Fallback breadth when the target is met but no upgrade/tier exists.
    if (!building_type_has_max_reached(cache, *snapshot.fiefdom, current_type)) {
        if (auto site = find_building_site(
                cache, *snapshot.fiefdom, current_type, style,
                config.max_site_scan_radius)) {
            ReeveAction action;
            action.kind = ReeveActionKind::Build;
            action.building_type = current_type;
            action.x = site->first;
            action.y = site->second;
            return action;
        }
    }

    return std::nullopt;
}

std::vector<ReeveCandidate> build_safety_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style) {
    std::vector<ReeveCandidate> candidates;
    const auto& thresholds = config.thresholds;

    std::vector<ReeveResourceBalance> low;
    for (const auto& balance : snapshot.balances) {
        if (balance.net < -kEpsilon) {
            ReeveSeverity severity = severity_for_days(balance.days_cover, thresholds);
            if (severity != ReeveSeverity::Ok) low.push_back(balance);
        }
    }
    std::sort(low.begin(), low.end(), [](const auto& a, const auto& b) {
        if (a.days_cover != b.days_cover) return a.days_cover < b.days_cover;
        if (a.net != b.net) return a.net < b.net;
        return a.resource < b.resource;
    });

    for (size_t i = 0; i < low.size() && i < 3; ++i) {
        const auto& balance = low[i];
        auto action = next_grow_step(
            cache, snapshot, balance.resource, config, style);
        if (!action) continue;
        ReeveCandidate candidate;
        candidate.id = "safety_stores_low_" + balance.resource;
        candidate.category = ReeveCategory::Safety;
        candidate.severity = severity_for_days(balance.days_cover, thresholds);
        candidate.text = recommendation_text_for_action(*action);
        candidate.why = make_reeve_text("reeve_why_resource_deficit", {
            {"resource", make_reeve_arg_text_id(resource_label_id(balance.resource))},
            {"days", round1(balance.days_cover)},
            {"net", round1(balance.net)}
        });
        candidate.action = *action;
        candidate.target = target_for_action(*action);
        add_candidate(candidates, std::move(candidate));
    }

    if (snapshot.retinue_unfunded >= thresholds.retinue_unfunded_watch) {
        const ReeveResourceBalance* best = nullptr;
        for (const auto& balance : snapshot.balances) {
            if (balance.resource == "gold" || balance.resource == "silver_pence") continue;
            if (balance.stock <= 0.0) continue;
            if (balance.net < -kEpsilon) continue;
            if (balance.days_cover <= thresholds.stores_watch_days) continue;
            if (!best || balance.stock > best->stock) best = &balance;
        }
        ReeveCandidate candidate;
        candidate.id = "safety_retinue_unfunded";
        candidate.category = ReeveCategory::Safety;
        candidate.severity = ReeveSeverity::Problem;
        candidate.why = make_reeve_text("reeve_why_retinue_unfunded", {
            {"funded", snapshot.retinue_funded},
            {"unfunded", snapshot.retinue_unfunded}
        });
        if (best) {
            candidate.action.kind = ReeveActionKind::SetReserve;
            candidate.action.resource = best->resource;
            candidate.action.value = round1(best->stock * 0.5);
            candidate.text = make_reeve_text("reeve_rec_set_reserve", {
                {"resource", make_reeve_arg_text_id(resource_label_id(best->resource))},
                {"amount", candidate.action.value}
            });
            candidate.target.kind = ReeveTargetKind::Resource;
            candidate.target.resource = best->resource;
        } else {
            candidate.action.kind = ReeveActionKind::SetReserve;
            candidate.text = make_reeve_text("reeve_rec_hold_need_money");
            candidate.target.kind = ReeveTargetKind::EconomyPanel;
        }
        add_candidate(candidates, std::move(candidate));
    }

    return candidates;
}

std::vector<ReeveCandidate> build_continuity_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style) {
    std::vector<ReeveCandidate> candidates;
    const auto& thresholds = config.thresholds;
    std::set<std::string> seen_resources;

    for (const auto& building : snapshot.buildings) {
        for (const auto& output : building.outputs) {
            if (output.input_satisfaction + 0.0001 >= thresholds.input_satisfaction_watch) {
                continue;
            }
            double satisfaction = 1.0;
            std::string input_resource = worst_input_resource(output, satisfaction);
            if (input_resource.empty() || seen_resources.count(input_resource)) continue;
            seen_resources.insert(input_resource);

            ReeveCandidate candidate;
            candidate.id = "continuity_starved_" + input_resource;
            candidate.category = ReeveCategory::Continuity;
            candidate.severity = output.input_satisfaction < 0.5
                ? ReeveSeverity::Problem
                : ReeveSeverity::Watch;
            candidate.why = make_reeve_text("reeve_why_building_starved", {
                {"building", make_reeve_arg_text_id(building_label_id(building.type))},
                {"resource", make_reeve_arg_text_id(resource_label_id(input_resource))},
                {"percent", static_cast<int>(std::round(output.input_satisfaction * 100.0))}
            });

            if (!import_enabled(*snapshot.fiefdom, input_resource)) {
                candidate.action.kind = ReeveActionKind::SetImport;
                candidate.action.resource = input_resource;
                candidate.action.enabled = true;
                candidate.text = make_reeve_text("reeve_rec_set_import", {
                    {"resource", make_reeve_arg_text_id(resource_label_id(input_resource))}
                });
            } else {
                auto action = next_grow_step(
                    cache, snapshot, input_resource, config, style);
                if (!action) {
                    seen_resources.erase(input_resource);
                    continue;
                }
                candidate.action = *action;
                candidate.text = recommendation_text_for_action(*action);
            }
            candidate.target.kind = ReeveTargetKind::BuildingCard;
            candidate.target.building_id = building.building_id;
            add_candidate(candidates, std::move(candidate));
            if (candidates.size() >= 3) return candidates;
        }
    }
    return candidates;
}

std::vector<ReeveCandidate> build_constraint_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style) {
    (void)style;
    std::vector<ReeveCandidate> candidates;
    const auto& thresholds = config.thresholds;
    double arable_ratio = snapshot.arable_total > 0.0
        ? snapshot.arable_used / snapshot.arable_total
        : 0.0;
    double forest_ratio = snapshot.forest_total > 0.0
        ? snapshot.forest_used / snapshot.forest_total
        : 0.0;
    bool arable_low = snapshot.arable_total > 0.0 &&
        (snapshot.arable_available <= 0.0001 ||
         arable_ratio >= thresholds.land_usage_watch);
    bool forest_low = snapshot.forest_total > 0.0 &&
        (snapshot.forest_available <= 0.0001 ||
         forest_ratio >= thresholds.land_usage_watch);
    if (!arable_low && !forest_low) return candidates;

    for (const auto& building : snapshot.fiefdom->buildings) {
        if (building.name != "home_base" || building.level <= 0) continue;
        double max_level = config_max_level(cache, "home_base");
        if (building.level >= static_cast<int>(max_level)) break;
        ReeveCandidate candidate;
        candidate.id = "constraint_land";
        candidate.category = ReeveCategory::Constraint;
        candidate.severity = (snapshot.arable_available <= 0.0001 &&
                              snapshot.arable_total > 0.0) ||
                             (snapshot.forest_available <= 0.0001 &&
                              snapshot.forest_total > 0.0)
            ? ReeveSeverity::Problem
            : ReeveSeverity::Watch;
        candidate.action.kind = ReeveActionKind::Upgrade;
        candidate.action.building_type = "home_base";
        candidate.action.building_id = building.id;
        candidate.text = make_reeve_text("reeve_rec_upgrade_stage", {
            {"building", make_reeve_arg_text_id(building_label_id("home_base"))}
        });
        candidate.why = make_reeve_text("reeve_why_land_low", {
            {"arable_used", round1(snapshot.arable_used)},
            {"arable_total", round1(snapshot.arable_total)},
            {"forest_used", round1(snapshot.forest_used)},
            {"forest_total", round1(snapshot.forest_total)}
        });
        candidate.target.kind = ReeveTargetKind::BuildingCard;
        candidate.target.building_id = building.id;
        add_candidate(candidates, std::move(candidate));
        break;
    }
    return candidates;
}

std::vector<ReeveCandidate> build_growth_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style) {
    std::vector<ReeveCandidate> candidates;
    const auto& thresholds = config.thresholds;

    const ReeveResourceBalance* deficit = nullptr;
    for (const auto& balance : snapshot.balances) {
        if (balance.net >= -kEpsilon) continue;
        if (config.producer_chains.find(balance.resource) == config.producer_chains.end()) {
            continue;
        }
        if (!deficit ||
            balance.days_cover < deficit->days_cover ||
            (balance.days_cover == deficit->days_cover && balance.net < deficit->net)) {
            deficit = &balance;
        }
    }

    if (deficit) {
        auto action = next_grow_step(
            cache, snapshot, deficit->resource, config, style);
        if (action) {
            ReeveCandidate candidate;
            candidate.id = "growth_deficit_" + deficit->resource;
            candidate.category = ReeveCategory::Growth;
            candidate.severity = deficit->days_cover <= thresholds.stores_watch_days
                ? ReeveSeverity::Problem
                : ReeveSeverity::Watch;
            candidate.text = recommendation_text_for_action(*action);
            candidate.why = make_reeve_text("reeve_why_resource_deficit", {
                {"resource", make_reeve_arg_text_id(resource_label_id(deficit->resource))},
                {"days", round1(deficit->days_cover)},
                {"net", round1(deficit->net)}
            });
            candidate.action = *action;
            candidate.target = target_for_action(*action);
            const std::string growth_id = candidate.id;
            const ReeveSeverity growth_severity = candidate.severity;
            add_candidate(candidates, std::move(candidate));

            if (action->kind == ReeveActionKind::Build &&
                has_road_morale_source(cache, *snapshot.fiefdom)) {
                auto dims = GameLogic::GridCollision::getBuildingDimensions(
                    cache, action->building_type);
                int w = dims.valid ? dims.width : 1;
                int h = dims.valid ? dims.height : 1;
                auto road = find_road_link_to_site(
                    cache, *snapshot.fiefdom, action->x, action->y, w, h,
                    style.max_road_link, style.corridor_spacing);
                if (road.needed && road.found && !road.cells.empty()) {
                    ReeveCandidate road_candidate;
                    road_candidate.id = "connect_growth_site_" + deficit->resource;
                    road_candidate.category = ReeveCategory::Growth;
                    road_candidate.severity = growth_severity;
                    road_candidate.text = make_reeve_text("reeve_rec_build_road");
                    road_candidate.why = make_reeve_text("reeve_why_road");
                    road_candidate.action.kind = ReeveActionKind::BuildRoadLink;
                    road_candidate.action.cells = road.cells;
                    road_candidate.action.building_type = action->building_type;
                    road_candidate.target.kind = ReeveTargetKind::BuildPalette;
                    road_candidate.target.building_type = "road";
                    road_candidate.intent_peer_id = growth_id;
                    add_candidate(candidates, std::move(road_candidate));
                }
            }
        }
    }

    const ReeveResourceBalance* heavy_import = nullptr;
    for (const auto& balance : snapshot.balances) {
        if (balance.imported < thresholds.import_heavy_units) continue;
        if (config.producer_chains.find(balance.resource) == config.producer_chains.end()) {
            continue;
        }
        if (deficit && balance.resource == deficit->resource) continue;
        if (!heavy_import || balance.imported > heavy_import->imported) {
            heavy_import = &balance;
        }
    }
    if (heavy_import) {
        auto action = next_grow_step(
            cache, snapshot, heavy_import->resource, config, style);
        if (action) {
            ReeveCandidate candidate;
            candidate.id = "growth_reduce_import_" + heavy_import->resource;
            candidate.category = ReeveCategory::Growth;
            candidate.severity = ReeveSeverity::Watch;
            candidate.text = recommendation_text_for_action(*action);
            candidate.why = make_reeve_text("reeve_why_import_heavy", {
                {"resource", make_reeve_arg_text_id(resource_label_id(heavy_import->resource))},
                {"amount", round1(heavy_import->imported)}
            });
            candidate.action = *action;
            candidate.target = target_for_action(*action);
            add_candidate(candidates, std::move(candidate));
        }
    }

    return candidates;
}

std::vector<ReeveCandidate> build_connect_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style) {
    std::vector<ReeveCandidate> candidates;
    if (!has_road_morale_source(cache, *snapshot.fiefdom)) return candidates;
    (void)config;

    int added = 0;
    for (const auto& building : snapshot.buildings) {
        if (building.type == "road" || building.type == "head_race" ||
            building.type == "tail_race") {
            continue;
        }
        if (building.outputs.empty()) continue;
        if (building_touches_road(*snapshot.fiefdom, cache, building.building_id)) {
            continue;
        }
        auto road = find_road_link_to_building(
            cache, *snapshot.fiefdom, building.building_id,
            style.max_road_link, style.corridor_spacing);
        if (!road.needed || !road.found || road.cells.empty()) continue;

        ReeveCandidate candidate;
        candidate.id = "connect_building_" + std::to_string(building.building_id);
        candidate.category = ReeveCategory::Polish;
        candidate.severity = ReeveSeverity::Watch;
        candidate.text = make_reeve_text("reeve_rec_build_road");
        candidate.why = make_reeve_text("reeve_why_road");
        candidate.action.kind = ReeveActionKind::BuildRoadLink;
        candidate.action.cells = road.cells;
        candidate.action.building_type = building.type;
        candidate.target.kind = ReeveTargetKind::BuildingCard;
        candidate.target.building_id = building.building_id;
        add_candidate(candidates, std::move(candidate));
        if (++added >= 2) break;
    }
    return candidates;
}

std::vector<ReeveCandidate> build_polish_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style) {
    (void)style;
    std::vector<ReeveCandidate> candidates;
    int added = 0;

    // Outputs that the player has switched off.
    for (const auto& building : snapshot.fiefdom->buildings) {
        if (building.level <= 0) continue;
        auto config_opt = GameLogic::Validation::getBuildingConfig(cache, building.name);
        if (!config_opt) continue;
        const auto& building_config = *config_opt;
        if (!building_config.contains("outputs") || !building_config["outputs"].is_array()) {
            continue;
        }
        for (const auto& output : building_config["outputs"]) {
            if (!output.is_object()) continue;
            int min_level = output.value("min_level", 1);
            if (building.level < min_level) continue;
            std::string resource = output.value("resource", "");
            if (resource.empty()) continue;
            double rate = 1.0;
            if (building.output_rates.is_object() &&
                building.output_rates.contains(resource) &&
                building.output_rates[resource].is_number()) {
                rate = building.output_rates[resource].get<double>();
            }
            if (rate > 0.0001) continue;
            ReeveCandidate candidate;
            candidate.id = "polish_output_off_" + std::to_string(building.id) + "_" + resource;
            candidate.category = ReeveCategory::Polish;
            candidate.severity = ReeveSeverity::Watch;
            candidate.action.kind = ReeveActionKind::SetOutputRate;
            candidate.action.building_id = building.id;
            candidate.action.building_type = building.name;
            candidate.action.output = resource;
            candidate.action.rate = 1.0;
            candidate.text = make_reeve_text("reeve_rec_set_output_rate", {
                {"building", make_reeve_arg_text_id(building_label_id(building.name))},
                {"resource", make_reeve_arg_text_id(resource_label_id(resource))}
            });
            candidate.why = make_reeve_text("reeve_why_output_off");
            candidate.target.kind = ReeveTargetKind::BuildingCard;
            candidate.target.building_id = building.id;
            add_candidate(candidates, std::move(candidate));
            if (++added >= 2) break;
        }
        if (added >= 2) break;
    }

    // Surplus being sold above a reserve.
    for (const auto& balance : snapshot.balances) {
        if (added >= 2) break;
        if (balance.exported < config.thresholds.surplus_export_units) continue;
        if (balance.stock <= 0.0) continue;
        ReeveCandidate candidate;
        candidate.id = "polish_reserve_" + balance.resource;
        candidate.category = ReeveCategory::Polish;
        candidate.severity = ReeveSeverity::Watch;
        candidate.action.kind = ReeveActionKind::SetReserve;
        candidate.action.resource = balance.resource;
        candidate.action.value = round1(balance.stock);
        candidate.text = make_reeve_text("reeve_rec_set_reserve", {
            {"resource", make_reeve_arg_text_id(resource_label_id(balance.resource))},
            {"amount", candidate.action.value}
        });
        candidate.why = make_reeve_text("reeve_why_surplus_export", {
            {"resource", make_reeve_arg_text_id(resource_label_id(balance.resource))},
            {"amount", round1(balance.exported)}
        });
        candidate.target.kind = ReeveTargetKind::Resource;
        candidate.target.resource = balance.resource;
        add_candidate(candidates, std::move(candidate));
        ++added;
    }

    // Maxed stage with an available successor.
    added = 0;
    for (const auto& building : snapshot.fiefdom->buildings) {
        if (added >= 2) break;
        if (building.level <= 0) continue;
        double max_level = config_max_level(cache, building.name);
        if (building.level < static_cast<int>(max_level)) continue;
        if (!has_successor(cache, building.name)) continue;
        ReeveCandidate candidate;
        candidate.id = "polish_convert_" + std::to_string(building.id);
        candidate.category = ReeveCategory::Polish;
        candidate.severity = ReeveSeverity::Watch;
        candidate.action.kind = ReeveActionKind::Convert;
        candidate.action.building_id = building.id;
        candidate.action.building_type = building.name;
        candidate.text = make_reeve_text("reeve_rec_convert_stage", {
            {"building", make_reeve_arg_text_id(building_label_id(building.name))}
        });
        candidate.why = make_reeve_text("reeve_why_chain_ready", {
            {"building", make_reeve_arg_text_id(building_label_id(building.name))}
        });
        candidate.target.kind = ReeveTargetKind::BuildingCard;
        candidate.target.building_id = building.id;
        add_candidate(candidates, std::move(candidate));
        ++added;
    }

    return candidates;
}

std::vector<ReeveCandidate> build_reeve_candidates(
    GameConfigCache& cache,
    const ReeveSnapshot& snapshot,
    const ReeveConfig& config,
    const ReeveStyle& style) {
    std::vector<ReeveCandidate> candidates;
    auto append = [&](std::vector<ReeveCandidate> more) {
        for (auto& candidate : more) add_candidate(candidates, std::move(candidate));
    };
    append(build_safety_candidates(cache, snapshot, config, style));
    append(build_continuity_candidates(cache, snapshot, config, style));
    append(build_constraint_candidates(cache, snapshot, config, style));
    append(build_growth_candidates(cache, snapshot, config, style));
    append(build_connect_candidates(cache, snapshot, config, style));
    append(build_polish_candidates(cache, snapshot, config, style));
    return candidates;
}

double score_reeve_candidate(
    const ReeveCandidate& candidate,
    const ReeveStyle& style) {
    double severity_weight = 1.0;
    switch (candidate.severity) {
        case ReeveSeverity::Critical: severity_weight = 1000.0; break;
        case ReeveSeverity::Problem: severity_weight = 100.0; break;
        case ReeveSeverity::Watch: severity_weight = 10.0; break;
        case ReeveSeverity::Ok: severity_weight = 1.0; break;
    }
    double category_weight = 1.0;
    auto category_it = style.category_weights.find(category_to_string(candidate.category));
    if (category_it != style.category_weights.end()) category_weight = category_it->second;
    double action_weight = 1.0;
    auto action_it = style.action_weights.find(action_kind_to_string(candidate.action.kind));
    if (action_it != style.action_weights.end()) action_weight = action_it->second;
    double score = severity_weight * category_weight * action_weight;
    if (!candidate.intent_peer_id.empty() &&
        candidate.action.kind == ReeveActionKind::BuildRoadLink) {
        score *= style.road_first_weight;
    }
    return score;
}

std::optional<ReeveCandidate> select_reeve_candidate(
    const std::vector<ReeveCandidate>& candidates,
    const ReeveStyle& style) {
    if (candidates.empty()) return std::nullopt;

    auto in_band = [](const ReeveCandidate& candidate, int band) {
        if (band == 0) {
            return candidate.category == ReeveCategory::Safety &&
                   (candidate.severity == ReeveSeverity::Critical ||
                    candidate.severity == ReeveSeverity::Problem);
        }
        if (band == 1) {
            return candidate.category == ReeveCategory::Continuity &&
                   candidate.severity == ReeveSeverity::Problem;
        }
        return true;
    };

    for (int band = 0; band < 3; ++band) {
        std::vector<const ReeveCandidate*> pool;
        for (const auto& candidate : candidates) {
            if (in_band(candidate, band)) pool.push_back(&candidate);
        }
        if (pool.empty()) continue;

        std::sort(pool.begin(), pool.end(), [&](const auto* a, const auto* b) {
            double sa = score_reeve_candidate(*a, style);
            double sb = score_reeve_candidate(*b, style);
            if (sa != sb) return sa > sb;
            int ca = category_order(a->category);
            int cb = category_order(b->category);
            if (ca != cb) return ca < cb;
            if (a->id != b->id) return a->id < b->id;
            if (a->target.kind != b->target.kind) {
                return static_cast<int>(a->target.kind) < static_cast<int>(b->target.kind);
            }
            if (a->target.building_id != b->target.building_id) {
                return a->target.building_id < b->target.building_id;
            }
            return a->target.resource < b->target.resource;
        });
        return *pool.front();
    }
    return std::nullopt;
}

bool is_reeve_action_feasible(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const ReeveAction& action,
    std::string& error_code) {
    error_code.clear();
    GameLogic::ActionContext ctx;
    ctx.config_cache = &cache;
    ctx.requesting_fiefdom_id = fiefdom.id;
    ctx.requesting_character_id = fiefdom.owner_id;
    auto& registry = GameLogic::ActionRegistry::getInstance();

    auto build_level_one_costs = [&](const std::string& building_type) {
        nlohmann::json costs = nlohmann::json::object();
        auto config = GameLogic::Validation::getBuildingConfig(cache, building_type);
        if (!config) return costs;
        const std::pair<const char*, const char*> fields[] = {
            {"gold_cost", "gold"}, {"silver_pence_cost", "silver_pence"},
            {"wood_cost", "wood"}, {"stone_cost", "stone"},
            {"steel_cost", "steel"}, {"bronze_cost", "bronze"},
            {"grain_cost", "grain"}, {"leather_cost", "leather"},
            {"mana_cost", "mana"}, {"charcoal_cost", "charcoal"},
            {"iron_cost", "iron"}, {"ironwork_cost", "ironwork"},
            {"fancy_ironwork_cost", "fancy_ironwork"},
            {"beams_cost", "beams"}, {"boards_cost", "boards"}
        };
        for (const auto& [field, resource] : fields) {
            auto arr = GameLogic::Validation::getBuildingArrayField(
                cache, building_type, "", field);
            if (arr.is_array() && !arr.empty() && arr[0].is_number()) {
                double amount = arr[0].get<double>();
                if (amount > 0.0) costs[resource] = amount;
            }
        }
        return costs;
    };

    switch (action.kind) {
        case ReeveActionKind::Build: {
            nlohmann::json payload = {
                {"fiefdom_id", fiefdom.id},
                {"building_type", action.building_type},
                {"x", action.x},
                {"y", action.y},
                {"character_id", fiefdom.owner_id}
            };
            auto result = registry.validate("build", payload, ctx);
            if (result.status != GameLogic::ActionStatus::OK) {
                error_code = result.error_code.empty() ? "invalid_location" : result.error_code;
                return false;
            }
            auto costs = build_level_one_costs(action.building_type);
            if (!GameLogic::Validation::hasEnoughResources(cache, fiefdom.id, costs)) {
                error_code = "insufficient_resources";
                return false;
            }
            return true;
        }
        case ReeveActionKind::Upgrade: {
            nlohmann::json payload = {
                {"fiefdom_id", fiefdom.id},
                {"building_id", action.building_id},
                {"character_id", fiefdom.owner_id}
            };
            auto result = registry.validate("upgrade", payload, ctx);
            if (result.status != GameLogic::ActionStatus::OK) {
                error_code = result.error_code.empty() ? "invalid_config" : result.error_code;
                return false;
            }
            return true;
        }
        case ReeveActionKind::Convert: {
            nlohmann::json payload = {
                {"fiefdom_id", fiefdom.id},
                {"building_id", action.building_id},
                {"character_id", fiefdom.owner_id}
            };
            auto result = registry.validate("convert", payload, ctx);
            if (result.status != GameLogic::ActionStatus::OK) {
                error_code = result.error_code.empty() ? "invalid_config" : result.error_code;
                return false;
            }
            return true;
        }
        case ReeveActionKind::BuildRoadLink: {
            if (action.cells.empty()) {
                error_code = "invalid_location";
                return false;
            }
            nlohmann::json total_cost = build_level_one_costs("road");
            for (auto& [resource, value] : total_cost.items()) {
                (void)resource;
                if (value.is_number()) {
                    value = value.get<double>() * static_cast<double>(action.cells.size());
                }
            }
            for (const auto& [x, y] : action.cells) {
                nlohmann::json payload = {
                    {"fiefdom_id", fiefdom.id},
                    {"building_type", "road"},
                    {"x", x},
                    {"y", y},
                    {"character_id", fiefdom.owner_id}
                };
                auto result = registry.validate("build", payload, ctx);
                if (result.status != GameLogic::ActionStatus::OK) {
                    error_code = result.error_code.empty() ? "invalid_location" : result.error_code;
                    return false;
                }
            }
            if (!GameLogic::Validation::hasEnoughResources(cache, fiefdom.id, total_cost)) {
                error_code = "insufficient_resources";
                return false;
            }
            return true;
        }
        case ReeveActionKind::SetReserve:
            if (action.resource.empty()) {
                error_code = "insufficient_resources";
                return false;
            }
            if (!is_valid_resource(action.resource) || action.value < 0.0) {
                error_code = "invalid_resource";
                return false;
            }
            return true;
        case ReeveActionKind::SetImport:
            if (!is_valid_resource(action.resource)) {
                error_code = "invalid_resource";
                return false;
            }
            return true;
        case ReeveActionKind::SetOutputRate: {
            const BuildingData* building = find_building(fiefdom, action.building_id);
            if (!building) {
                error_code = "building_not_found";
                return false;
            }
            auto config = GameLogic::Validation::getBuildingConfig(cache, building->name);
            if (!config) {
                error_code = "invalid_config";
                return false;
            }
            bool found = false;
            int min_level = 1;
            if (config->contains("outputs") && (*config)["outputs"].is_array()) {
                for (const auto& output : (*config)["outputs"]) {
                    if (output.is_object() && output.value("resource", "") == action.output) {
                        found = true;
                        min_level = output.value("min_level", 1);
                        break;
                    }
                }
            }
            if (!found) {
                error_code = "invalid_output";
                return false;
            }
            if (building->level < min_level) {
                error_code = "output_locked";
                return false;
            }
            if (action.rate < 0.0 || action.rate > 1.0) {
                error_code = "invalid_rate";
                return false;
            }
            return true;
        }
    }
    error_code = "unknown_action";
    return false;
}

std::string reeve_error_text_id(const std::string& error_code) {
    if (error_code == "insufficient_resources") return "reeve_error_insufficient_resources";
    if (error_code == "insufficient_arable_land") return "reeve_error_insufficient_arable_land";
    if (error_code == "insufficient_forest_land") return "reeve_error_insufficient_forest_land";
    if (error_code == "prerequisites_not_met" || error_code == "dependencies_not_met") {
        return "reeve_error_prerequisites_not_met";
    }
    if (error_code == "exclusive_building_exists") return "reeve_error_exclusive_building_exists";
    if (error_code == "max_per_fiefdom_reached") return "reeve_error_max_per_fiefdom_reached";
    if (error_code == "home_base_required") return "reeve_error_home_base_required";
    if (error_code == "invalid_location") return "reeve_error_invalid_location";
    return "reeve_error_unknown";
}

std::string reeve_hold_text_id(const std::string& error_code) {
    if (error_code == "insufficient_resources") return "reeve_rec_hold_need_money";
    if (error_code == "insufficient_arable_land" ||
        error_code == "insufficient_forest_land") {
        return "reeve_rec_hold_need_land";
    }
    if (error_code == "invalid_location" || error_code == "max_per_fiefdom_reached") {
        return "reeve_rec_hold_need_space";
    }
    if (error_code == "prerequisites_not_met" || error_code == "dependencies_not_met") {
        return "reeve_rec_hold_need_prereq";
    }
    if (error_code == "exclusive_building_exists") return "reeve_rec_hold_exclusive";
    if (error_code == "home_base_required") return "reeve_rec_hold_manor_level";
    return "reeve_rec_hold_unknown";
}

ReeveReport build_reeve_report(
    GameConfigCache& cache,
    const FiefdomData& fiefdom,
    const nlohmann::json& economy_report,
    const ReeveConfig& config,
    const ReeveStyle& style) {
    ReeveReport report;
    report.style_tag = style.tag;

    ReeveSnapshot snapshot = build_reeve_snapshot(
        cache, fiefdom, economy_report, config);
    report.sections = build_reeve_sections(cache, snapshot, config);
    report.headline = build_reeve_headline(snapshot, report.sections);

    auto candidates = build_reeve_candidates(cache, snapshot, config, style);
    auto selected = select_reeve_candidate(candidates, style);
    if (!selected) {
        report.recommendation.kind = "none";
        report.recommendation.text = make_reeve_text("reeve_rec_none");
        report.recommendation.target.kind = ReeveTargetKind::EconomyPanel;
        return report;
    }

    report.recommendation.id = selected->id;
    report.recommendation.category = category_to_string(selected->category);
    report.recommendation.severity = selected->severity;
    report.recommendation.text = selected->text;
    report.recommendation.why = selected->why;
    report.recommendation.target = selected->target;
    report.recommendation.action = selected->action;

    std::string error_code;
    if (is_reeve_action_feasible(cache, fiefdom, selected->action, error_code)) {
        report.recommendation.kind = "action";
    } else {
        report.recommendation.kind = "hold";
        report.recommendation.reason_text_id = reeve_error_text_id(error_code);
        report.recommendation.text = make_reeve_text(reeve_hold_text_id(error_code));
        report.recommendation.target.kind = ReeveTargetKind::EconomyPanel;
        report.recommendation.action = ReeveAction{};
    }
    return report;
}

nlohmann::json reeve_text_to_json(const ReeveText& text) {
    nlohmann::json result = nlohmann::json::object();
    result["text_id"] = text.text_id;
    result["args"] = text.args.is_object() ? text.args : nlohmann::json::object();
    return result;
}

nlohmann::json reeve_action_to_json(const ReeveAction& action) {
    nlohmann::json result = nlohmann::json::object();
    result["kind"] = action_kind_to_string(action.kind);
    switch (action.kind) {
        case ReeveActionKind::Build:
            result["building_type"] = action.building_type;
            result["x"] = action.x;
            result["y"] = action.y;
            break;
        case ReeveActionKind::Upgrade:
        case ReeveActionKind::Convert:
            result["building_id"] = action.building_id;
            break;
        case ReeveActionKind::BuildRoadLink: {
            nlohmann::json cells = nlohmann::json::array();
            for (const auto& [x, y] : action.cells) {
                nlohmann::json cell = nlohmann::json::object();
                cell["x"] = x;
                cell["y"] = y;
                cells.push_back(std::move(cell));
            }
            result["cells"] = std::move(cells);
            break;
        }
        case ReeveActionKind::SetReserve:
            result["resource"] = action.resource;
            result["value"] = action.value;
            break;
        case ReeveActionKind::SetImport:
            result["resource"] = action.resource;
            result["enabled"] = action.enabled;
            break;
        case ReeveActionKind::SetOutputRate:
            result["building_id"] = action.building_id;
            result["output"] = action.output;
            result["rate"] = action.rate;
            break;
    }
    return result;
}

nlohmann::json reeve_target_to_json(const ReeveTarget& target) {
    nlohmann::json result = nlohmann::json::object();
    result["kind"] = target_kind_to_string(target.kind);
    switch (target.kind) {
        case ReeveTargetKind::BuildingCard:
            result["building_id"] = target.building_id;
            break;
        case ReeveTargetKind::Resource:
            result["resource"] = target.resource;
            break;
        case ReeveTargetKind::BuildPalette:
            result["building_type"] = target.building_type;
            break;
        case ReeveTargetKind::EconomyPanel:
        case ReeveTargetKind::ProductionPanel:
            break;
    }
    return result;
}

nlohmann::json reeve_report_to_json(const ReeveReport& report) {
    nlohmann::json result = nlohmann::json::object();
    result["advisor"] = {{"style_tag", report.style_tag}};
    result["headline"] = reeve_text_to_json(report.headline);

    nlohmann::json sections = nlohmann::json::array();
    for (const auto& section : report.sections) {
        nlohmann::json section_json = nlohmann::json::object();
        section_json["id"] = section.id;
        section_json["title"] = reeve_text_to_json(section.title);
        section_json["status"] = reeve_text_to_json(section.status);
        section_json["severity"] = severity_to_string(section.severity);
        section_json["target"] = reeve_target_to_json(section.target);
        nlohmann::json details = nlohmann::json::array();
        for (const auto& detail : section.details) {
            nlohmann::json detail_json = nlohmann::json::object();
            detail_json["text"] = reeve_text_to_json(detail.text);
            detail_json["severity"] = severity_to_string(detail.severity);
            detail_json["target"] = reeve_target_to_json(detail.target);
            details.push_back(std::move(detail_json));
        }
        section_json["details"] = std::move(details);
        sections.push_back(std::move(section_json));
    }
    result["sections"] = std::move(sections);

    nlohmann::json recommendation = nlohmann::json::object();
    recommendation["kind"] = report.recommendation.kind;
    recommendation["id"] = report.recommendation.id;
    recommendation["category"] = report.recommendation.category;
    recommendation["severity"] = severity_to_string(report.recommendation.severity);
    recommendation["text"] = reeve_text_to_json(report.recommendation.text);
    recommendation["why"] = reeve_text_to_json(report.recommendation.why);
    recommendation["target"] = reeve_target_to_json(report.recommendation.target);
    if (!report.recommendation.reason_text_id.empty()) {
        recommendation["reason_text_id"] = report.recommendation.reason_text_id;
    }
    if (report.recommendation.kind == "action") {
        recommendation["action"] = reeve_action_to_json(report.recommendation.action);
    }
    result["recommendation"] = std::move(recommendation);
    return result;
}

} // namespace Reeve
