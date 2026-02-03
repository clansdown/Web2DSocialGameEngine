#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <cstdint>
#include <string>

/**
 * @file common_types.h
 * @brief Common data types shared between client and server
 */

namespace Web2DEngine {

/**
 * @brief Player identifier
 */
using PlayerID = uint32_t;

/**
 * @brief Entity identifier
 */
using EntityID = uint32_t;

/**
 * @brief Vector2D structure
 */
struct Vector2D {
    float x;
    float y;
    
    Vector2D() : x(0.0f), y(0.0f) {}
    Vector2D(float _x, float _y) : x(_x), y(_y) {}
};

/**
 * @brief Player information
 */
struct PlayerInfo {
    PlayerID id;
    std::string name;
    Vector2D position;
    int health;
    int score;
};

/**
 * @brief Sprite information
 */
struct SpriteInfo {
    EntityID id;
    Vector2D position;
    float rotation;
    float scale;
    std::string texturePath;
};

} // namespace Web2DEngine

#endif // COMMON_TYPES_H
