#pragma once
// Registration/invitation business rules. Keeps code redemption, player
// generation limits, and status shaping out of the HTTP handlers.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "registration/InvitationStore.hpp"
#include "registration/RegistrationSettings.hpp"

enum class registration_code_kind {
    none,
    global_code,
    individual_code,
};

struct registration_code_resolution {
    bool ok = false;
    registration_code_kind kind = registration_code_kind::none;
    std::string invitation_code_id;
    std::string error;
};

struct invitation_allowance {
    bool enabled = false;
    bool can_generate = false;
    int daily_limit = 0;
    int lifetime_limit = 0;
    std::int64_t generated_today = 0;
    std::int64_t generated_total = 0;
    std::int64_t daily_remaining = 0;
    std::int64_t lifetime_remaining = 0;
};

struct invitation_status {
    registration_mode mode = registration_mode::off;
    bool invite_required = false;
    bool players_can_generate = false;
    invitation_allowance allowance;
    std::vector<invitation_code_record> codes;
};

bool registration_code_required(const registration_settings& settings);

registration_code_resolution resolve_registration_code(
    const registration_settings& settings, const std::string& submitted_code);

invitation_allowance get_invitation_allowance(std::int64_t account_id,
                                              const registration_settings& settings);

invitation_status get_invitation_status(std::int64_t account_id,
                                        const registration_settings& settings);

std::optional<invitation_code_record> generate_for_player(
    std::int64_t account_id, const registration_settings& settings, std::string& error);
