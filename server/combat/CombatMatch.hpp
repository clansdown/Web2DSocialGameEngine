#pragma once
#include "CombatPathfinding.hpp"
#include "CombatTypes.hpp"
#include "CombatantStats.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
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
//  - The sim thread reads ONLY the combat_match_config snapshot captured at
//    create (never GameConfigCache / SQLite).
class combat_match : public std::enable_shared_from_this<combat_match> {
public:
    combat_match(std::string match_id, std::string match_code, std::string mode,
                 combat_match_config config);

    // ---- Lobby phase (uWS loop thread) ----
    bool add_player(int64_t character_id, const std::string& display_name,
                    const std::vector<retinue_member_snapshot>& retinue);
    bool remove_player(int64_t character_id);
    void set_player_ready(int64_t character_id, bool ready);
    bool all_players_ready() const;
    void set_player_deploy_ready(int64_t character_id, bool ready);
    bool all_players_deploy_ready() const;
    // Roster select (rts_design.md §4.2): restrict a player's fielded members.
    // The knight can never be deselected (baked-in ruling).
    void set_player_roster(int64_t character_id, const std::vector<int64_t>& unit_ids);
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
    nlohmann::json ruleset() const { return config_.ruleset; }
    nlohmann::json map_metadata() const { return config_.map_metadata; }
    std::vector<combat_player> players_snapshot() const;
    nlohmann::json lobby_summary() const;

    // ---- Battle phase (match thread) ----
    void enqueue_command(const combat_command& cmd);
    // Abrupt server shutdown: ask run_battle to return promptly. A stopped
    // match persists no result (no XP / casualties / morale stamps).
    void request_stop() { stop_requested_.store(true, std::memory_order_relaxed); }
    // Runs until phase == ended. Called by the manager's worker thread.
    void run_battle(const std::function<void(const combat_message&)>& broadcast,
                    const std::function<void(const combat_match&)>& on_end);

    // Full-state snapshot for welcome / request_state (any thread).
    combat_message build_full_state() const;

    // Casualty records for persistence (any thread; called after battle ends).
    std::vector<casualty_record> casualties_snapshot() const;

    // The ruleset's death-handling mode: "permanent" | "wounded" |
    // "respawn_after_seconds" | "revive_resource".
    std::string death_handling() const { return death_handling_; }

    // The mission opportunity (warband fixed at create — D62) and the
    // rewards config (XP curve / victory multiplier).
    nlohmann::json opportunity() const { return config_.opportunity; }
    nlohmann::json rewards_config() const { return config_.rewards; }
    nlohmann::json player_combatants() const { return config_.player_combatants; }
    double xp_base() const { return config_.opportunity.value("xp_base", 0.0); }

    // The auto-generated offering id (empty for authored/legacy creates) and
    // its mission_attempt row id.
    std::string offering_id() const { return config_.offering_id; }
    int64_t mission_attempt_id() const { return config_.mission_attempt_id; }
    // Set once by the creator on the loop thread during the lobby phase (before
    // the match thread starts), after the attempt row is inserted.
    void set_mission_attempt_id(int64_t id) { config_.mission_attempt_id = id; }
    nlohmann::json milestone_conditions() const { return config_.milestone_conditions; }

    // Per-class enemy death and rout tallies accumulated during the match.
    // Enemies are removed from `units_` when they die or flee, so the counts are
    // kept separately (used for the slain.<class> progression counters).
    std::map<std::string, int> enemy_deaths() const;
    std::map<std::string, int> enemy_fled() const;

    // Match result for PvE: 1 = players win, 0 = players lose, -1 = draw
    // (rts_design.md §13.3). PvP later maps team ids.
    int winner() const { return winner_team_.load(); }

    int64_t ended_at_seconds() const { return ended_at_.load(); }

    // The set of players who forfeited mid-match (§3.5). The persistence layer
    // forces these players' result to a defeat regardless of team outcome.
    std::set<int64_t> forfeited_players() const;

    // ── Determinism replay (D27 / M10) ──
    // Record the config + seed + per-tick command log so a match can be re-run
    // tick-exactly and compared by state_hash.
    void enable_replay_log(bool enabled) { replay_log_enabled_ = enabled; }
    nlohmann::json replay_record() const;
    // Replay input: recorded commands/ready events are injected at their tick.
    void set_scripted_input(std::vector<nlohmann::json> commands,
                            std::vector<nlohmann::json> ready_events);

