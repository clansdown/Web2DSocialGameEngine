#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include "ApiResponse.hpp"

class GameConfigCache;

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
    GameConfigCache&,
    const nlohmann::json&,
    const std::optional<std::string>&,
    const ClientInfo&,
    const std::optional<std::string>&
)>;

ApiResponse handleLogin(GameConfigCache& config_cache, const nlohmann::json& body,
                        const std::optional<std::string>& username,
                        const ClientInfo& client,
                        const std::optional<std::string>& new_token);

ApiResponse handleGetCharacter(GameConfigCache& config_cache, const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token);

ApiResponse handleBuild(GameConfigCache& config_cache, const nlohmann::json& body,
                        const std::optional<std::string>& username,
                        const ClientInfo& client,
                        const std::optional<std::string>& new_token);

ApiResponse handleGetWorld(GameConfigCache& config_cache, const nlohmann::json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token);

ApiResponse handleGetFiefdom(GameConfigCache& config_cache, const nlohmann::json& body,
                             const std::optional<std::string>& username,
                             const ClientInfo& client,
                             const std::optional<std::string>& new_token);

ApiResponse handleSally(GameConfigCache& config_cache, const nlohmann::json& body,
                        const std::optional<std::string>& username,
                        const ClientInfo& client,
                        const std::optional<std::string>& new_token);

ApiResponse handleCampaign(GameConfigCache& config_cache, const nlohmann::json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token);

ApiResponse handleHunt(GameConfigCache& config_cache, const nlohmann::json& body,
                       const std::optional<std::string>& username,
                       const ClientInfo& client,
                       const std::optional<std::string>& new_token);

ApiResponse handleCreateAccount(GameConfigCache& config_cache, const nlohmann::json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token);

ApiResponse handleUpdateUserProfile(GameConfigCache& config_cache, const nlohmann::json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token);

ApiResponse handleUpdateCharacterProfile(GameConfigCache& config_cache, const nlohmann::json& body,
                                           const std::optional<std::string>& username,
                                           const ClientInfo& client,
                                           const std::optional<std::string>& new_token);

ApiResponse handleGetGameInfo(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handleGetPlayerState(GameConfigCache& config_cache, const nlohmann::json& body,
                                  const std::optional<std::string>& username,
                                  const ClientInfo& client,
                                  const std::optional<std::string>& new_token);

ApiResponse handleStartMiniGame(GameConfigCache& config_cache, const nlohmann::json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token);

ApiResponse handleEndMiniGame(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handleGetMiniGameConfig(GameConfigCache& config_cache, const nlohmann::json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token);

ApiResponse handleGetTexts(GameConfigCache& config_cache, const nlohmann::json& body,
                            const std::optional<std::string>& username,
                            const ClientInfo& client,
                            const std::optional<std::string>& new_token);

ApiResponse handleSetCharacterArchetype(GameConfigCache& config_cache, const nlohmann::json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token);

ApiResponse handleGetDukedoms(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handleJoinDukedom(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handleCreateDukedom(GameConfigCache& config_cache, const nlohmann::json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token);

ApiResponse handleStartDukeTrack(GameConfigCache& config_cache, const nlohmann::json& body,
                                  const std::optional<std::string>& username,
                                  const ClientInfo& client,
                                  const std::optional<std::string>& new_token);

ApiResponse handleSetCharacterSex(GameConfigCache& config_cache, const nlohmann::json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token);

ApiResponse handleTDRound(GameConfigCache& config_cache, const nlohmann::json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token);

ApiResponse handleGetUITextures(GameConfigCache& config_cache, const nlohmann::json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token);

ApiResponse handleVerifyAgeOverride(GameConfigCache& config_cache, const nlohmann::json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token);

inline std::unordered_map<std::string, ApiHandler> getEndpointHandlers(GameConfigCache& config_cache) {
    using Json = const nlohmann::json&;
    using Username = const std::optional<std::string>&;
    using Client = const ClientInfo&;
    using Token = const std::optional<std::string>&;

    std::unordered_map<std::string, ApiHandler> handlers;
    auto add = [&](const std::string& name, ApiHandler handler) { handlers[name] = std::move(handler); };

    add("login",                  [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleLogin(c, b, u, cl, t); });
    add("getCharacter",           [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetCharacter(c, b, u, cl, t); });
    add("Build",                  [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleBuild(c, b, u, cl, t); });
    add("getWorld",               [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetWorld(c, b, u, cl, t); });
    add("getFiefdom",             [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetFiefdom(c, b, u, cl, t); });
    add("sally",                  [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSally(c, b, u, cl, t); });
    add("campaign",               [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleCampaign(c, b, u, cl, t); });
    add("hunt",                   [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleHunt(c, b, u, cl, t); });
    add("createAccount",          [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleCreateAccount(c, b, u, cl, t); });
    add("updateUserProfile",      [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleUpdateUserProfile(c, b, u, cl, t); });
    add("updateCharacterProfile", [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleUpdateCharacterProfile(c, b, u, cl, t); });
    add("getGameInfo",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetGameInfo(c, b, u, cl, t); });
    add("getPlayerState",         [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetPlayerState(c, b, u, cl, t); });
    add("startMiniGame",          [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleStartMiniGame(c, b, u, cl, t); });
    add("endMiniGame",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleEndMiniGame(c, b, u, cl, t); });
    add("getMiniGameConfig",      [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetMiniGameConfig(c, b, u, cl, t); });
    add("getTexts",               [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetTexts(c, b, u, cl, t); });
    add("setCharacterArchetype",  [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSetCharacterArchetype(c, b, u, cl, t); });
    add("getDukedoms",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetDukedoms(c, b, u, cl, t); });
    add("joinDukedom",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleJoinDukedom(c, b, u, cl, t); });
    add("createDukedom",          [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleCreateDukedom(c, b, u, cl, t); });
    add("startDukeTrack",         [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleStartDukeTrack(c, b, u, cl, t); });
    add("setCharacterSex",        [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSetCharacterSex(c, b, u, cl, t); });
    add("tdRound",                [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleTDRound(c, b, u, cl, t); });
    return handlers;
}
