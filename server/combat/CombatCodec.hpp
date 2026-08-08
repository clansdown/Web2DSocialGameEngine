#pragma once
#include <string>
#include <string_view>
#include "CombatTypes.hpp"

namespace combat {

// Wire codec. Game logic builds combat_message structs; a codec converts them
// to/from wire bytes. json_codec is the default (hand-rolled fast serializer
// for the hot state path); binary codecs can be added later without touching
// simulation or transport. See docs/combat_protocol.md.
class combat_codec {
public:
    virtual ~combat_codec() = default;
    virtual std::string format_id() const = 0;
    virtual std::string serialize_message(const combat_message& msg) const = 0;
    // Deserializes inbound messages (auth/command/chat/voice/ready/leave/request_state).
    // Throws nlohmann::json::exception on malformed input; callers handle it.
    virtual combat_message deserialize_message(std::string_view wire) const = 0;
};

// Default JSON codec. Envelope:
//   {"v":1,"fmt":"json","type":"...","match_id":"...","tick":N,"payload":{...}}
class json_codec final : public combat_codec {
public:
    std::string format_id() const override { return "json"; }
    std::string serialize_message(const combat_message& msg) const override;
    combat_message deserialize_message(std::string_view wire) const override;
};

// Placeholder binary codec — the slot exists so the hot-path serializer is
// provably swappable. Throws on use until implemented.
class binary_codec final : public combat_codec {
public:
    std::string format_id() const override { return "binary"; }
    std::string serialize_message(const combat_message& msg) const override;
    combat_message deserialize_message(std::string_view wire) const override;
};

} // namespace combat
