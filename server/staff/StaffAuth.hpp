#pragma once
// Staff authorization helpers. UI visibility is cosmetic; every staff endpoint
// calls require_capability server-side.

#include <cstdint>
#include <optional>
#include <string>

#include "ApiResponse.hpp"
#include "staff/StaffTypes.hpp"

namespace staff {

std::optional<int64_t> account_id_for_username(const std::string& username);
std::optional<std::string> username_for_account(int64_t account_id);
bool character_account_id(int64_t character_id, int64_t& account_id);
staff_context context_for_username(const std::optional<std::string>& username);

bool require_capability(const staff_context& context, const std::string& capability,
                        ApiResponse& response);
bool staff_can_view_chat(const staff_context& context);
bool staff_can_moderate_chat(const staff_context& context);

bool account_is_social_restricted(int64_t account_id);
bool account_is_support_locked(int64_t account_id);
bool character_is_social_restricted(int64_t character_id);

}  // namespace staff
