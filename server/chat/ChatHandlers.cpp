#include "chat/ChatHandlers.hpp"

#include <optional>
#include <string>
#include <vector>

#include "Database.hpp"
#include "chat/ChatService.hpp"
#include "chat/Presence.hpp"

namespace {

using chat::json;

struct Actor {
    int64_t character_id = 0;
    int64_t account_id = 0;
    bool ok = false;
    std::string error;
};

// Resolves the acting account + character. Every chat request carries
// `character_id`; ownership is verified against the authenticated username.
Actor resolve_actor(const nlohmann::json& body, const std::optional<std::string>& username) {
    Actor actor;
    if (!username) {
        actor.error = "needs_auth";
        return actor;
    }
    auto& db = Database::getInstance().gameDB();
    db << "SELECT id FROM users WHERE username = ?;" << *username
       >> [&](int64_t id) { actor.account_id = id; };
    if (body.contains("character_id") && body["character_id"].is_number_integer()) {
        int64_t cid = body["character_id"].get<int64_t>();
        db << "SELECT id FROM characters WHERE id = ? AND user_id = ?;" << cid << actor.account_id
           >> [&](int64_t id) { actor.character_id = id; };
    }
    actor.ok = actor.character_id > 0;
    if (!actor.ok) actor.error = "character_required";
    return actor;
}

ApiResponse run(const std::function<json()>& fn, const std::optional<std::string>& new_token) {
    ApiResponse response;
    try {
        response.data = fn();
    } catch (const chat::chat_error& e) {
        response.error = e.what();
    } catch (const std::exception& e) {
        response.error = std::string("chat_error: ") + e.what();
    }
    if (new_token) response.data["token"] = *new_token;
    return response;
}

ApiResponse auth_error(const Actor& actor, bool has_username) {
    ApiResponse response;
    response.error = actor.error;
    if (!has_username) response.needs_auth = true;
    return response;
}

#define CHAT_HANDLER(name, expr)                                                                 \
    ApiResponse name(GameConfigCache& config_cache, const nlohmann::json& body,                  \
                     const std::optional<std::string>& username, const ClientInfo& client,        \
                     const std::optional<std::string>& new_token) {                              \
        (void)config_cache;                                                                      \
        (void)client;                                                                            \
        Actor actor = resolve_actor(body, username);                                             \
        if (!actor.ok) return auth_error(actor, username.has_value());                           \
        return run([&]() -> json { return (expr); }, new_token);                                 \
    }

// ── Core ─────────────────────────────────────────────────────────────────────
CHAT_HANDLER(handleGetChats, chat::ChatService::instance().get_chats(actor.character_id))
CHAT_HANDLER(handleGetChatMessages,
             chat::ChatService::instance().get_messages(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("before_seq", static_cast<int64_t>(0)),
                 body.value("after_seq", static_cast<int64_t>(0)), body.value("limit", 50),
                 body.value("lang", std::string())))
CHAT_HANDLER(handlePollChats,
             chat::ChatService::instance().poll(actor.character_id,
                                                body.value("cursor", static_cast<int64_t>(0)),
                                                body.value("lang", std::string())))
CHAT_HANDLER(handlePollWildlands,
             chat::ChatService::instance().poll_wildlands(
                 actor.character_id, body.value("cursor", static_cast<int64_t>(0)),
                 body.value("lang", std::string())))
CHAT_HANDLER(handleSendChatMessage,
             chat::ChatService::instance().send_message(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("safe_message_id", std::string()),
                 body.value("body", std::string()),
                 body.value("gif_id", std::string()),
                 body.value("reply_to_message_id", std::string()),
                 body.value("client_id", std::string()),
                 body.value("language", std::string())))
CHAT_HANDLER(handleEditChatMessage,
             chat::ChatService::instance().edit_message(
                 actor.character_id, body.value("message_id", std::string()),
                 body.value("safe_message_id", std::string()),
                 body.value("body", std::string()),
                 body.value("gif_id", std::string())))
CHAT_HANDLER(handleDeleteChatMessage,
             chat::ChatService::instance().delete_message(
                 actor.character_id, body.value("message_id", std::string())))
CHAT_HANDLER(handleMarkChatRead,
             chat::ChatService::instance().mark_read(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("seq", static_cast<int64_t>(0))))
CHAT_HANDLER(handleSetChatSettings,
             chat::ChatService::instance().set_settings(
                 actor.character_id, body.value("chat_id", std::string()), body))
CHAT_HANDLER(handleOpenDirectChat,
             chat::ChatService::instance().open_direct_chat(
                 actor.character_id, body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleSearchChatCharacters,
             chat::ChatService::instance().search_characters(
                 actor.character_id, body.value("query", std::string())))

// ── Translation ──────────────────────────────────────────────────────────────
CHAT_HANDLER(handleRequestTranslations,
             chat::ChatService::instance().request_translations(
                 actor.character_id, body.value("message_ids", std::vector<std::string>{}),
                 body.value("lang", std::string())))
CHAT_HANDLER(handleGetChatLanguagePrefs,
             chat::ChatService::instance().get_language_prefs(actor.character_id))
CHAT_HANDLER(handleSetChatLanguagePref,
             chat::ChatService::instance().set_language_pref(
                 actor.character_id, body.value("language", std::string()),
                 body.value("enabled", true)))

// ── Presence + typing (in-memory only) ───────────────────────────────────────
CHAT_HANDLER(handleGetFriendPresence,
             chat::ChatService::instance().get_friend_presence(actor.character_id))

// ── Web Push ─────────────────────────────────────────────────────────────────
CHAT_HANDLER(handleGetPushConfig,
             chat::ChatService::instance().get_push_config(actor.character_id))
CHAT_HANDLER(handleSubscribePush,
             chat::ChatService::instance().subscribe_push(actor.character_id, body))
CHAT_HANDLER(handleUnsubscribePush,
             chat::ChatService::instance().unsubscribe_push(
                 actor.character_id, body.value("endpoint", std::string())))
CHAT_HANDLER(handleSetPushPreferences,
             chat::ChatService::instance().set_push_preferences(actor.character_id, body))

ApiResponse handleChatHeartbeat(GameConfigCache& config_cache, const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    Actor actor = resolve_actor(body, username);
    if (!actor.ok) return auth_error(actor, username.has_value());
    chat::Presence::instance().heartbeat(actor.character_id, chat::now_seconds());
    return run([]() -> json { return json{{"ok", true}}; }, new_token);
}

ApiResponse handleSetChatTyping(GameConfigCache& config_cache, const nlohmann::json& body,
                                const std::optional<std::string>& username,
                                const ClientInfo& client,
                                const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    Actor actor = resolve_actor(body, username);
    if (!actor.ok) return auth_error(actor, username.has_value());
    chat::Presence::instance().set_typing(actor.character_id,
                                          body.value("chat_id", std::string()),
                                          chat::now_seconds());
    return run([]() -> json { return json{{"ok", true}}; }, new_token);
}

// ── Reactions + bookmarks ────────────────────────────────────────────────────
CHAT_HANDLER(handleAddChatReaction,
             chat::ChatService::instance().add_reaction(
                 actor.character_id, body.value("message_id", std::string()),
                 body.value("reaction_id",
                            body.value("emoji", std::string()))))
CHAT_HANDLER(handleRemoveChatReaction,
             chat::ChatService::instance().remove_reaction(
                 actor.character_id, body.value("message_id", std::string()),
                 body.value("reaction_id",
                            body.value("emoji", std::string()))))
CHAT_HANDLER(handleAddChatBookmark,
             chat::ChatService::instance().add_bookmark(
                 actor.character_id, body.value("message_id", std::string())))
CHAT_HANDLER(handleRemoveChatBookmark,
             chat::ChatService::instance().remove_bookmark(
                 actor.character_id, body.value("message_id", std::string())))
CHAT_HANDLER(handleGetChatBookmarks,
             chat::ChatService::instance().get_bookmarks(actor.character_id))

// ── Social graph ─────────────────────────────────────────────────────────────
CHAT_HANDLER(handleSendFriendRequest,
             chat::ChatService::instance().send_friend_request(
                 actor.character_id, body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleAcceptFriendRequest,
             chat::ChatService::instance().accept_friend_request(
                 actor.character_id, body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleDeclineFriendRequest,
             chat::ChatService::instance().decline_friend_request(
                 actor.character_id, body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleUnfriend,
             chat::ChatService::instance().unfriend(
                 actor.character_id, body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleBlockUser,
             chat::ChatService::instance().block_user(
                 actor.character_id, body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleUnblockUser,
             chat::ChatService::instance().unblock_user(
                 actor.character_id, body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleMuteCharacter,
             chat::ChatService::instance().mute_character(
                 actor.character_id, body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleUnmuteCharacter,
             chat::ChatService::instance().unmute_character(
                 actor.character_id, body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleGetFriends, chat::ChatService::instance().get_friends(actor.character_id))

// ── Groups ───────────────────────────────────────────────────────────────────
CHAT_HANDLER(handleCreateGroupChat,
             chat::ChatService::instance().create_group(
                 actor.character_id, body.value("name", std::string())))
CHAT_HANDLER(handleInviteToGroup,
             chat::ChatService::instance().invite_to_group(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleAcceptGroupInvite,
             chat::ChatService::instance().accept_group_invite(
                 actor.character_id, body.value("chat_id", std::string())))
CHAT_HANDLER(handleDeclineGroupInvite,
             chat::ChatService::instance().decline_group_invite(
                 actor.character_id, body.value("chat_id", std::string())))
CHAT_HANDLER(handleLeaveChat,
             chat::ChatService::instance().leave_chat(
                 actor.character_id, body.value("chat_id", std::string())))
CHAT_HANDLER(handleTransferGroupOwnership,
             chat::ChatService::instance().transfer_ownership(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleSetGroupModerator,
             chat::ChatService::instance().set_moderator(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("target_character_id", static_cast<int64_t>(0)),
                 body.value("moderator", false)))
CHAT_HANDLER(handleKickMember,
             chat::ChatService::instance().kick_member(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("target_character_id", static_cast<int64_t>(0))))
CHAT_HANDLER(handleRenameGroup,
             chat::ChatService::instance().rename_group(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("name", std::string())))
CHAT_HANDLER(handleSetGroupInvitePolicy,
             chat::ChatService::instance().set_invite_policy(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("members_can_invite", false)))

// ── Moderation ───────────────────────────────────────────────────────────────
CHAT_HANDLER(handleBanFromChat,
             chat::ChatService::instance().ban_from_chat(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("target_character_id", static_cast<int64_t>(0)),
                 body.value("reason", std::string())))
CHAT_HANDLER(handleUnbanFromChat,
             chat::ChatService::instance().unban_from_chat(
                 actor.character_id, body.value("chat_id", std::string()),
                 body.value("target_character_id", static_cast<int64_t>(0))))

#undef CHAT_HANDLER

// ── Admin-only handlers ──────────────────────────────────────────────────────

ApiResponse handleGetModerationLog(GameConfigCache& config_cache, const nlohmann::json& body,
                                   const std::optional<std::string>& username,
                                   const ClientInfo& client,
                                   const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    Actor actor = resolve_actor(body, username);
    if (!actor.ok) return auth_error(actor, username.has_value());
    if (!chat::ChatService::instance().is_admin(actor.account_id)) {
        ApiResponse response;
        response.error = "not_admin";
        return response;
    }
    return run([&]() -> json {
        return chat::ChatService::instance().get_moderation_log(actor.character_id, body);
    }, new_token);
}

ApiResponse handleAdminGetIdentityHistory(GameConfigCache& config_cache, const nlohmann::json& body,
                                          const std::optional<std::string>& username,
                                          const ClientInfo& client,
                                          const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    Actor actor = resolve_actor(body, username);
    if (!actor.ok) return auth_error(actor, username.has_value());
    if (!chat::ChatService::instance().is_admin(actor.account_id)) {
        ApiResponse response;
        response.error = "not_admin";
        return response;
    }
    return run([&]() -> json {
        return chat::ChatService::instance().get_identity_history(
            actor.character_id, body.value("target_character_id", static_cast<int64_t>(0)));
    }, new_token);
}

ApiResponse handleAdminLockIdentity(GameConfigCache& config_cache, const nlohmann::json& body,
                                    const std::optional<std::string>& username,
                                    const ClientInfo& client,
                                    const std::optional<std::string>& new_token) {
    (void)config_cache;
    (void)client;
    Actor actor = resolve_actor(body, username);
    if (!actor.ok) return auth_error(actor, username.has_value());
    if (!chat::ChatService::instance().is_admin(actor.account_id)) {
        ApiResponse response;
        response.error = "not_admin";
        return response;
    }
    return run([&]() -> json {
        return chat::ChatService::instance().admin_lock_identity(actor.character_id, body);
    }, new_token);
}

}  // namespace

void register_chat_handlers(std::unordered_map<std::string, ApiHandler>& handlers) {
    auto add = [&](const std::string& name, ApiHandler handler) {
        handlers[name] = std::move(handler);
    };
    auto reg = [&](const std::string& name, auto fn) {
        add(name, [fn](GameConfigCache& c, const nlohmann::json& b,
                       const std::optional<std::string>& u, const ClientInfo& cl,
                       const std::optional<std::string>& t) { return fn(c, b, u, cl, t); });
    };

    reg("getChats", handleGetChats);
    reg("getChatMessages", handleGetChatMessages);
    reg("pollChats", handlePollChats);
    reg("pollWildlands", handlePollWildlands);
    reg("sendChatMessage", handleSendChatMessage);
    reg("editChatMessage", handleEditChatMessage);
    reg("deleteChatMessage", handleDeleteChatMessage);
    reg("markChatRead", handleMarkChatRead);
    reg("setChatSettings", handleSetChatSettings);
    reg("openDirectChat", handleOpenDirectChat);
    reg("searchChatCharacters", handleSearchChatCharacters);
    reg("requestTranslations", handleRequestTranslations);
    reg("getChatLanguagePrefs", handleGetChatLanguagePrefs);
    reg("setChatLanguagePref", handleSetChatLanguagePref);
    reg("getFriendPresence", handleGetFriendPresence);
    reg("chatHeartbeat", handleChatHeartbeat);
    reg("setChatTyping", handleSetChatTyping);
    reg("getPushConfig", handleGetPushConfig);
    reg("subscribePush", handleSubscribePush);
    reg("unsubscribePush", handleUnsubscribePush);
    reg("setPushPreferences", handleSetPushPreferences);

    reg("addChatReaction", handleAddChatReaction);
    reg("removeChatReaction", handleRemoveChatReaction);
    reg("addChatBookmark", handleAddChatBookmark);
    reg("removeChatBookmark", handleRemoveChatBookmark);
    reg("getChatBookmarks", handleGetChatBookmarks);

    reg("sendFriendRequest", handleSendFriendRequest);
    reg("acceptFriendRequest", handleAcceptFriendRequest);
    reg("declineFriendRequest", handleDeclineFriendRequest);
    reg("unfriend", handleUnfriend);
    reg("blockUser", handleBlockUser);
    reg("unblockUser", handleUnblockUser);
    reg("muteCharacter", handleMuteCharacter);
    reg("unmuteCharacter", handleUnmuteCharacter);
    reg("getFriends", handleGetFriends);

    reg("createGroupChat", handleCreateGroupChat);
    reg("inviteToGroup", handleInviteToGroup);
    reg("acceptGroupInvite", handleAcceptGroupInvite);
    reg("declineGroupInvite", handleDeclineGroupInvite);
    reg("leaveChat", handleLeaveChat);
    reg("transferGroupOwnership", handleTransferGroupOwnership);
    reg("setGroupModerator", handleSetGroupModerator);
    reg("kickMember", handleKickMember);
    reg("renameGroup", handleRenameGroup);
    reg("setGroupInvitePolicy", handleSetGroupInvitePolicy);

    reg("banFromChat", handleBanFromChat);
    reg("unbanFromChat", handleUnbanFromChat);
    reg("getModerationLog", handleGetModerationLog);

    reg("adminGetIdentityHistory", handleAdminGetIdentityHistory);
    reg("adminLockIdentity", handleAdminLockIdentity);
}
