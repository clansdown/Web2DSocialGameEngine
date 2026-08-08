#pragma once
#include "CombatTypes.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace combat {

// A single combat match. All battle state lives in RAM — never written to
// SQLite during play (results/casualties are persisted after the match ends).
//
// Threading contract:
//  - Lobby state (players_/ready flags) is owned by the uWS loop thread,
//    guarded by state_mutex_.
//  - The command queue is the ONLY shared structure between loop thread
//    (enqueue) and match thread (drain). The loop thread never waits on the
//    match thread.
//  - Battle state (units_, dirty set, outbox) is owned by the match thread,
//    guarded by units_mutex_ so the loop thread can build welcome/full-state
//    snapshots safely.
class combat_match : public std::enable_shared_from_this<combat_match> {
public:
    combat_match(std::string match_id, std::string match_code, std::string mode,
                 nlohmann::json ruleset, nlohmann::json map_metadata);

    // ---- Lobby phase (uWS loop thread) ----
    bool add_player(int64_t character_id, const std::string& display_name,
                    const std::vector<retinue_member_snapshot>& retinue);
    bool remove_player(int64_t character_id);
    void set_player_ready(int64_t character_id, bool ready);
    bool all_players_ready() const;
    bool contains_player(int64_t character_id) const;
    int player_team(int64_t character_id) const;
    std::string player_display_name(int64_t character_id) const;
    void set_player_connected(int64_t character_id, bool connected);
    int player_count() const;
    int max_players() const { return max_players_; }
    bool is_joinable() const;

    std::string id() const { return match_id_; }
    std::string code() const { return match_code_; }
    std::string mode() const { return mode_; }
    match_phase phase() const { return phase_.load(); }
    std::string ruleset_id() const;
    int64_t tick() const { return tick_.load(); }
    nlohmann::json ruleset() const { return ruleset_; }
    nlohmann::json map_metadata() const { return map_metadata_; }
    std::vector<combat_player> players_snapshot() const;
    nlohmann::json lobby_summary() const;

    // ---- Battle phase (match thread) ----
    void enqueue_command(const combat_command& cmd);
    // Runs until phase == ended. Called by the manager's worker thread.
    void run_battle(const std::function<void(const combat_message&)>& broadcast,
                    const std::function<void(const combat_match&)>& on_end);

    // Full-state snapshot for welcome / request_state (any thread).
    combat_message build_full_state() const;

    // Casualty records for persistence (any thread; called after battle ends).
    std::vector<casualty_record> casualties_snapshot() const;

    int64_t ended_at_seconds() const { return ended_at_.load(); }

private:
    void drain_commands();
    void apply_command(const combat_command& cmd);
    void simulate_tick(double delta_seconds);
    void spawn_all_units();
    void kill_unit(int64_t unit_id);
    void broadcast_state(const std::function<void(const combat_message&)>& broadcast);
    void check_end_conditions(const std::function<void(const combat_message&)>& broadcast);
    void finish_match(const std::string& reason);
    void broadcast_ended(const std::function<void(const combat_message&)>& broadcast);
    std::string phase_name_string() const;
    void mark_dirty(combat_unit& u) { dirty_units_.insert(u.unit_id); }
    double spawn_x_for(int team, int index) const;
    double spawn_y_for(int team) const;
    int alive_count_for_team(int team) const;
    std::set<int> active_teams() const;
    int match_duration_seconds() const;

    std::string match_id_;
    std::string match_code_;
    std::string mode_;
    nlohmann::json ruleset_;
    nlohmann::json map_metadata_;

    std::atomic<match_phase> phase_{match_phase::lobby};
    std::atomic<int> winner_team_{-1};
    std::atomic<int64_t> tick_{0};
    std::atomic<int64_t> ended_at_{0};
    std::atomic<double> countdown_remaining_{5.0};
    std::atomic<double> battle_time_{0.0};
    std::string end_reason_;

    int64_t next_sequence_ = 0;
    int max_players_ = 8;
    int max_units_per_player_ = 30;
    std::string death_handling_ = "permanent";
    int respawn_delay_seconds_ = 10;
    int duration_seconds_ = 1800;
    double unit_speed_ = 0.025;     // normalized map units per second

    // Lobby state (loop thread, guarded by state_mutex_)
    mutable std::mutex state_mutex_;
    std::vector<combat_player> players_;

    // Command queue (loop thread enqueues, match thread drains)
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<combat_command> command_queue_;

    // Battle state (match thread, guarded by units_mutex_)
    mutable std::mutex units_mutex_;
    std::map<int64_t, combat_unit> units_;
    std::set<int64_t> dirty_units_;
    std::map<int64_t, double> respawn_timers_;          // unit_id -> seconds remaining
    std::vector<combat_event> outbox_;
    std::vector<int64_t> removed_this_tick_;
    std::vector<casualty_record> casualties_;
    bool ended_ = false;
};

} // namespace combat
