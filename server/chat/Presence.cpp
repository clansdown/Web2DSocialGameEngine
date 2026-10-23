#include "chat/Presence.hpp"

namespace chat {

void Presence::purge_locked(int64_t now) {
    for (auto it = last_seen_.begin(); it != last_seen_.end();) {
        if (now - it->second > online_ttl_seconds * 4) {
            it = last_seen_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = typing_.begin(); it != typing_.end();) {
        if (now - it->second > typing_ttl_seconds) {
            it = typing_.erase(it);
        } else {
            ++it;
        }
    }
}

void Presence::heartbeat(int64_t character_id, int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    purge_locked(now);
    last_seen_[character_id] = now;
}

bool Presence::is_online(int64_t character_id, int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = last_seen_.find(character_id);
    if (it == last_seen_.end()) return false;
    return now - it->second <= online_ttl_seconds;
}

void Presence::set_typing(int64_t character_id, const std::string& chat_id, int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    purge_locked(now);
    last_seen_[character_id] = now;
    typing_[{character_id, chat_id}] = now;
}

bool Presence::is_typing(int64_t character_id, const std::string& chat_id, int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = typing_.find({character_id, chat_id});
    if (it == typing_.end()) return false;
    return now - it->second <= typing_ttl_seconds;
}

std::vector<std::string> Presence::typing_chats(int64_t character_id, int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> chats;
    for (const auto& [key, timestamp] : typing_) {
        if (key.first == character_id && now - timestamp <= typing_ttl_seconds) {
            chats.push_back(key.second);
        }
    }
    return chats;
}

void Presence::forget(int64_t character_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_seen_.erase(character_id);
    for (auto it = typing_.begin(); it != typing_.end();) {
        if (it->first.first == character_id) {
            it = typing_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace chat
