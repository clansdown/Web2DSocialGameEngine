#pragma once
#include <sqlite_modern_cpp.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// In-game letters (the king's missives and future notices). A letter is a
// text-system key plus timestamps; the client renders it through the
// character-text path (server-side gender / name / honor substitution). See
// server/tables/character_letters.md.
namespace letters {

struct letter_row {
    int64_t id = 0;
    std::string letter_key;
    int64_t created_at = 0;
    int64_t read_at = 0;   // 0 = unread
};

// Inserts a letter for the character and returns its id.
int64_t send_letter(sqlite::database& db, int character_id,
                    const std::string& letter_key, int64_t now);

// All letters for the character, newest first.
std::vector<letter_row> get_letters(sqlite::database& db, int character_id);

// True when the character has received at least one letter (drives the hub's
// conditional Letters button).
bool has_any_letter(sqlite::database& db, int character_id);

// The oldest unread letter, or nullopt when everything has been read.
std::optional<letter_row> oldest_unread_letter(sqlite::database& db, int character_id);

// Marks a letter read (idempotent). Returns false when the id does not belong
// to the character.
bool mark_letter_read(sqlite::database& db, int character_id, int64_t letter_id, int64_t now);

} // namespace letters
