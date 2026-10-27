#pragma once

#include "ApiHandlers.hpp"

ApiResponse handleGetReeveReport(
    GameConfigCache& config_cache,
    const nlohmann::json& body,
    const std::optional<std::string>& username,
    const ClientInfo& client,
    const std::optional<std::string>& new_token);
