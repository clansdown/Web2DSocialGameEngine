#pragma once
#include "ActionHandler.hpp"
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

void registerAllActionHandlers(ActionRegistry& registry);

} // namespace GameLogic