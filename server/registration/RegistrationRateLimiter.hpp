#pragma once
// In-memory account-creation rate limiter. This is a speed bump, not the real
// guessing defence (invitation-code entropy is). Bounded by max_entries with
// expired-entry purge + LRU eviction. Loop-thread-only; no mutex.

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

#include "ApiHandlers.hpp"
#include "registration/RegistrationSettings.hpp"

class registration_rate_limiter {
public:
    static registration_rate_limiter& instance();

    bool allow(const ClientInfo& client, const rate_limit_settings& settings,
               std::int64_t now_ms, std::int64_t& retry_after_seconds);

    std::size_t size() const { return entries_.size(); }

private:
    registration_rate_limiter() = default;

    struct entry {
        std::int64_t window_start_ms = 0;
        std::int32_t count = 0;
        std::list<std::string>::iterator lru_position;
    };

    void touch(std::unordered_map<std::string, entry>::iterator it);
    void prune_expired(std::int64_t now_ms, std::int64_t window_ms);
    void evict_lru();

    std::unordered_map<std::string, entry> entries_;
    std::list<std::string> lru_;  // front = most recently used
    std::int64_t last_prune_ms_ = 0;
};

std::string rate_limit_network_key(const ClientInfo& client, int ipv6_prefix_bits);
