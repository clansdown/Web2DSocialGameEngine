#include "mini_game_registry.hpp"
#include "tower_defense_handler.hpp"
#include "weeding_handler.hpp"
#include <iostream>

MiniGameRegistry& MiniGameRegistry::getInstance() {
    static MiniGameRegistry instance;
    return instance;
}

void MiniGameRegistry::register_handler(std::unique_ptr<MiniGameHandler> handler) {
    std::string name = handler->name();
    handlers_[name] = std::move(handler);
}

MiniGameHandler* MiniGameRegistry::get_handler(const std::string& name) const {
    auto it = handlers_.find(name);
    if (it != handlers_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool MiniGameRegistry::has_handler(const std::string& name) const {
    return handlers_.find(name) != handlers_.end();
}

std::vector<std::string> MiniGameRegistry::get_available_games() const {
    std::vector<std::string> names;
    names.reserve(handlers_.size());
    for (const auto& pair : handlers_) {
        names.push_back(pair.first);
    }
    return names;
}

void register_all_mini_game_handlers() {
    auto& registry = MiniGameRegistry::getInstance();

    registry.register_handler(std::make_unique<TowerDefenseHandler>());
    registry.register_handler(std::make_unique<WeedingHandler>());

    std::cerr << "[mini_games] Registered handlers: ";
    bool first = true;
    for (const auto& name : registry.get_available_games()) {
        if (!first) std::cerr << ", ";
        std::cerr << name;
        first = false;
    }
    std::cerr << std::endl;
}
