#pragma once
// Shared primitives for the chat subsystem. See
// /home/ctl/.opencode/plan/chat-system-implementation-plan.md and
// server/docs/chat.md for the design.
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>
#include <random>
#include <chrono>
#include <stdexcept>

namespace chat {

using json = nlohmann::json;

// A chat error carries a stable machine code (returned as `{ "error": code }`).
// The HTTP layer catches this and never leaks internal details.
struct chat_error : std::runtime_error {
    explicit chat_error(const std::string& code) : std::runtime_error(code) {}
};

inline int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Random, collision-resistant identifier. The server is the sole writer and
// runs HTTP handlers on one thread, so a thread_local PRNG is sufficient (this
// is not used for anything security-sensitive).
inline std::string generate_id(const std::string& prefix) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static const char* hex = "0123456789abcdef";
    std::string out = prefix;
    out.reserve(prefix.size() + 32);
    for (int i = 0; i < 32; ++i) {
        out.push_back(hex[static_cast<size_t>(rng() & 0xF)]);
    }
    return out;
}

enum class chat_type { direct, group, barony, dukedom, wildlands };

inline const char* chat_type_to_string(chat_type t) {
    switch (t) {
        case chat_type::direct: return "direct";
        case chat_type::group: return "group";
        case chat_type::barony: return "barony";
        case chat_type::dukedom: return "dukedom";
        case chat_type::wildlands: return "wildlands";
    }
    return "direct";
}

inline chat_type chat_type_from_string(const std::string& s) {
    if (s == "group") return chat_type::group;
    if (s == "barony") return chat_type::barony;
    if (s == "dukedom") return chat_type::dukedom;
    if (s == "wildlands") return chat_type::wildlands;
    return chat_type::direct;
}

inline std::string make_direct_key(int a, int b) {
    int lo = a < b ? a : b;
    int hi = a < b ? b : a;
    return std::to_string(lo) + ":" + std::to_string(hi);
}

}  // namespace chat
