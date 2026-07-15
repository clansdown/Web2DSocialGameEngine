#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include "GameConfigCache.hpp"

struct MiniGameContext {
    int character_id;
    int level_id;
    bool is_random_generation;
};

struct MiniGameResult {
    bool won;
    int score;
};

class MiniGameHandler {
public:
    virtual ~MiniGameHandler() = default;
    virtual std::string name() const = 0;
    virtual nlohmann::json start_level(const MiniGameContext& ctx) = 0;
    virtual nlohmann::json end_level(const MiniGameContext& ctx, const MiniGameResult& result) = 0;
    virtual nlohmann::json get_config() const = 0;
    virtual bool validate_prerequisites(int character_id, int level_id) const = 0;
};

class TowerDefenseHandler : public MiniGameHandler {
    GameConfigCache& config_cache_;
public:
    explicit TowerDefenseHandler(GameConfigCache& cache) : config_cache_(cache) {}
    std::string name() const override;
    nlohmann::json start_level(const MiniGameContext& ctx) override;
    nlohmann::json end_level(const MiniGameContext& ctx, const MiniGameResult& result) override;
    nlohmann::json get_config() const override;
    bool validate_prerequisites(int character_id, int level_id) const override;
};

class WeedingHandler : public MiniGameHandler {
    GameConfigCache& config_cache_;
public:
    explicit WeedingHandler(GameConfigCache& cache) : config_cache_(cache) {}
    std::string name() const override;
    nlohmann::json start_level(const MiniGameContext& ctx) override;
    nlohmann::json end_level(const MiniGameContext& ctx, const MiniGameResult& result) override;
    nlohmann::json get_config() const override;
    bool validate_prerequisites(int character_id, int level_id) const override;
};

class MiniGames {
    GameConfigCache config_cache_;
    TowerDefenseHandler tower_defense_{config_cache_};
    WeedingHandler weeding_{config_cache_};
public:
    bool initialize(const std::string& config_dir) {
        return config_cache_.initialize(config_dir);
    }
    GameConfigCache& config_cache() { return config_cache_; }
    MiniGameHandler* get(const std::string& name) {
        if (name == "tower_defense") return &tower_defense_;
        if (name == "weeding") return &weeding_;
        return nullptr;
    }
};
