#include "Letters.hpp"

namespace letters {

int64_t send_letter(sqlite::database& db, int character_id,
                    const std::string& letter_key, int64_t now) {
    db << "INSERT INTO character_letters (character_id, letter_key, created_at, read_at) "
          "VALUES (?, ?, ?, 0);"
       << character_id << letter_key << now;
    return db.last_insert_rowid();
}

std::vector<letter_row> get_letters(sqlite::database& db, int character_id) {
    std::vector<letter_row> rows;
    db << "SELECT id, letter_key, created_at, read_at FROM character_letters "
          "WHERE character_id = ? ORDER BY created_at DESC, id DESC;"
       << character_id
       >> [&](int64_t id, std::string letter_key, int64_t created_at, int64_t read_at) {
              letter_row row;
              row.id = id;
              row.letter_key = std::move(letter_key);
              row.created_at = created_at;
              row.read_at = read_at;
              rows.push_back(std::move(row));
          };
    return rows;
}

bool has_any_letter(sqlite::database& db, int character_id) {
    int count = 0;
    db << "SELECT COUNT(*) FROM character_letters WHERE character_id = ?;"
       << character_id
       >> [&](int c) { count = c; };
    return count > 0;
}

std::optional<letter_row> oldest_unread_letter(sqlite::database& db, int character_id) {
    std::optional<letter_row> found;
    db << "SELECT id, letter_key, created_at, read_at FROM character_letters "
          "WHERE character_id = ? AND read_at = 0 "
          "ORDER BY created_at ASC, id ASC LIMIT 1;"
       << character_id
       >> [&](int64_t id, std::string letter_key, int64_t created_at, int64_t read_at) {
              letter_row row;
              row.id = id;
              row.letter_key = std::move(letter_key);
              row.created_at = created_at;
              row.read_at = read_at;
              found = std::move(row);
          };
    return found;
}

bool mark_letter_read(sqlite::database& db, int character_id, int64_t letter_id, int64_t now) {
    int owned = 0;
    db << "SELECT COUNT(*) FROM character_letters WHERE id = ? AND character_id = ?;"
       << letter_id << character_id
       >> [&](int c) { owned = c; };
    if (owned == 0) return false;

    db << "UPDATE character_letters SET read_at = ? WHERE id = ? AND character_id = ? AND read_at = 0;"
       << now << letter_id << character_id;
    return true;
}

} // namespace letters
