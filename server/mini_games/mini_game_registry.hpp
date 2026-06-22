#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "mini_game_handler.hpp"

class MiniGameRegistry {
public:
    static MiniGameRegistry& getInstance();

    void register_handler(std::unique_ptr<MiniGameHandler> handler);

    MiniGameHandler* get_handler(const std::string& name) const;

    bool has_handler(const std::string& name) const;

    std::vector<std::string> get_available_games() const;

private:
    MiniGameRegistry() = default;
    MiniGameRegistry(const MiniGameRegistry&) = delete;
    MiniGameRegistry& operator=(const MiniGameRegistry&) = delete;

    std::unordered_map<std::string, std::unique_ptr<MiniGameHandler>> handlers_;
};

void register_all_mini_game_handlers();
