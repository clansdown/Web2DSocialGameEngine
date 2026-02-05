#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include "ApiResponse.hpp"

struct ClientInfo {
    std::string_view real_ip;
    std::string_view forwarded_for;
    std::string_view forwarded_proto;
    std::string_view forwarded_host;
    std::string_view forwarded_port;
    std::string_view user_agent;
    std::string_view host;
    std::string_view request_id;
};

inline ClientInfo parseClientHeaders(auto* req) {
    ClientInfo info;
    info.real_ip = req->getHeader("x-real-ip");
    info.forwarded_for = req->getHeader("x-forwarded-for");
    info.forwarded_proto = req->getHeader("x-forwarded-proto");
    info.forwarded_host = req->getHeader("x-forwarded-host");
    info.forwarded_port = req->getHeader("x-forwarded-port");
    info.user_agent = req->getHeader("user-agent");
    info.host = req->getHeader("host");
    info.request_id = req->getHeader("x-request-id");
    return info;
}

using ApiHandler = std::function<ApiResponse(
    const nlohmann::json&,
    const std::optional<std::string>&,
    const ClientInfo&,
    const std::optional<std::string>&
)>;

ApiResponse handleLogin(const nlohmann::json& body,
                       const std::optional<std::string>& username,
                       const ClientInfo& client,
                       const std::optional<std::string>& new_token);

ApiResponse handleGetPlayer(const nlohmann::json& body,
                            const std::optional<std::string>& username,
                            const ClientInfo& client,
                            const std::optional<std::string>& new_token);

ApiResponse handleBuild(const nlohmann::json& body,
                        const std::optional<std::string>& username,
                        const ClientInfo& client,
                        const std::optional<std::string>& new_token);

ApiResponse handleGetWorld(const nlohmann::json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token);

ApiResponse handleGetFiefdom(const nlohmann::json& body,
                            const std::optional<std::string>& username,
                            const ClientInfo& client,
                            const std::optional<std::string>& new_token);

ApiResponse handleSally(const nlohmann::json& body,
                        const std::optional<std::string>& username,
                        const ClientInfo& client,
                        const std::optional<std::string>& new_token);

ApiResponse handleCampaign(const nlohmann::json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token);

ApiResponse handleHunt(const nlohmann::json& body,
                       const std::optional<std::string>& username,
                       const ClientInfo& client,
                       const std::optional<std::string>& new_token);

ApiResponse handleCreateAccount(const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token);

ApiResponse handleUpdateProfile(const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token);

inline std::unordered_map<std::string, ApiHandler>& getEndpointHandlers() {
    static std::unordered_map<std::string, ApiHandler> handlers = {
        {"login",       handleLogin},
        {"getPlayer",   handleGetPlayer},
        {"Build",       handleBuild},
        {"getWorld",    handleGetWorld},
        {"getFiefdom",  handleGetFiefdom},
        {"sally",       handleSally},
        {"campaign",    handleCampaign},
        {"hunt",        handleHunt},
        {"updateProfile", handleUpdateProfile}
    };
    return handlers;
}