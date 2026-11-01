#include "chat/ChatDB.hpp"

namespace chat {

int64_t ChatDB::allocate_change_id() {
    // One statement: concurrent writers can never read the same counter value.
    // The adapter always pins one connection per thread, so RETURNING is safe
    // even while multiple HTTP workers and the translation queue allocate ids.
    auto& db = this->db();
    int64_t allocated = 0;
    db << "UPDATE change_counter SET next_change_id = next_change_id + 1 "
          "WHERE id = 1 RETURNING next_change_id - 1;"
       >> [&](int64_t v) { allocated = v; };
    return allocated;
}

int64_t ChatDB::current_change_id() {
    auto& db = this->db();
    int64_t next = 1;
    db << "SELECT next_change_id FROM change_counter WHERE id = 1;"
       >> [&](int64_t v) { next = v; };
    return next - 1;
}

int64_t ChatDB::allocate_seq(const std::string& chat_id, int64_t change_id) {
    // Atomic per-chat sequence allocation. The UNIQUE (chat_id, seq) constraint
    // otherwise turns concurrent sends into intermittent insert failures.
    auto& db = this->db();
    int64_t next_seq = 0;
    db << "UPDATE chats SET last_seq = last_seq + 1, change_id = ? WHERE id = ? "
          "RETURNING last_seq;"
       << change_id << chat_id
       >> [&](int64_t v) { next_seq = v; };
    return next_seq;
}

void ChatDB::touch_member(const std::string& chat_id, int64_t character_id, int64_t change_id) {
    this->db() << "UPDATE chat_members SET change_id = ? WHERE chat_id = ? AND character_id = ?;"
               << change_id << chat_id << character_id;
}

bool ChatDB::is_member(const std::string& chat_id, int64_t character_id) {
    bool found = false;
    this->db() << "SELECT 1 FROM chat_members WHERE chat_id = ? AND character_id = ? AND left_at IS NULL LIMIT 1;"
               << chat_id << character_id
               >> [&](int) { found = true; };
    return found;
}

std::string ChatDB::member_role(const std::string& chat_id, int64_t character_id) {
    std::string role;
    this->db() << "SELECT role FROM chat_members WHERE chat_id = ? AND character_id = ? AND left_at IS NULL LIMIT 1;"
               << chat_id << character_id
               >> [&](std::string r) { role = r; };
    return role;
}

}  // namespace chat
