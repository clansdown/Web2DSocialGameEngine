#pragma once
#include <nlohmann/json.hpp>
#include <string>

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
