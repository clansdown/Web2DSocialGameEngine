#ifndef GAME_WORLD_H
#define GAME_WORLD_H

/**
 * @file game_world.h
 * @brief Game world and logic management
 */

namespace Web2DEngine {
namespace Server {

/**
 * @brief Manages the game world state and logic
 */
class GameWorld {
public:
    GameWorld();
    ~GameWorld();
    
    /**
     * @brief Initialize the game world
     * @return true if successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Update the game world (called each tick)
     * @param deltaTime Time elapsed since last update in seconds
     */
    void update(float deltaTime);
    
    /**
     * @brief Shutdown the game world
     */
    void shutdown();
};

} // namespace Server
} // namespace Web2DEngine

#endif // GAME_WORLD_H