    // Determinism (D27): the match seed and a stable state hash for replay
    // comparison (same seed + same commands → identical hash).
    uint32_t seed() const { return seed_; }
    uint64_t state_hash() const;
    // Same hash, but assumes units_mutex_ is already held (used from
    // check_end_conditions/broadcast_ended, which hold it).
    uint64_t state_hash_unlocked() const;

private:
    void drain_commands();
    void apply_command(combat_command cmd);
    // Replay (M10): injects recorded commands/ready events due at the current tick.
    void inject_scripted_for_tick();
    bool replay_log_enabled_ = false;
    std::vector<nlohmann::json> command_log_;    // {tick, player_id, type, payload}
    std::vector<nlohmann::json> ready_log_;      // {tick, player_id, deploy}
    std::vector<nlohmann::json> scripted_commands_;
    std::vector<nlohmann::json> scripted_ready_;

    // rts_design.md §16.1 tick composition (ordered).
    void simulate_tick(double delta_seconds);
    void run_enemy_behavior(double delta_seconds);      // M4 profile-driven group AI
    bool enemy_style_flag(const std::string& unit_class, const std::string& key, bool def) const;
    double enemy_style_number(const std::string& unit_class, const std::string& key, double def) const;
    const combat_unit* nearest_player_to(double x, double y) const;
    std::pair<double, double> group_centroid(int group_index) const;
    bool group_tactic_is(const combat_unit& u, const std::string& tactic) const;
    void update_enemy_goals();                          // path goals for active groups
    void activate_enemy_group(int group_index, double contact_x, double contact_y,
                              const std::string& aggro_model);
    // Formations (§8.3): persistent movement modifier per player.
    void apply_formation_command(const combat_command& cmd);
    std::pair<double, double> formation_offset(const std::string& formation_id, int index,
                                               int count, double rotation) const;
    void acquire_targets();                             // both sides, per stance
    void move_units(double delta_seconds);              // order + waypoint movement
    void collide_units();                               // everything hard (§5.6)
    void combat_resolve(double delta_seconds);

    void spawn_all_units();
    void spawn_players();
    void spawn_warband();
    combat_unit make_player_unit(const combat_player& p, const retinue_member_snapshot& m,
                                 int index, double placement_x = -1.0,
                                 double placement_y = -1.0, double placement_facing = -999.0);
    combat_unit make_enemy_unit(const warband_spawn_entry& e, int instance_index, int group_index);

    // Temporary structure helpers (§7).
    bool try_start_construct(const combat_command& cmd);
    bool structure_placement_valid(int64_t owner_player_id, const std::string& type,
                                   double x, double y) const;
    int structure_count_owned_by(int64_t player_id, const std::string& type) const;
    void update_structures(double delta_seconds);
    void try_garrison_units();
    void expel_garrison(int64_t structure_id);
    void destroy_structure(int64_t structure_id);
    combat_structure* find_structure(int64_t id);
    const combat_structure* structure_at_point(double x, double y) const;
    bool structure_def_blocks(const combat_structure& s, const std::string& attack_type) const;
    bool structure_is_protected(int64_t structure_id, const std::string& type) const;
    bool structure_blocks_line(double ax, double ay, double bx, double by,
                               const std::string& attack_type) const;
    std::vector<bool> structure_blocked_mask() const;
    int64_t allocate_structure_id() { return next_structure_id_++; }

    void kill_unit(int64_t unit_id);
    // Forfeit (§3.5): remove a leaver's units UNWOUNDED (no casualty records),
    // empty their garrisons, free their channels, and mark them forfeited.
    void withdraw_player(int64_t character_id);
    // Build abort (§7.2): a new explicit order (or builder death) cancels the
    // channel and resets the structure's progress.
    void abort_build(int64_t structure_id);
    void execute_orders(combat_unit& u, double delta_seconds);
    void follow_waypoints(combat_unit& u, double delta_seconds);
    void start_path_for(combat_unit& u, const combat_order& order);
    void complete_current_order(combat_unit& u);
    void move_toward(combat_unit& u, double tx, double ty, double delta_seconds,
                     bool avoid_blocked = true);
    void face_toward(combat_unit& u, double tx, double ty, double delta_seconds);
    double rotation_to_target(const combat_unit& u, double tx, double ty) const;
    void set_facing(combat_unit& u, double target_angle);
    void issue_order(combat_unit& u, const combat_order& order);
    void clear_orders(combat_unit& u);
    std::string arc_for(const combat_unit& attacker, const combat_unit& target) const;
    bool in_attack_range(const combat_unit& u, const combat_unit& target) const;
    bool have_line_of_sight(const combat_unit& a, const combat_unit& b) const;
    void try_attack(combat_unit& u, combat_unit& target);
    double damage_for(const combat_unit& u) const;
    double buffed_range(const combat_unit& u) const;
    double buffed_damage(const combat_unit& u) const;
    void damage_structure(combat_structure& s, double dmg);
    // AoE / indirect fire (§6).
    bool find_safe_center(const combat_unit& u, double base_x, double base_y, double radius,
                          double& cx, double& cy) const;
    bool friendly_blocking_structure_in_blast(int team, double cx, double cy,
                                              double radius) const;
    void try_attack_aoe(combat_unit& u, double cx, double cy, std::vector<int64_t>& deaths);
    combat_unit* find_unit(int64_t id);
    const combat_unit* find_alive_target(const combat_unit& u, double radius) const;
    double acquisition_radius_for(const combat_unit& u) const;
    double pursuit_radius_for(const combat_unit& u) const;
    bool order_allows_engagement(const combat_unit& u) const;
    double distance(const combat_unit& a, const combat_unit& b) const;
    double distance_to(double ax, double ay, double bx, double by) const;
    bool tile_blocked(int tx, int ty) const;
    // Terrain movement-speed scale at a normalized point (§8.4): 1 on open
    // ground, larger on cost>1 terrain (slower traversal).
    double terrain_scale_at(double x, double y) const;
    double camp_angle_for(int team) const;
    bool point_in_deploy_zone(int team, double x, double y) const;

