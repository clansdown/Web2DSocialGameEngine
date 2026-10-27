#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include "ApiResponse.hpp"

class GameConfigCache;

// Request metadata for logging/auth context. These are OWNED strings: uWS only
// guarantees req->getHeader() views during the synchronous handler call, and we
// capture this struct into res->onData (which fires later, when the body arrives
// and the request buffer may be reused). Never store uWS views in here.
struct ClientInfo {
    std::string real_ip;
    std::string forwarded_for;
    std::string forwarded_proto;
    std::string forwarded_host;
    std::string forwarded_port;
    std::string user_agent;
    std::string host;
    std::string request_id;
};

inline ClientInfo parseClientHeaders(auto* req) {
    ClientInfo info;
    info.real_ip = std::string(req->getHeader("x-real-ip"));
    info.forwarded_for = std::string(req->getHeader("x-forwarded-for"));
    info.forwarded_proto = std::string(req->getHeader("x-forwarded-proto"));
    info.forwarded_host = std::string(req->getHeader("x-forwarded-host"));
    info.forwarded_port = std::string(req->getHeader("x-forwarded-port"));
    info.user_agent = std::string(req->getHeader("user-agent"));
    info.host = std::string(req->getHeader("host"));
    info.request_id = std::string(req->getHeader("x-request-id"));
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

ApiResponse handleGetReeveReport(GameConfigCache& config_cache, const nlohmann::json& body,
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

ApiResponse handleEstimateOngoingRewards(GameConfigCache& config_cache, const nlohmann::json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token);

ApiResponse handleGetTexts(GameConfigCache& config_cache, const nlohmann::json& body,
                            const std::optional<std::string>& username,
                            const ClientInfo& client,
                            const std::optional<std::string>& new_token);

ApiResponse handleGetCharacterTexts(GameConfigCache& config_cache, const nlohmann::json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token);

ApiResponse handleSetCharacterArchetype(GameConfigCache& config_cache, const nlohmann::json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token);

ApiResponse handleGetBaronies(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handleGetBaronyMembers(GameConfigCache& config_cache, const nlohmann::json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token);

ApiResponse handleJoinBarony(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handleCreateBarony(GameConfigCache& config_cache, const nlohmann::json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token);

ApiResponse handleStartBaronTrack(GameConfigCache& config_cache, const nlohmann::json& body,
                                  const std::optional<std::string>& username,
                                  const ClientInfo& client,
                                  const std::optional<std::string>& new_token);

ApiResponse handleSetBaronyAcceptsMinors(GameConfigCache& config_cache,
                                         const nlohmann::json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token);

ApiResponse handleGetSafeContent(GameConfigCache& config_cache, const nlohmann::json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token);

ApiResponse handleAdminSetBaronyIdentity(GameConfigCache& config_cache,
                                         const nlohmann::json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token);

ApiResponse handleAdminSetAdultStatus(GameConfigCache& config_cache, const nlohmann::json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token);

ApiResponse handleAdminGetDeletedMessages(GameConfigCache& config_cache,
                                          const nlohmann::json& body,
                                          const std::optional<std::string>& username,
                                          const ClientInfo& client,
                                          const std::optional<std::string>& new_token);

ApiResponse handleFoundFreehold(GameConfigCache& config_cache, const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token);

ApiResponse handleLeaveBarony(GameConfigCache& config_cache, const nlohmann::json& body,
                              const std::optional<std::string>& username,
                              const ClientInfo& client,
                              const std::optional<std::string>& new_token);

ApiResponse handleApplyForMesneLord(GameConfigCache& config_cache, const nlohmann::json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token);

ApiResponse handleGetLetters(GameConfigCache& config_cache, const nlohmann::json& body,
                             const std::optional<std::string>& username,
                             const ClientInfo& client,
                             const std::optional<std::string>& new_token);

ApiResponse handleMarkLetterRead(GameConfigCache& config_cache, const nlohmann::json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token);

ApiResponse handleSetFiefdomImport(GameConfigCache& config_cache, const nlohmann::json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token);

ApiResponse handleSetFiefdomReserve(GameConfigCache& config_cache, const nlohmann::json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token);

ApiResponse handleSetBuildingOutputRate(GameConfigCache& config_cache, const nlohmann::json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token);

ApiResponse handleSetCharacterSex(GameConfigCache& config_cache, const nlohmann::json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token);

ApiResponse handleGetAvatarPool(GameConfigCache& config_cache, const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token);

ApiResponse handleGetPortraitFrames(GameConfigCache& config_cache, const nlohmann::json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token);

ApiResponse handleSetCharacterAvatar(GameConfigCache& config_cache, const nlohmann::json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token);

ApiResponse handleTDRound(GameConfigCache& config_cache, const nlohmann::json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                            const std::optional<std::string>& new_token);

ApiResponse handleWeedingStart(GameConfigCache& config_cache, const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token);

ApiResponse handleWeedingTurn(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handleGetUITextures(GameConfigCache& config_cache, const nlohmann::json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token);

ApiResponse handleAcknowledgeLandPatent(GameConfigCache& config_cache, const nlohmann::json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token);

ApiResponse handleGetBuildingConfigs(GameConfigCache& config_cache, const nlohmann::json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token);

ApiResponse handleCombatCreate(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handleCombatJoin(GameConfigCache& config_cache, const nlohmann::json& body,
                             const std::optional<std::string>& username,
                             const ClientInfo& client,
                             const std::optional<std::string>& new_token);

ApiResponse handleCombatList(GameConfigCache& config_cache, const nlohmann::json& body,
                             const std::optional<std::string>& username,
                             const ClientInfo& client,
                             const std::optional<std::string>& new_token);

ApiResponse handleCombatMatchmaking(GameConfigCache& config_cache, const nlohmann::json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token);

ApiResponse handleCombatGetConfigs(GameConfigCache& config_cache, const nlohmann::json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token);

ApiResponse handleGetCombatOfferings(GameConfigCache& config_cache, const nlohmann::json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token);

ApiResponse handleGetStore(GameConfigCache& config_cache, const nlohmann::json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token);

ApiResponse handleGetCosmetics(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handlePurchasePremiumItem(GameConfigCache& config_cache, const nlohmann::json& body,
                                      const std::optional<std::string>& username,
                                      const ClientInfo& client,
                                      const std::optional<std::string>& new_token);

ApiResponse handlePurchasePlatinum(GameConfigCache& config_cache, const nlohmann::json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token);

ApiResponse handleSettlePlatinum(GameConfigCache& config_cache, const nlohmann::json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token);

ApiResponse handleEquipCosmetic(GameConfigCache& config_cache, const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token);

ApiResponse handleDeassignCosmetic(GameConfigCache& config_cache, const nlohmann::json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token);

ApiResponse handleRefundPremiumItem(GameConfigCache& config_cache, const nlohmann::json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token);

ApiResponse handleGetRetinue(GameConfigCache& config_cache, const nlohmann::json& body,
                             const std::optional<std::string>& username,
                             const ClientInfo& client,
                             const std::optional<std::string>& new_token);

ApiResponse handleListRecruitCandidates(GameConfigCache& config_cache, const nlohmann::json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token);

ApiResponse handleHireRecruit(GameConfigCache& config_cache, const nlohmann::json& body,
                              const std::optional<std::string>& username,
                              const ClientInfo& client,
                              const std::optional<std::string>& new_token);

ApiResponse handleSetRetinuePriority(GameConfigCache& config_cache, const nlohmann::json& body,
                                     const std::optional<std::string>& username,
                                     const ClientInfo& client,
                                     const std::optional<std::string>& new_token);

ApiResponse handleGetTechTrees(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handleLearnTechNode(GameConfigCache& config_cache, const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token);

ApiResponse handleStartForgeOrder(GameConfigCache& config_cache, const nlohmann::json& body,
                                  const std::optional<std::string>& username,
                                  const ClientInfo& client,
                                  const std::optional<std::string>& new_token);

ApiResponse handleGetRetinueGear(GameConfigCache& config_cache, const nlohmann::json& body,
                                 const std::optional<std::string>& username,
                                 const ClientInfo& client,
                                 const std::optional<std::string>& new_token);

ApiResponse handleEquipGear(GameConfigCache& config_cache, const nlohmann::json& body,
                            const std::optional<std::string>& username,
                            const ClientInfo& client,
                            const std::optional<std::string>& new_token);

ApiResponse handleDeassignGear(GameConfigCache& config_cache, const nlohmann::json& body,
                               const std::optional<std::string>& username,
                               const ClientInfo& client,
                               const std::optional<std::string>& new_token);

ApiResponse handleSellItem(GameConfigCache& config_cache, const nlohmann::json& body,
                           const std::optional<std::string>& username,
                           const ClientInfo& client,
                           const std::optional<std::string>& new_token);

ApiResponse handleBuyGearCity(GameConfigCache& config_cache, const nlohmann::json& body,
                              const std::optional<std::string>& username,
                              const ClientInfo& client,
                              const std::optional<std::string>& new_token);

ApiResponse handleHireTeacher(GameConfigCache& config_cache, const nlohmann::json& body,
                              const std::optional<std::string>& username,
                              const ClientInfo& client,
                              const std::optional<std::string>& new_token);

ApiResponse handleApplyBookItem(GameConfigCache& config_cache, const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token);

// Defined in chat/ChatHandlers.cpp; registers the chat endpoint family.
void register_chat_handlers(std::unordered_map<std::string, ApiHandler>& handlers);

// Defined in staff/StaffHandlers.cpp; registers player support/notifications
// and the staff console endpoints.
void register_staff_handlers(std::unordered_map<std::string, ApiHandler>& handlers);

// Defined in registration/InvitationHandlers.cpp; registers player invitation
// status/generation and the staff invite-management endpoints.
void register_invitation_handlers(std::unordered_map<std::string, ApiHandler>& handlers);

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
    add("getReeveReport",         [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetReeveReport(c, b, u, cl, t); });
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
    add("getCharacterTexts",      [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetCharacterTexts(c, b, u, cl, t); });
    add("setCharacterArchetype",  [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSetCharacterArchetype(c, b, u, cl, t); });
    add("setFiefdomImport",       [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSetFiefdomImport(c, b, u, cl, t); });
    add("setFiefdomReserve",      [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSetFiefdomReserve(c, b, u, cl, t); });
    add("setBuildingOutputRate",  [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSetBuildingOutputRate(c, b, u, cl, t); });
    add("getBaronies",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetBaronies(c, b, u, cl, t); });
    add("setBaronyAcceptsMinors", [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSetBaronyAcceptsMinors(c, b, u, cl, t); });
    add("getBaronyMembers",       [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetBaronyMembers(c, b, u, cl, t); });
    add("joinBarony",             [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleJoinBarony(c, b, u, cl, t); });
    add("createBarony",           [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleCreateBarony(c, b, u, cl, t); });
    add("startBaronTrack",        [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleStartBaronTrack(c, b, u, cl, t); });
    add("foundFreehold",          [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleFoundFreehold(c, b, u, cl, t); });
    add("leaveBarony",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleLeaveBarony(c, b, u, cl, t); });
    add("applyForMesneLord",      [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleApplyForMesneLord(c, b, u, cl, t); });
    add("getLetters",             [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetLetters(c, b, u, cl, t); });
    add("markLetterRead",         [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleMarkLetterRead(c, b, u, cl, t); });
    add("setCharacterSex",        [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSetCharacterSex(c, b, u, cl, t); });
    add("getAvatarPool",          [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetAvatarPool(c, b, u, cl, t); });
    add("getPortraitFrames",      [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetPortraitFrames(c, b, u, cl, t); });
    add("getSafeContent",         [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetSafeContent(c, b, u, cl, t); });
    add("setCharacterAvatar",     [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSetCharacterAvatar(c, b, u, cl, t); });
    add("tdRound",                [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleTDRound(c, b, u, cl, t); });
    add("weedingStart",           [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleWeedingStart(c, b, u, cl, t); });
    add("weedingTurn",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleWeedingTurn(c, b, u, cl, t); });
    add("getBuildingConfigs",     [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetBuildingConfigs(c, b, u, cl, t); });
    add("acknowledgeLandPatent",  [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleAcknowledgeLandPatent(c, b, u, cl, t); });
    add("estimateOngoingRewards", [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleEstimateOngoingRewards(c, b, u, cl, t); });
    add("combatCreate",          [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleCombatCreate(c, b, u, cl, t); });
    add("combatJoin",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleCombatJoin(c, b, u, cl, t); });
    add("combatList",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleCombatList(c, b, u, cl, t); });
    add("combatMatchmaking",     [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleCombatMatchmaking(c, b, u, cl, t); });
    add("combatGetConfigs",      [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleCombatGetConfigs(c, b, u, cl, t); });
    add("getCombatOfferings",    [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetCombatOfferings(c, b, u, cl, t); });
    add("getStore",              [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetStore(c, b, u, cl, t); });
    add("getCosmetics",          [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetCosmetics(c, b, u, cl, t); });
    add("purchasePremiumItem",   [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handlePurchasePremiumItem(c, b, u, cl, t); });
    add("purchasePlatinum",      [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handlePurchasePlatinum(c, b, u, cl, t); });
    add("settlePlatinum",        [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSettlePlatinum(c, b, u, cl, t); });
    add("equipCosmetic",         [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleEquipCosmetic(c, b, u, cl, t); });
    add("deassignCosmetic",      [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleDeassignCosmetic(c, b, u, cl, t); });
    add("refundPremiumItem",     [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleRefundPremiumItem(c, b, u, cl, t); });
    add("getRetinue",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetRetinue(c, b, u, cl, t); });
    add("listRecruitCandidates", [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleListRecruitCandidates(c, b, u, cl, t); });
    add("hireRecruit",           [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleHireRecruit(c, b, u, cl, t); });
    add("setRetinuePriority",    [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSetRetinuePriority(c, b, u, cl, t); });
    add("getTechTrees",           [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetTechTrees(c, b, u, cl, t); });
    add("learnTechNode",          [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleLearnTechNode(c, b, u, cl, t); });
    add("startForgeOrder",        [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleStartForgeOrder(c, b, u, cl, t); });
    add("getRetinueGear",         [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleGetRetinueGear(c, b, u, cl, t); });
    add("equipGear",              [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleEquipGear(c, b, u, cl, t); });
    add("deassignGear",           [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleDeassignGear(c, b, u, cl, t); });
    add("sellItem",               [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleSellItem(c, b, u, cl, t); });
    add("buyGearCity",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleBuyGearCity(c, b, u, cl, t); });
    add("hireTeacher",            [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleHireTeacher(c, b, u, cl, t); });
    add("applyBookItem",          [&](GameConfigCache& c, Json b, Username u, Client cl, Token t) { return handleApplyBookItem(c, b, u, cl, t); });
    register_chat_handlers(handlers);
    register_staff_handlers(handlers);
    register_invitation_handlers(handlers);
    return handlers;
}
