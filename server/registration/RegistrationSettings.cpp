#include "registration/RegistrationSettings.hpp"

#include <iostream>

namespace {

int non_negative_int(const nlohmann::json& value, const char* key, int fallback) {
    if (!value.is_object()) return fallback;
    auto it = value.find(key);
    if (it == value.end() || !it->is_number_integer()) return fallback;
    const int parsed = it->get<int>();
    return parsed < 0 ? fallback : parsed;
}

std::size_t positive_size(const nlohmann::json& value, const char* key,
                          std::size_t fallback) {
    if (!value.is_object()) return fallback;
    auto it = value.find(key);
    if (it == value.end() || !it->is_number_integer()) return fallback;
    const long long parsed = it->get<long long>();
    return parsed <= 0 ? fallback : static_cast<std::size_t>(parsed);
}

}  // namespace

const char* registration_mode_name(registration_mode mode) {
    switch (mode) {
        case registration_mode::off: return "off";
        case registration_mode::global_code: return "global_code";
        case registration_mode::invite_only: return "invite_only";
    }
    return "off";
}

bool registration_mode_from_string(const std::string& value, registration_mode& out) {
    if (value == "off") {
        out = registration_mode::off;
        return true;
    }
    if (value == "global_code") {
        out = registration_mode::global_code;
        return true;
    }
    if (value == "invite_only") {
        out = registration_mode::invite_only;
        return true;
    }
    return false;
}

registration_settings parse_registration_settings(const nlohmann::json& config) {
    registration_settings settings;
    if (!config.is_object()) {
        // Fail closed: bad/missing config must never silently open registration.
        settings.mode = registration_mode::invite_only;
        settings.players_can_generate = false;
        std::cerr << "registration.json missing or invalid; registration is closed"
                  << std::endl;
        return settings;
    }

    std::string mode_name = "invalid";
    if (config.contains("mode") && config["mode"].is_string()) {
        mode_name = config["mode"].get<std::string>();
    }
    if (!registration_mode_from_string(mode_name, settings.mode)) {
        // Fail closed on a missing/typo mode rather than falling back to `off`
        // (which would open registration to everyone).
        settings.mode = registration_mode::invite_only;
        std::cerr << "registration.json has invalid mode '" << mode_name
                  << "'; registration is closed" << std::endl;
    }

    if (config.contains("global_code") && config["global_code"].is_string()) {
        settings.global_code = config["global_code"].get<std::string>();
    }
    if (config.contains("players_can_generate") &&
        config["players_can_generate"].is_boolean()) {
        settings.players_can_generate = config["players_can_generate"].get<bool>();
    }
    settings.player_daily_limit = non_negative_int(config, "player_daily_limit", 0);
    settings.player_lifetime_limit = non_negative_int(config, "player_lifetime_limit", 0);

    const auto rate_it = config.find("create_account_rate_limit");
    if (rate_it != config.end() && rate_it->is_object()) {
        const nlohmann::json& rate = *rate_it;
        if (rate.contains("enabled") && rate["enabled"].is_boolean()) {
            settings.rate_limit.enabled = rate["enabled"].get<bool>();
        }
        settings.rate_limit.window_seconds =
            non_negative_int(rate, "window_seconds", settings.rate_limit.window_seconds);
        settings.rate_limit.max_attempts_per_network = non_negative_int(
            rate, "max_attempts_per_network", settings.rate_limit.max_attempts_per_network);
        const int prefix = non_negative_int(
            rate, "ipv6_prefix_bits", settings.rate_limit.ipv6_prefix_bits);
        settings.rate_limit.ipv6_prefix_bits = (prefix >= 1 && prefix <= 128) ? prefix : 64;
        settings.rate_limit.max_entries = positive_size(
            rate, "max_entries", settings.rate_limit.max_entries);
        settings.rate_limit.prune_interval_seconds = non_negative_int(
            rate, "prune_interval_seconds", settings.rate_limit.prune_interval_seconds);
    }

    if (settings.rate_limit.window_seconds <= 0) settings.rate_limit.window_seconds = 3600;
    if (settings.rate_limit.max_attempts_per_network <= 0) {
        settings.rate_limit.max_attempts_per_network = 300;
    }
    if (settings.rate_limit.max_entries == 0) settings.rate_limit.max_entries = 1;
    if (settings.rate_limit.prune_interval_seconds <= 0) {
        settings.rate_limit.prune_interval_seconds = 60;
    }

    return settings;
}
