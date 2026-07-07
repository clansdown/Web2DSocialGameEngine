#include "PlayerStateDB.hpp"
#include <iostream>
#include <chrono>

nlohmann::json MiniGameProgressRow::toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["character_id"] = character_id;
    j["mini_game"] = mini_game;
    j["level_id"] = level_id;
    j["completed"] = completed;
    j["best_score"] = best_score;
    j["times_played"] = times_played;
    j["last_played"] = last_played;
    return j;
}

nlohmann::json PlayerGameStateRow::toJson() const {
    nlohmann::json j;
    j["character_id"] = character_id;
    j["game_phase"] = game_phase;
    j["current_mini_game"] = current_mini_game ? nlohmann::json(*current_mini_game) : nlohmann::json(nullptr);
    j["current_level_id"] = current_level_id ? nlohmann::json(*current_level_id) : nlohmann::json(nullptr);
    j["base_unlocked"] = base_unlocked;
    j["entered_at"] = entered_at;
    j["last_updated"] = last_updated;

    nlohmann::json progress_arr = nlohmann::json::array();
    for (const auto& p : progress) {
        progress_arr.push_back(p.toJson());
    }
    j["progress"] = progress_arr;

    return j;
}

namespace player_state_db {

PlayerGameStateRow get_player_game_state(sqlite::database& db, int character_id) {
    PlayerGameStateRow state;
    state.character_id = character_id;

    db << "SELECT game_phase, current_mini_game, current_level_id, base_unlocked, entered_at, last_updated "
          "FROM player_game_state WHERE character_id = ?;"
       << character_id
       >> [&](std::string game_phase,
              std::optional<std::string> current_mini_game,
              std::optional<int> current_level_id,
              int base_unlocked,
              int64_t entered_at,
              int64_t last_updated) {
            state.game_phase = game_phase;
            state.current_mini_game = current_mini_game;
            state.current_level_id = current_level_id;
            state.base_unlocked = (base_unlocked != 0);
            state.entered_at = entered_at;
            state.last_updated = last_updated;
       };

    db << "SELECT id, character_id, mini_game, level_id, completed, best_score, times_played, last_played "
          "FROM mini_game_progress WHERE character_id = ? "
          "ORDER BY mini_game, level_id;"
       << character_id
       >> [&](int id, int cid, std::string mini_game, int level_id, int completed, int best_score, int times_played, int64_t last_played) {
            MiniGameProgressRow row;
            row.id = id;
            row.character_id = cid;
            row.mini_game = mini_game;
            row.level_id = level_id;
            row.completed = (completed != 0);
            row.best_score = best_score;
            row.times_played = times_played;
            row.last_played = last_played;
            state.progress.push_back(std::move(row));
       };

    return state;
}

void create_player_game_state(sqlite::database& db, int character_id) {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    db << "INSERT OR IGNORE INTO player_game_state (character_id, game_phase, base_unlocked, entered_at, last_updated) "
          "VALUES (?, 'initial_mission', 0, ?, ?);"
       << character_id << now << now;
}

void start_mini_game(sqlite::database& db, int character_id, const std::string& mini_game, int level_id, int64_t timestamp) {
    db << "UPDATE player_game_state SET "
          "current_mini_game = ?, current_level_id = ?, entered_at = ?, last_updated = ? "
          "WHERE character_id = ?;"
       << mini_game << level_id << timestamp << timestamp << character_id;
}

EndMiniGameResult end_mini_game(sqlite::database& db, int character_id, const std::string& mini_game, int level_id, bool won, int score, int64_t timestamp, int expected_total_levels) {
    EndMiniGameResult result;
    result.completed = won;
    result.all_levels_done = false;
    result.base_unlocked = false;

    if (won) {
        int existing_id = 0;
        int existing_completed = 0;
        int existing_best = 0;
        int existing_times = 0;

        db << "SELECT id, completed, best_score, times_played FROM mini_game_progress "
              "WHERE character_id = ? AND mini_game = ? AND level_id = ?;"
           << character_id << mini_game << level_id
           >> [&](int id, int completed, int best_score, int times_played) {
                existing_id = id;
                existing_completed = completed;
                existing_best = best_score;
                existing_times = times_played;
            };

        if (existing_id == 0) {
            db << "INSERT INTO mini_game_progress (character_id, mini_game, level_id, completed, best_score, times_played, last_played) "
                  "VALUES (?, ?, ?, 1, ?, 1, ?);"
               << character_id << mini_game << level_id << score << timestamp;

            result.new_times_played = 1;
            result.new_best_score = score;
        } else {
            int new_times = existing_times + 1;
            int new_best = std::max(existing_best, score);

            db << "UPDATE mini_game_progress SET completed = 1, best_score = ?, times_played = ?, last_played = ? "
                  "WHERE id = ?;"
               << new_best << new_times << timestamp << existing_id;

            result.new_times_played = new_times;
            result.new_best_score = new_best;
        }

        int total_levels = 0;
        int completed_levels = 0;

        db << "SELECT COUNT(*), SUM(completed) FROM mini_game_progress "
              "WHERE character_id = ? AND mini_game = ?;"
           << character_id << mini_game
           >> [&](int total, std::optional<int> completed_sum) {
                total_levels = total;
                completed_levels = completed_sum.value_or(0);
            };

        if (total_levels >= expected_total_levels && completed_levels >= expected_total_levels) {
            result.all_levels_done = true;
        }
    } else {
        int existing_times = 0;
        db << "SELECT times_played FROM mini_game_progress "
              "WHERE character_id = ? AND mini_game = ? AND level_id = ?;"
           << character_id << mini_game << level_id
           >> [&](int times_played) {
                existing_times = times_played;
            };

        if (existing_times == 0) {
            db << "INSERT INTO mini_game_progress (character_id, mini_game, level_id, completed, best_score, times_played, last_played) "
                  "VALUES (?, ?, ?, 0, 0, 1, ?);"
               << character_id << mini_game << level_id << timestamp;
            result.new_times_played = 1;
        } else {
            int new_times = existing_times + 1;
            db << "UPDATE mini_game_progress SET times_played = ?, last_played = ? "
                  "WHERE character_id = ? AND mini_game = ? AND level_id = ?;"
               << new_times << timestamp << character_id << mini_game << level_id;
            result.new_times_played = new_times;
        }
        result.new_best_score = 0;
    }

    return result;
}

bool has_completed_previous_level(sqlite::database& db, int character_id, const std::string& mini_game, int level_id) {
    if (level_id <= 1) {
        return true;
    }

    int completed = 0;
    db << "SELECT COALESCE(completed, 0) FROM mini_game_progress "
          "WHERE character_id = ? AND mini_game = ? AND level_id = ?;"
       << character_id << mini_game << (level_id - 1)
       >> [&](int c) { completed = c; };

    return completed != 0;
}

std::optional<int> get_next_incomplete_level(sqlite::database& db, int character_id, const std::string& mini_game, int total_levels) {
    for (int id = 1; id <= total_levels; ++id) {
        int completed = 0;
        db << "SELECT COALESCE(completed, 0) FROM mini_game_progress "
              "WHERE character_id = ? AND mini_game = ? AND level_id = ?;"
           << character_id << mini_game << id
           >> [&](int c) { completed = c; };

        if (completed == 0) {
            return id;
        }
    }

    return std::nullopt;
}

void unlock_base(sqlite::database& db, int character_id, int64_t timestamp) {
    db << "UPDATE player_game_state SET "
          "game_phase = 'sandbox', base_unlocked = 1, last_updated = ? "
          "WHERE character_id = ?;"
       << timestamp << character_id;
}

void clear_current_mini_game(sqlite::database& db, int character_id, int64_t timestamp) {
    db << "UPDATE player_game_state SET "
          "current_mini_game = NULL, current_level_id = NULL, last_updated = ? "
          "WHERE character_id = ?;"
       << timestamp << character_id;
}

void earn_land_patent(sqlite::database& db, int character_id, int64_t timestamp) {
    db << "UPDATE player_game_state SET "
          "game_phase = 'land_patent', last_updated = ? "
          "WHERE character_id = ?;"
       << timestamp << character_id;
}

void start_duke_track(sqlite::database& db, int character_id, int64_t timestamp) {
    db << "UPDATE player_game_state SET "
          "game_phase = 'duke_track', last_updated = ? "
          "WHERE character_id = ?;"
       << timestamp << character_id;
}

void earn_duke_right(sqlite::database& db, int character_id, int64_t timestamp) {
    db << "UPDATE player_game_state SET "
          "game_phase = 'duke_right', last_updated = ? "
          "WHERE character_id = ?;"
       << timestamp << character_id;
}

} // namespace player_state_db