    void broadcast_state(const std::function<void(const combat_message&)>& broadcast);
    void check_end_conditions(const std::function<void(const combat_message&)>& broadcast);
    void finish_match(const std::string& reason);
    void broadcast_ended(const std::function<void(const combat_message&)>& broadcast);
    std::string phase_name_string() const;
    void mark_dirty(combat_unit& u) { dirty_units_.insert(u.unit_id); }
    double deploy_x_for(int team, int index) const;
    double deploy_y_for(int team) const;
    int alive_count_for_team(int team) const;
    std::set<int> active_teams() const;
    int match_duration_seconds() const;
    size_t warband_total() const;

    std::string match_id_;
    std::string match_code_;
    std::string mode_;
    combat_match_config config_;

    std::atomic<match_phase> phase_{match_phase::lobby};
    std::atomic<int> winner_team_{-1};
    std::atomic<int64_t> tick_{0};
    std::atomic<int64_t> ended_at_{0};
    std::atomic<double> countdown_remaining_{5.0};
    std::atomic<double> deploy_time_remaining_{-1.0};
    std::atomic<double> battle_time_{0.0};
    std::string end_reason_;

    int64_t next_sequence_ = 0;
    int max_players_ = 8;
    int max_units_per_player_ = 30;
    std::string death_handling_ = "permanent";
    int respawn_delay_seconds_ = 10;
    int duration_seconds_ = 1800;

    // Per-match seeded PRNG (D27): drives all dodge and AI randomness.
    std::mt19937 rng_{std::random_device{}()};
    uint32_t seed_ = 0;                 // recorded for replay reproduction
    pathfinding_grid path_grid_;
    int64_t next_enemy_unit_id_ = -1;
    int64_t next_structure_id_ = 9001;
    std::vector<bool> group_active_;    // per-warband-group "in battle" (§11.3)
    // Shared per-group flow field (rts_design.md §8.1) toward the group goal;
    // rebuilt ~1/s by update_enemy_goals for active groups.
    std::map<int, flow_field> group_flow_fields_;
    // Per-player persistent formation modifier: player_id -> {formation_id, rotation}.
    // Offsets apply to subsequent Move/Attack-Move orders until toggled off or
    // a non-formation order (D42). Guarded by units_mutex_ (command path).
    std::map<int64_t, std::pair<std::string, double>> active_formations_;

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
    std::map<int64_t, combat_structure> structures_;    // M3
    std::set<int64_t> dirty_units_;
    std::map<int64_t, double> respawn_timers_;          // unit_id -> seconds remaining
    // Deploy-phase placements: member_id -> ((x, y), facing) set via `deploy`
    // commands; absent members auto-deploy at the team zone center.
    std::map<int64_t, std::pair<std::pair<double, double>, double>> deployments_;
    std::vector<combat_event> outbox_;
    std::vector<int64_t> removed_this_tick_;
    std::vector<casualty_record> casualties_;
    std::map<std::string, int> enemy_deaths_;    // unit_class -> slain count
    std::map<std::string, int> enemy_fled_;      // unit_class -> fled count
    std::map<int64_t, double> flee_timers_;      // unit_id -> seconds broken
    bool ended_ = false;
    std::atomic<bool> stop_requested_{false};   // server shutdown (no result persist)
};

} // namespace combat