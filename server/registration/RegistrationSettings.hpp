#pragma once
// Typed view over game/config/registration.json.
//
// The raw JSON is server-only because it may contain the shared global
// registration code. Public endpoints expose only mode/flags.

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

enum class registration_mode {
    off,
    global_code,
    invite_only,
};

struct rate_limit_settings {
    bool enabled = true;
    int window_seconds = 3600;
    int max_attempts_per_network = 300;
    int ipv6_prefix_bits = 64;
    std::size_t max_entries = 10000;
    int prune_interval_seconds = 60;
};

struct registration_settings {
    registration_mode mode = registration_mode::off;
    std::string global_code;
    bool players_can_generate = false;
    int player_daily_limit = 0;
    int player_lifetime_limit = 0;
    rate_limit_settings rate_limit;
};

registration_settings parse_registration_settings(const nlohmann::json& config);
const char* registration_mode_name(registration_mode mode);
bool registration_mode_from_string(const std::string& value, registration_mode& out);
