#pragma once
#include <nlohmann/json.hpp>
#include <sqlite_modern_cpp.h>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

struct MiniGameProgressRow {
    int id;
    int character_id;
    std::string mini_game;
    int level_id;
    bool completed;
    int best_score;
    int times_played;
    int64_t last_played;

    nlohmann::json toJson() const;
};

struct PlayerGameStateRow {
    int character_id;
    std::string game_phase;
    std::optional<std::string> current_mini_game;
    std::optional<int> current_level_id;
    bool base_unlocked;
    bool land_patent_acknowledged = false;
    std::optional<std::string> honor_name;
    int64_t entered_at;
    int64_t last_updated;
    std::vector<MiniGameProgressRow> progress;
    std::vector<std::string> available_activities;

    nlohmann::json toJson() const;
};

struct GameSessionRow {
    int id;
    int character_id;
    std::string mini_game;
    int level_id;
    int64_t started_at;
    int64_t last_activity;
    int total_rounds;
    int current_round;
    int difficulty;
    int lives;
    int gold;
    std::string state;
    std::string placements;

    nlohmann::json toJson() const;
};

struct WeedingSessionRow {
    int id;
    int character_id;
    int level_id;
    int64_t started_at;
    int64_t last_activity;
    std::string state;
    nlohmann::json session_json;
    nlohmann::json toJson() const;
};

struct EndMiniGameResult {
    bool completed;
    int new_times_played;
    int new_best_score;
    bool all_levels_done;
    bool base_unlocked;
};

namespace player_state_db {

PlayerGameStateRow get_player_game_state(sqlite::database& db, int character_id);

void create_player_game_state(sqlite::database& db, int character_id);

void start_mini_game(sqlite::database& db, int character_id, const std::string& mini_game, int level_id, int64_t timestamp);

EndMiniGameResult end_mini_game(sqlite::database& db, int character_id, const std::string& mini_game, int level_id, bool won, int score, int64_t timestamp, int expected_total_levels = 9);

bool has_completed_previous_level(sqlite::database& db, int character_id, const std::string& mini_game, int level_id);

std::optional<int> get_next_incomplete_level(sqlite::database& db, int character_id, const std::string& mini_game, int total_levels);

void unlock_base(sqlite::database& db, int character_id, int64_t timestamp);

void clear_current_mini_game(sqlite::database& db, int character_id, int64_t timestamp);

void earn_land_patent(sqlite::database& db, int character_id, int64_t timestamp);

void acknowledge_land_patent(sqlite::database& db, int character_id);

void start_baron_track(sqlite::database& db, int character_id, int64_t timestamp, const std::optional<std::string>& honor_name);

void earn_baron_right(sqlite::database& db, int character_id, int64_t timestamp);

// Game session management
GameSessionRow create_game_session(sqlite::database& db, int character_id, const std::string& mini_game, int level_id, int difficulty, int total_rounds, int64_t timestamp);

std::optional<GameSessionRow> get_game_session(sqlite::database& db, int session_id);

std::optional<GameSessionRow> get_active_session(sqlite::database& db, int character_id, const std::string& mini_game);

    bool update_game_session(sqlite::database& db, int session_id, int lives, int gold, const std::string& state, int64_t timestamp, const std::string& placements = "");

    void store_spawn_schedule(sqlite::database& db, int session_id, const nlohmann::json& schedule);

    nlohmann::json load_spawn_schedule(sqlite::database& db, int session_id);

    WeedingSessionRow create_weeding_session(sqlite::database& db, int character_id, int level_id, int64_t timestamp, const nlohmann::json& session_json);
    std::optional<WeedingSessionRow> get_weeding_session(sqlite::database& db, int session_id);
    std::optional<WeedingSessionRow> get_active_weeding_session(sqlite::database& db, int character_id);
    bool update_weeding_session(sqlite::database& db, int session_id, const nlohmann::json& session_json, const std::string& state, int64_t timestamp);

} // namespace player_state_db
