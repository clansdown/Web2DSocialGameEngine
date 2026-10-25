#pragma once
// Registration/invitation HTTP handlers.
//
// getRegistrationConfig is public (dispatched before authentication). The
// player and staff endpoints are registered into the normal authenticated map.

#include <unordered_map>

#include "ApiHandlers.hpp"

ApiResponse handleGetRegistrationConfig(GameConfigCache& config_cache,
                                        const nlohmann::json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token);

void register_invitation_handlers(std::unordered_map<std::string, ApiHandler>& handlers);
