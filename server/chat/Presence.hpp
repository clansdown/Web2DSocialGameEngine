#pragma once
// In-memory presence + typing state. Never persisted: a restart simply drops
// everyone offline. Presence is friends-only and DM-scoped by the caller.
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chat {

class Presence {
public:
    static Presence& instance() {
        static Presence inst;
        return inst;
    }

    // Online TTL: a character is online while its last heartbeat is fresh.
    static constexpr int64_t online_ttl_seconds = 60;
    // Typing TTL: a typing flag lives ~5 seconds.
    static constexpr int64_t typing_ttl_seconds = 5;

    void heartbeat(int64_t character_id, int64_t now);
    bool is_online(int64_t character_id, int64_t now);

    void set_typing(int64_t character_id, const std::string& chat_id, int64_t now);
    bool is_typing(int64_t character_id, const std::string& chat_id, int64_t now);
    // Chats in which the character is currently typing.
    std::vector<std::string> typing_chats(int64_t character_id, int64_t now);

    void forget(int64_t character_id);

private:
    Presence() = default;
    void purge_locked(int64_t now);

    std::mutex mutex_;
    std::unordered_map<int64_t, int64_t> last_seen_;
    std::map<std::pair<int64_t, std::string>, int64_t> typing_;
};

}  // namespace chat
