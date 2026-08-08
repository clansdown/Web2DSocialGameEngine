#include "CombatCodec.hpp"
#include <stdexcept>

namespace combat {

// Placeholder: the binary codec slot exists so the transport can prove the
// codec interface is swappable. Implement when bandwidth profiling demands it
// (see docs/combat_protocol.md for the wire-neutral message contract).

std::string binary_codec::serialize_message(const combat_message& /*msg*/) const {
    throw std::runtime_error("combat binary codec is not implemented yet");
}

combat_message binary_codec::deserialize_message(std::string_view /*wire*/) const {
    throw std::runtime_error("combat binary codec is not implemented yet");
}

} // namespace combat
