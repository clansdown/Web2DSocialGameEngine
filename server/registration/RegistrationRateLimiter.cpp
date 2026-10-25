#include "registration/RegistrationRateLimiter.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

std::string trim_copy(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string first_forwarded_address(const std::string& header) {
    const std::size_t comma = header.find(',');
    return trim_copy(header.substr(0, comma));
}

std::string ipv4_key(const in_addr& address) {
    char buffer[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &address, buffer, sizeof(buffer)) == nullptr) {
        return std::string();
    }
    return std::string("v4:") + buffer;
}

std::string ipv6_key(const in6_addr& address, int prefix_bits) {
    in6_addr masked = address;
    const int bits = std::clamp(prefix_bits, 1, 128);
    for (int i = 0; i < 16; ++i) {
        const int bit_start = i * 8;
        if (bit_start >= bits) {
            masked.s6_addr[i] = 0;
        } else if (bit_start + 8 > bits) {
            const int keep = bits - bit_start;
            const unsigned char mask = static_cast<unsigned char>(0xFF << (8 - keep));
            masked.s6_addr[i] =
                static_cast<unsigned char>(masked.s6_addr[i] & mask);
        }
    }
    char buffer[INET6_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET6, &masked, buffer, sizeof(buffer)) == nullptr) {
        return std::string();
    }
    return std::string("v6:") + buffer;
}

}  // namespace

registration_rate_limiter& registration_rate_limiter::instance() {
    static registration_rate_limiter limiter;
    return limiter;
}

std::string rate_limit_network_key(const ClientInfo& client, int ipv6_prefix_bits) {
    std::string value = trim_copy(client.real_ip);
    if (value.empty()) {
        value = first_forwarded_address(client.forwarded_for);
    }
    if (value.empty()) return "unknown";

    in_addr v4{};
    if (inet_pton(AF_INET, value.c_str(), &v4) == 1) {
        return ipv4_key(v4);
    }

    in6_addr v6{};
    if (inet_pton(AF_INET6, value.c_str(), &v6) == 1) {
        if (IN6_IS_ADDR_V4MAPPED(&v6)) {
            in_addr mapped{};
            std::memcpy(&mapped, &v6.s6_addr[12], sizeof(mapped));
            return ipv4_key(mapped);
        }
        return ipv6_key(v6, ipv6_prefix_bits);
    }

    return "raw:" + value;
}

bool registration_rate_limiter::allow(const ClientInfo& client,
                                      const rate_limit_settings& settings,
                                      std::int64_t now_ms,
                                      std::int64_t& retry_after_seconds) {
    retry_after_seconds = 0;
    if (!settings.enabled) {
        entries_.clear();
        lru_.clear();
        return true;
    }

    const std::int64_t window_ms =
        static_cast<std::int64_t>(std::max(1, settings.window_seconds)) * 1000;
    const std::size_t max_entries = std::max<std::size_t>(1, settings.max_entries);
    const std::int64_t prune_ms =
        static_cast<std::int64_t>(std::max(1, settings.prune_interval_seconds)) * 1000;

    if (now_ms - last_prune_ms_ >= prune_ms) {
        prune_expired(now_ms, window_ms);
        last_prune_ms_ = now_ms;
    }

    while (entries_.size() > max_entries) {
        evict_lru();
    }

    const std::string key = rate_limit_network_key(client, settings.ipv6_prefix_bits);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        entry& current = it->second;
        if (now_ms - current.window_start_ms >= window_ms) {
            current.window_start_ms = now_ms;
            current.count = 0;
        }
        ++current.count;
        touch(it);
        if (current.count > settings.max_attempts_per_network) {
            const std::int64_t remaining_ms =
                current.window_start_ms + window_ms - now_ms;
            retry_after_seconds = std::max<std::int64_t>(1, remaining_ms / 1000);
            return false;
        }
        return true;
    }

    if (entries_.size() >= max_entries) {
        prune_expired(now_ms, window_ms);
        while (entries_.size() >= max_entries) {
            evict_lru();
        }
    }

    lru_.push_front(key);
    entry fresh;
    fresh.window_start_ms = now_ms;
    fresh.count = 1;
    fresh.lru_position = lru_.begin();
    entries_.emplace(key, fresh);
    return true;
}

void registration_rate_limiter::touch(
    std::unordered_map<std::string, entry>::iterator it) {
    lru_.splice(lru_.begin(), lru_, it->second.lru_position);
    it->second.lru_position = lru_.begin();
}

void registration_rate_limiter::prune_expired(std::int64_t now_ms,
                                              std::int64_t window_ms) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now_ms - it->second.window_start_ms >= window_ms) {
            lru_.erase(it->second.lru_position);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void registration_rate_limiter::evict_lru() {
    if (lru_.empty()) return;
    const std::string victim = lru_.back();
    lru_.pop_back();
    entries_.erase(victim);
}
