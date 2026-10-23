#pragma once
// Thin access layer over the dedicated chat.db connection (see Database.hpp).
// Owns cursor allocation (per-chat `seq` + global `change_id`) and the small
// set of cross-cutting queries used by ChatService.
#include <sqlite_modern_cpp.h>
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>
#include <optional>

#include "Database.hpp"

namespace chat {

class ChatDB {
public:
    static ChatDB& instance() {
        static ChatDB inst;
        return inst;
    }

    sqlite::database& db() { return Database::getInstance().chatDB(); }

    // Global monotonic change counter. Every mutated row gets a fresh value so
    // a poll cursor can detect updates across all tables with one integer.
    int64_t allocate_change_id();
    int64_t current_change_id();

    // Per-chat monotonic sequence. Bumps chats.last_seq and stamps the chat
    // with the supplied change_id in the same logical step.
    int64_t allocate_seq(const std::string& chat_id, int64_t change_id);

    // Bump a membership row's change_id (used for settings/read/role changes).
    void touch_member(const std::string& chat_id, int64_t character_id, int64_t change_id);

    // True when the character has an active membership in the chat.
    bool is_member(const std::string& chat_id, int64_t character_id);

    // Role of an active member, or empty if not a member.
    std::string member_role(const std::string& chat_id, int64_t character_id);

private:
    ChatDB() = default;
};

}  // namespace chat
