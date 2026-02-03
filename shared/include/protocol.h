#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>

/**
 * @file protocol.h
 * @brief Network protocol definitions for client-server communication
 */

namespace Web2DEngine {
namespace Protocol {

/**
 * @brief Message types for client-server communication
 */
enum class MessageType : uint8_t {
    CONNECT = 0,
    DISCONNECT,
    PLAYER_MOVE,
    PLAYER_ACTION,
    CHAT_MESSAGE,
    WORLD_UPDATE,
    PLAYER_JOIN,
    PLAYER_LEAVE
};

/**
 * @brief Base message header
 */
struct MessageHeader {
    MessageType type;
    uint32_t size;
};

/**
 * @brief Player position data
 */
struct PlayerPosition {
    float x;
    float y;
    float rotation;
};

} // namespace Protocol
} // namespace Web2DEngine

#endif // PROTOCOL_H
