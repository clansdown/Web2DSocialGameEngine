#pragma once
// Server-authoritative chat business logic. Every method throws chat_error with
// a stable machine code on failure; the HTTP layer turns that into
// `{ "error": code }`. See server/docs/chat.md.
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>
#include <memory>

#include "chat/ChatTypes.hpp"
#include "chat/TranslationQueue.hpp"

class GameConfigCache;

namespace chat {

class TranslationEngine;
class LlmTranslationEngine;

struct service_result {
    json data;
};

class ChatService {
public:
    static ChatService& instance() {
        static ChatService inst;
        return inst;
    }

    void initialize(GameConfigCache* config_cache);

    // ── Core ───────────────────────────────────────────────────────────────
    json get_chats(int64_t character_id);
    json get_messages(int64_t character_id, const std::string& chat_id,
                      int64_t before_seq, int64_t after_seq, int limit,
                      const std::string& viewer_lang);
    json poll(int64_t character_id, int64_t cursor, const std::string& viewer_lang);
    json poll_wildlands(int64_t character_id, int64_t cursor, const std::string& viewer_lang);
    json send_message(int64_t character_id, const std::string& chat_id,
                      const std::string& safe_message_id, const std::string& body,
                      const std::string& gif_id,
                      const std::string& reply_to_message_id,
                      const std::string& client_id, const std::string& sender_language);
    json edit_message(int64_t character_id, const std::string& message_id,
                      const std::string& safe_message_id, const std::string& body,
                      const std::string& gif_id);
    json delete_message(int64_t character_id, const std::string& message_id);
    json mark_read(int64_t character_id, const std::string& chat_id, int64_t seq);
    json set_settings(int64_t character_id, const std::string& chat_id, const json& body);
    json open_direct_chat(int64_t character_id, int64_t target_character_id);
    json search_characters(int64_t character_id, const std::string& query);

    // ── Translation ────────────────────────────────────────────────────────
    json request_translations(int64_t character_id, const std::vector<std::string>& message_ids,
                              const std::string& viewer_lang);
    json get_language_prefs(int64_t character_id);
    json set_language_pref(int64_t character_id, const std::string& language, bool enabled);

    // ── Reactions + bookmarks ──────────────────────────────────────────────
    json add_reaction(int64_t character_id, const std::string& message_id,
                      const std::string& reaction_id);
    json remove_reaction(int64_t character_id, const std::string& message_id,
                         const std::string& reaction_id);
    json add_bookmark(int64_t character_id, const std::string& message_id);
    json remove_bookmark(int64_t character_id, const std::string& message_id);
    json get_bookmarks(int64_t character_id);

    // ── Social graph ───────────────────────────────────────────────────────
    json send_friend_request(int64_t character_id, int64_t target_character_id);
    json accept_friend_request(int64_t character_id, int64_t target_character_id);
    json decline_friend_request(int64_t character_id, int64_t target_character_id);
    json unfriend(int64_t character_id, int64_t target_character_id);
    json block_user(int64_t character_id, int64_t target_character_id);
    json unblock_user(int64_t character_id, int64_t target_character_id);
    json mute_character(int64_t character_id, int64_t target_character_id);
    json unmute_character(int64_t character_id, int64_t target_character_id);
    json get_friends(int64_t character_id);
    json get_friend_presence(int64_t character_id);

    // ── Groups ─────────────────────────────────────────────────────────────
    json create_group(int64_t character_id, const std::string& name);
    json invite_to_group(int64_t character_id, const std::string& chat_id, int64_t target_character_id);
    json accept_group_invite(int64_t character_id, const std::string& chat_id);
    json decline_group_invite(int64_t character_id, const std::string& chat_id);
    json leave_chat(int64_t character_id, const std::string& chat_id);
    json transfer_ownership(int64_t character_id, const std::string& chat_id, int64_t target_character_id);
    json set_moderator(int64_t character_id, const std::string& chat_id, int64_t target_character_id, bool moderator);
    json kick_member(int64_t character_id, const std::string& chat_id, int64_t target_character_id);
    json rename_group(int64_t character_id, const std::string& chat_id, const std::string& name);
    json set_invite_policy(int64_t character_id, const std::string& chat_id, bool members_can_invite);

    // ── Moderation ─────────────────────────────────────────────────────────
    json ban_from_chat(int64_t character_id, const std::string& chat_id, int64_t target_character_id,
                       const std::string& reason);
    json unban_from_chat(int64_t character_id, const std::string& chat_id, int64_t target_character_id);
    json get_moderation_log(int64_t character_id, const json& filter);

    // ── Alliance materialization ───────────────────────────────────────────
    void sync_barony_chat(int64_t barony_id);
    void reconcile_alliances();

    // ── Admin ──────────────────────────────────────────────────────────────
    json get_identity_history(int64_t admin_character_id, int64_t target_character_id);
    json admin_lock_identity(int64_t admin_character_id, const json& body);

    // ── Push ───────────────────────────────────────────────────────────────
    json get_push_config(int64_t character_id);
    json subscribe_push(int64_t character_id, const json& body);
    json unsubscribe_push(int64_t character_id, const std::string& endpoint);
    json set_push_preferences(int64_t character_id, const json& body);

    // ── Internals shared with handlers / push ──────────────────────────────
    // Ensure a direct chat exists between two characters; returns its id.
    std::string ensure_direct_chat(int64_t a, int64_t b);
    std::string wildlands_chat_id();
    bool is_admin(int64_t account_id);
    bool can_view_chat(int64_t character_id, const std::string& chat_id);

    // Languages that should receive an immediate premium fan-out for a message
    // authored by `author_character_id`: active room members excluding the
    // author, minors, and anyone with a block involving the author. Languages
    // are ordered by frequency and capped by
    // translation.json premium.max_target_languages.
    std::vector<std::string> translation_targets(
        const std::string& chat_id, int64_t author_character_id,
        const std::string& source_language);

    // ── Translation queue bridge (internal; used by TranslationQueue) ──────
    std::vector<translation_result> translate_baseline_batch(
        const std::vector<translation_request>& requests);
    std::optional<fanout_translation_result> translate_premium_fanout(
        const fanout_translation_request& request);
    bool store_translation(const translation_job& job,
                           const translation_result& result);
    bool baseline_available() const;
    bool premium_engine_available() const;

private:
    ChatService() = default;

    void check_rate_limit(int64_t account_id);
    void dispatch_push(const std::string& chat_id, int64_t author, const std::string& message_id,
                       int64_t message_version, const std::string& body,
                       const std::string& source_language,
                       const std::string& safe_message_id);
    translation_tier translation_tier_for_viewer(int64_t character_id, int64_t now) const;
    bool enqueue_translation(const translation_job& job);
    bool message_version_matches(const std::string& message_id,
                                 int64_t expected_version) const;

    GameConfigCache* config_ = nullptr;
    std::shared_ptr<TranslationEngine> baseline_engine_;
    std::shared_ptr<LlmTranslationEngine> llm_engine_;
};

}  // namespace chat
