#include "chat/ChatDB.hpp"

namespace chat {

int64_t ChatDB::allocate_change_id() {
    auto& db = this->db();
    int64_t next = 1;
    db << "SELECT next_change_id FROM change_counter WHERE id = 1;"
       >> [&](int64_t v) { next = v; };
    db << "UPDATE change_counter SET next_change_id = ? WHERE id = 1;" << (next + 1);
    return next;
}

int64_t ChatDB::current_change_id() {
    auto& db = this->db();
    int64_t next = 1;
    db << "SELECT next_change_id FROM change_counter WHERE id = 1;"
       >> [&](int64_t v) { next = v; };
    return next - 1;
}

int64_t ChatDB::allocate_seq(const std::string& chat_id, int64_t change_id) {
    auto& db = this->db();
    int64_t last_seq = 0;
    db << "SELECT last_seq FROM chats WHERE id = ?;" << chat_id
       >> [&](int64_t v) { last_seq = v; };
    int64_t next_seq = last_seq + 1;
    db << "UPDATE chats SET last_seq = ?, change_id = ? WHERE id = ?;"
       << next_seq << change_id << chat_id;
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
