#pragma once
#include "mini_game_handler.hpp"

class TowerDefenseHandler : public MiniGameHandler {
public:
    std::string name() const override;

    nlohmann::json start_level(const MiniGameContext& ctx) override;

    nlohmann::json end_level(const MiniGameContext& ctx, const MiniGameResult& result) override;

    nlohmann::json get_config() const override;

    bool validate_prerequisites(int character_id, int level_id) const override;
};
