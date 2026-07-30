#pragma once
#include "ActionHandler.hpp"
#include <map>
#include <unordered_map>

namespace GameLogic {

class BuildActionHandler : public ActionHandler {
public:
    ActionResult validate(const nlohmann::json& payload, const ActionContext& ctx) override;
    ActionResult execute(const nlohmann::json& payload, const ActionContext& ctx) override;
    std::string getDescription() const override { return "Build/upgrade structures"; }
};

class BuildWallActionHandler : public ActionHandler {
public:
    ActionResult validate(const nlohmann::json& payload, const ActionContext& ctx) override;
    ActionResult execute(const nlohmann::json& payload, const ActionContext& ctx) override;
    std::string getDescription() const override { return "Build/upgrade walls"; }
};

class TrainTroopsActionHandler : public ActionHandler {
public:
    ActionResult validate(const nlohmann::json& payload, const ActionContext& ctx) override;
    ActionResult execute(const nlohmann::json& payload, const ActionContext& ctx) override;
    std::string getDescription() const override { return "Train combatants"; }
};

class ResearchMagicActionHandler : public ActionHandler {
public:
    ActionResult validate(const nlohmann::json& payload, const ActionContext& ctx) override;
    ActionResult execute(const nlohmann::json& payload, const ActionContext& ctx) override;
    std::string getDescription() const override { return "Research magic"; }
};

class ResearchTechActionHandler : public ActionHandler {
public:
    ActionResult validate(const nlohmann::json& payload, const ActionContext& ctx) override;
    ActionResult execute(const nlohmann::json& payload, const ActionContext& ctx) override;
    std::string getDescription() const override { return "Research technology"; }
};

class UpgradeActionHandler : public ActionHandler {
public:
    ActionResult validate(const nlohmann::json& payload, const ActionContext& ctx) override;
    ActionResult execute(const nlohmann::json& payload, const ActionContext& ctx) override;
    std::string getDescription() const override { return "Upgrade buildings and walls"; }
};

void registerAllActionHandlers(ActionRegistry& registry);

namespace Validation {

std::optional<nlohmann::json> getPrerequisitesForLevel(
    const std::string& building_type,
    int target_level
);

int getBuildingLevelInFiefdom(int fiefdom_id, const std::string& building_name);

int getFiefdomManorLevel(int fiefdom_id);

bool checkFiefdomPrerequisites(int fiefdom_id, const nlohmann::json& prerequisites);

bool hasCompletedHomeBase(int fiefdom_id);

nlohmann::json getDependenciesForLevel(
    GameConfigCache& cache,
    const std::string& building_type,
    int target_level
);

int countBuildingsByType(int fiefdom_id, const std::string& target_building, int min_level);

std::map<std::string, std::pair<int, int>> aggregateFiefdomDependencies(
    GameConfigCache& cache,
    int fiefdom_id,
    const std::vector<BuildingData>& buildings,
    const nlohmann::json& additional_deps
);

std::pair<bool, std::string> checkBuildingDependencies(
    GameConfigCache& cache,
    int fiefdom_id,
    const std::vector<BuildingData>& buildings,
    const nlohmann::json& deps_to_check
);

std::vector<BuildingData> getFiefdomAllBuildings(int fiefdom_id);

} // namespace Validation

} // namespace GameLogic