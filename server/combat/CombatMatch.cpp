#include "CombatMatch.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace combat {

namespace {
constexpr double k_ticks_per_second = 10.0;         // RTS command cadence — see docs/combat_protocol.md
constexpr int64_t k_full_snapshot_interval_ticks = 25;  // full state every 2.5 s as a sync guard
constexpr double k_countdown_seconds = 5.0;
constexpr double k_spawn_scatter = 0.02;            // normalized units between units at spawn
constexpr double k_pi = 3.14159265358979323846;

// rts_design.md §5.3: base speed in normalized units/second, multiplied by the
// combatant's `movement_speed` (tuning).
constexpr double k_base_speed = 0.04;
// Stance acquisition/pursuit radii (§10). Tuning constants.
constexpr double k_defensive_acquire = 0.35;
constexpr double k_aggressive_acquire = 0.70;
constexpr double k_defensive_pursuit = 0.50;
constexpr double k_aggressive_pursuit = 0.80;
} // namespace

combat_match::combat_match(std::string match_id, std::string match_code, std::string mode,
                           combat_match_config config)
    : match_id_(std::move(match_id)),
      match_code_(std::move(match_code)),
      mode_(std::move(mode)),
      config_(std::move(config))
{
    if (config_.ruleset.contains("players") && config_.ruleset["players"].contains("max")) {
        max_players_ = config_.ruleset["players"]["max"].get<int>();
    }
    if (config_.ruleset.contains("unit_caps") && config_.ruleset["unit_caps"].contains("per_player")) {
        max_units_per_player_ = config_.ruleset["unit_caps"]["per_player"].get<int>();
    }
    death_handling_ = config_.ruleset.value("death_handling", "permanent");
    respawn_delay_seconds_ = config_.ruleset.value("respawn_delay_seconds", 10);
    duration_seconds_ = config_.ruleset.value("match_duration_seconds", 1800);
    countdown_remaining_.store(k_countdown_seconds);
    path_grid_ = build_pathfinding_grid(config_.map_metadata);
    // Deterministic PRNG (D27): a non-zero configured seed makes the whole
    // match reproducible; otherwise seed from the OS entropy source.
    if (config_.seed != 0) {
        seed_ = config_.seed;
        rng_.seed(seed_);
    } else {
        seed_ = std::random_device{}();
        rng_.seed(seed_);
    }
}

// ────────────────────────── Lobby (loop thread) ──────────────────────────

bool combat_match::add_player(int64_t character_id, const std::string& display_name,
                              const std::vector<retinue_member_snapshot>& retinue) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (phase_.load() != match_phase::lobby) return false;
    for (const auto& p : players_) {
        if (p.player_id == character_id) return false;   // already in match
    }
    if (static_cast<int>(players_.size()) >= max_players_) return false;

    combat_player player;
    player.player_id = character_id;
    player.display_name = display_name;
    player.ready = false;
    player.connected = false;
    player.retinue = retinue;
    // PvE: everyone fights on team 1. PvP alternates teams (per-user team
    // selection arrives with the matchmaking system later).
    if (mode_ == "pvp") {
        int team_1_count = 0;
        for (const auto& p : players_) if (p.team == 1) team_1_count++;
        int team_2_count = static_cast<int>(players_.size()) - team_1_count;
        player.team = (team_1_count <= team_2_count) ? 1 : 2;
    } else {
        player.team = 1;
    }
    players_.push_back(std::move(player));
    return true;
}

bool combat_match::remove_player(int64_t character_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (phase_.load() != match_phase::lobby) return false;
    auto it = std::find_if(players_.begin(), players_.end(),
                           [&](const combat_player& p) { return p.player_id == character_id; });
    if (it == players_.end()) return false;
    players_.erase(it);
    return true;
}

void combat_match::set_player_ready(int64_t character_id, bool ready) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (auto& p : players_) {
        if (p.player_id == character_id) {
            p.ready = ready;
            break;
        }
    }
    if (replay_log_enabled_ && ready) {
        ready_log_.push_back({{"tick", tick_.load()}, {"player_id", character_id},
                              {"deploy", false}});
    }
}

bool combat_match::all_players_ready() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (players_.empty()) return false;
    for (const auto& p : players_) {
        if (!p.ready) return false;
    }
    return true;
}

void combat_match::set_player_deploy_ready(int64_t character_id, bool ready) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (auto& p : players_) {
        if (p.player_id == character_id) {
            p.deploy_ready = ready;
            break;
        }
    }
    if (replay_log_enabled_ && ready) {
        ready_log_.push_back({{"tick", tick_.load()}, {"player_id", character_id},
                              {"deploy", true}});
    }
}

bool combat_match::all_players_deploy_ready() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (players_.empty()) return false;
    for (const auto& p : players_) {
        if (!p.deploy_ready) return false;
    }
    return true;
}

void combat_match::set_player_roster(int64_t character_id, const std::vector<int64_t>& unit_ids) {
    // Roster select (§4.2): restrict the fielded snapshot to the chosen ids.
    // The knight is always fielded (baked-in ruling; a knight is also never in
    // `unit_ids` from the client, but keep it defensively).
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (phase_.load() != match_phase::lobby && phase_.load() != match_phase::deploy) return;
    for (auto& p : players_) {
        if (p.player_id != character_id) continue;
        if (unit_ids.empty()) return;   // empty = default all (no restriction)
        std::set<int64_t> chosen(unit_ids.begin(), unit_ids.end());
        std::vector<retinue_member_snapshot> filtered;
        for (const auto& m : p.retinue) {
            if (m.is_knight || chosen.count(m.member_id)) {
                filtered.push_back(m);
            }
        }
        if (filtered.empty()) return;   // don't allow fielding nothing
        p.retinue = std::move(filtered);
        return;
    }
}

bool combat_match::contains_player(int64_t character_id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto& p : players_) {
        if (p.player_id == character_id) return true;
    }
    return false;
}

int combat_match::player_team(int64_t character_id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto& p : players_) {
        if (p.player_id == character_id) return p.team;
    }
    return -1;
}

std::string combat_match::player_display_name(int64_t character_id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto& p : players_) {
        if (p.player_id == character_id) return p.display_name;
    }
    return "";
}

void combat_match::set_player_connected(int64_t character_id, bool connected) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (auto& p : players_) {
        if (p.player_id == character_id) {
            p.connected = connected;
            return;
        }
    }
}

int combat_match::player_count() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return static_cast<int>(players_.size());
}

bool combat_match::is_joinable() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return phase_.load() == match_phase::lobby &&
           static_cast<int>(players_.size()) < max_players_;
}

std::string combat_match::ruleset_id() const {
    return config_.ruleset.value("id", "");
}

std::vector<combat_player> combat_match::players_snapshot() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return players_;
}

nlohmann::json combat_match::lobby_summary() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    nlohmann::json summary;
    summary["match_id"] = match_id_;
    summary["match_code"] = match_code_;
    summary["mode"] = mode_;
    summary["ruleset_id"] = ruleset_id();
    summary["map_id"] = config_.map_metadata.value("name", "");
    summary["player_count"] = players_.size();
    summary["max_players"] = max_players_;
    summary["phase"] = "lobby";
    if (!config_.opportunity.is_null()) {
        summary["opportunity_id"] = config_.opportunity.value("id", "");
        summary["tier"] = config_.opportunity.value("tier", 1);
    }
    if (!players_.empty()) {
        summary["host_name"] = players_.front().display_name;
    }
    return summary;
}

// ────────────────────────── Command queue ──────────────────────────

void combat_match::enqueue_command(const combat_command& cmd) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        command_queue_.push_back(cmd);
    }
    queue_cv_.notify_one();
}

void combat_match::drain_commands() {
    std::deque<combat_command> pending;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pending.swap(command_queue_);
    }
    for (const auto& cmd : pending) {
        if (replay_log_enabled_) {
            nlohmann::json rec;
            rec["tick"] = tick_.load();
            rec["player_id"] = cmd.player_id;
            rec["type"] = cmd.type;
            rec["payload"] = cmd.payload;
            command_log_.push_back(std::move(rec));
        }
        apply_command(cmd);
    }
}

void combat_match::set_scripted_input(std::vector<nlohmann::json> commands,
                                      std::vector<nlohmann::json> ready_events) {
    scripted_commands_ = std::move(commands);
    scripted_ready_ = std::move(ready_events);
    std::stable_sort(scripted_commands_.begin(), scripted_commands_.end(),
                     [](const nlohmann::json& a, const nlohmann::json& b) {
                         return a.value("tick", (int64_t)-1) < b.value("tick", (int64_t)-1);
                     });
    std::stable_sort(scripted_ready_.begin(), scripted_ready_.end(),
                     [](const nlohmann::json& a, const nlohmann::json& b) {
                         return a.value("tick", (int64_t)-1) < b.value("tick", (int64_t)-1);
                     });
}

void combat_match::inject_scripted_for_tick() {
    const int64_t t = tick_.load();
    // Ready events due at or before this tick.
    for (auto it = scripted_ready_.begin(); it != scripted_ready_.end();) {
        if (it->value("tick", (int64_t)-1) > t) break;
        const int64_t pid = it->value("player_id", (int64_t)0);
        if (it->value("deploy", false)) {
            set_player_deploy_ready(pid, true);
        } else {
            set_player_ready(pid, true);
        }
        it = scripted_ready_.erase(it);
    }
    // Commands due at or before this tick, pushed onto the normal queue.
    while (!scripted_commands_.empty() &&
           scripted_commands_.front().value("tick", (int64_t)-1) <= t) {
        combat_command cmd;
        cmd.player_id = scripted_commands_.front().value("player_id", (int64_t)0);
        cmd.type = scripted_commands_.front().value("type", std::string());
        cmd.payload = scripted_commands_.front().value("payload", nlohmann::json::object());
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            command_queue_.push_back(std::move(cmd));
        }
        scripted_commands_.erase(scripted_commands_.begin());
    }
}

nlohmann::json combat_match::replay_record() const {
    nlohmann::json rec;
    rec["match_id"] = match_id_;
    rec["mode"] = mode_;
    rec["seed"] = seed_;
    rec["ruleset"] = config_.ruleset;
    rec["map"] = config_.map_metadata;
    rec["player_combatants"] = config_.player_combatants;
    rec["enemy_combatants"] = config_.enemy_combatants;
    rec["structures"] = config_.structures;
    rec["formations"] = config_.formations;
    rec["profiles"] = config_.profiles;
    rec["rewards"] = config_.rewards;
    rec["opportunity"] = config_.opportunity;
    nlohmann::json warband = nlohmann::json::array();
    for (const auto& e : config_.warband) {
        warband.push_back({{"unit_class", e.unit_class}, {"level", e.level},
                           {"count", e.count}, {"group", e.group},
                           {"tactic", e.tactic}, {"aggro_model", e.aggro_model}});
    }
    rec["warband"] = warband;
    nlohmann::json players = nlohmann::json::array();
    for (const auto& p : players_snapshot()) {
        nlohmann::json retinue = nlohmann::json::array();
        for (const auto& m : p.retinue) {
            retinue.push_back({{"member_id", m.member_id},
                               {"display_name", m.display_name},
                               {"unit_class", m.unit_class},
                               {"level", m.level},
                               {"is_knight", m.is_knight},
                               {"health", m.health}});
        }
        players.push_back({{"player_id", p.player_id},
                           {"display_name", p.display_name},
                           {"team", p.team},
                           {"retinue", retinue}});
    }
    rec["players"] = players;
    rec["commands"] = command_log_;
    rec["ready"] = ready_log_;
    rec["winner"] = winner_team_.load();
    rec["state_hash"] = state_hash();
    return rec;
}

// ────────────────────────── Battle (match thread) ──────────────────────────

void combat_match::run_battle(const std::function<void(const combat_message&)>& broadcast,
                              const std::function<void(const combat_match&)>& on_end)
{
    if (phase_.load() != match_phase::lobby) return;
    phase_.store(match_phase::deploy);
    deploy_time_remaining_.store(25.0);

    const auto tick_period = std::chrono::milliseconds(100);
    const double tick_seconds = std::chrono::duration<double>(tick_period).count();
    auto next_tick = std::chrono::steady_clock::now();

    while (!ended_ && !stop_requested_.load(std::memory_order_relaxed)) {
        next_tick += tick_period;
        inject_scripted_for_tick();
        drain_commands();
        simulate_tick(tick_seconds);
        broadcast_state(broadcast);
        check_end_conditions(broadcast);

        const auto now = std::chrono::steady_clock::now();
        if (now < next_tick) {
            std::this_thread::sleep_until(next_tick);
        } else {
            // Fell behind (blocked on lock or slow tick) — reset the schedule
            // instead of spinning to catch up.
            std::cerr << "[combat_match] tick behind schedule (match " << match_id_
                      << "), skipping catch-up" << std::endl;
            next_tick = now;
        }
    }

    // Abrupt server shutdown: do not persist a result for an aborted battle
    // (no XP / casualties / morale stamps). The process is going away anyway.
    if (stop_requested_.load(std::memory_order_relaxed) && !ended_) {
        phase_.store(match_phase::ended);
        return;
    }
    on_end(*this);
}

// ────────────────────────── Tick composition (§16.1) ──────────────────────────

void combat_match::simulate_tick(double delta_seconds) {
    tick_.fetch_add(1);
    const match_phase phase = phase_.load();

    if (phase == match_phase::deploy) {
        // rts_design.md §4.4 / §13.1: placement in the team deploy zone with a
        // facing default toward the enemy camp. Individual Ready ends the phase
        // early when all players are ready; otherwise auto-deploy on the timer.
        {
            std::lock_guard<std::mutex> lock(units_mutex_);
            deploy_time_remaining_.store(deploy_time_remaining_.load() - delta_seconds);
        }
        if (all_players_deploy_ready() || deploy_time_remaining_.load() <= 0.0) {
            {
                std::lock_guard<std::mutex> lock(units_mutex_);
                spawn_all_units();
            }
            phase_.store(match_phase::countdown);
            countdown_remaining_.store(k_countdown_seconds);
            outbox_.push_back({"system", {{"message", "deploy_complete"}}});
        }
        return;
    }

    if (phase == match_phase::countdown) {
        double remaining = countdown_remaining_.load() - delta_seconds;
        countdown_remaining_.store(remaining);
        if (remaining <= 0.0) {
            phase_.store(match_phase::battle);
            outbox_.push_back({"system", {{"message", "battle_started"}}});
        }
        return;
    }
    if (phase != match_phase::battle) return;

    std::lock_guard<std::mutex> lock(units_mutex_);
    battle_time_.store(battle_time_.load() + delta_seconds);

    // 3. Enemy behavior (M4 grows this; M1 relies on the symmetric defensive
    //    acquisition/combat below so idle-camping warbands fight back).
    run_enemy_behavior(delta_seconds);

    // 4. Targeting & acquisition (both sides).
    acquire_targets();

    // 5. Movement (order + waypoint following) then hard-body collision resolve.
    move_units(delta_seconds);
    collide_units();

    // 5b. Structures (§7): garrison entry, then advance channeled build progress.
    try_garrison_units();
    update_structures(delta_seconds);

    // 6. Combat resolve (cooldowns, arc lookup, damage + dodge, events).
    combat_resolve(delta_seconds);

    // 7. Respawns (rulesets with respawn_after_seconds) + end-condition check
    //    (the caller checks end conditions after broadcasting).
    if (!respawn_timers_.empty()) {
        for (auto it = respawn_timers_.begin(); it != respawn_timers_.end();) {
            it->second -= delta_seconds;
            if (it->second > 0.0) {
                ++it;
                continue;
            }
            auto unit_it = units_.find(it->first);
            if (unit_it != units_.end()) {
                combat_unit& u = unit_it->second;
                u.status = "alive";
                u.hp = u.max_hp;
                u.x = std::clamp(u.x, 0.0, 1.0);
                u.y = std::clamp(u.y, 0.0, 1.0);
                mark_dirty(u);
                outbox_.push_back({"unit_respawned", {{"unit_id", u.unit_id},
                                                      {"owner", u.owner_player_id}}});
            }
            it = respawn_timers_.erase(it);
        }
    }
}

void combat_match::run_enemy_behavior(double delta_seconds) {
    // rts_design.md §11 — profile system + warband battle state.
    if (config_.warband.empty()) return;

    // 1. Battle state (§11.3): any contact (acquired target or being damaged)
    // activates the group; the group's aggro_model propagates the alert.
    for (auto& [id, u] : units_) {
        if (u.status != "alive" || !u.is_enemy) continue;
        if (u.target_id == 0 && u.hp + 1e-9 >= u.max_hp) continue;
        if (u.warband_group < 0 || u.warband_group >= static_cast<int>(group_active_.size())) continue;
        if (group_active_[static_cast<size_t>(u.warband_group)]) continue;
        std::string aggro = config_.warband[static_cast<size_t>(u.warband_group)].aggro_model;
        if (aggro.empty()) aggro = "local";
        activate_enemy_group(u.warband_group, u.x, u.y, aggro);
    }

    // 1b. Rout (§3.2): an enemy that has broken (below its retreat threshold)
    //     and stayed broken for a few seconds flees the field — removed from
    //     `units_` and tallied as fled, so a warband of dead-or-fled enemies is
    //     annihilation. Deterministic (no RNG).
    static constexpr double k_flee_seconds = 6.0;
    for (auto it = units_.begin(); it != units_.end();) {
        combat_unit& u = it->second;
        if (u.status != "alive" || !u.is_enemy) { ++it; continue; }
        const double hp_pct = u.max_hp > 0.0 ? u.hp / u.max_hp : 0.0;
        const double retreat_threshold = enemy_style_number(u.unit_class, "retreat_threshold", 0.0);
        const bool broken = retreat_threshold > 0.0 && hp_pct < retreat_threshold;
        if (!broken) {
            flee_timers_.erase(u.unit_id);
            ++it;
            continue;
        }
        double& flee_time = flee_timers_[u.unit_id];
        flee_time += delta_seconds;
        if (flee_time >= k_flee_seconds) {
            outbox_.push_back({"unit_fled", {{"unit_id", u.unit_id},
                                             {"owner", 0}, {"is_enemy", true}}});
            enemy_fled_[u.unit_class]++;
            removed_this_tick_.push_back(u.unit_id);
            flee_timers_.erase(u.unit_id);
            it = units_.erase(it);
            continue;
        }
        ++it;
    }

    // 2. Path goals recomputed at a reduced cadence (~1/s) so large warbands
    // don't flood the A* every tick.
    if (tick_.load() % 10 != 0) return;
    update_enemy_goals();
}

void combat_match::activate_enemy_group(int group_index, double contact_x, double contact_y,
                                        const std::string& aggro_model) {
    if (group_index < 0 || group_index >= static_cast<int>(group_active_.size())) return;
    if (group_active_[static_cast<size_t>(group_index)]) return;
    group_active_[static_cast<size_t>(group_index)] = true;

    if (aggro_model == "global") {
        std::fill(group_active_.begin(), group_active_.end(), true);
        return;
    }
    if (aggro_model == "call_to_arms") {
        for (size_t g = 0; g < group_active_.size(); ++g) {
            if (group_active_[g] || g == static_cast<size_t>(group_index)) continue;
            const auto [cx, cy] = group_centroid(static_cast<int>(g));
            const double dx = cx - contact_x;
            const double dy = cy - contact_y;
            if (std::sqrt(dx * dx + dy * dy) <= 0.6) {
                const std::string sub =
                    config_.warband[g].aggro_model.empty() ? "local" : config_.warband[g].aggro_model;
                activate_enemy_group(static_cast<int>(g), cx, cy, sub);
            }
        }
    }
    // "local": only this group is raised.
}

bool combat_match::enemy_style_flag(const std::string& unit_class, const std::string& key,
                                    bool def) const {
    if (!config_.profiles.contains("class_styles") || !config_.profiles["class_styles"].is_object()) {
        return def;
    }
    const auto& styles = config_.profiles["class_styles"];
    if (!styles.contains(unit_class) || !styles[unit_class].is_object()) return def;
    const auto& st = styles[unit_class];
    if (!st.contains(key) || !st[key].is_boolean()) return def;
    return st[key].get<bool>();
}

double combat_match::enemy_style_number(const std::string& unit_class, const std::string& key,
                                        double def) const {
    if (!config_.profiles.contains("class_styles") || !config_.profiles["class_styles"].is_object()) {
        return def;
    }
    const auto& styles = config_.profiles["class_styles"];
    if (!styles.contains(unit_class) || !styles[unit_class].is_object()) return def;
    const auto& st = styles[unit_class];
    if (!st.contains(key) || !st[key].is_number()) return def;
    return st[key].get<double>();
}

const combat_unit* combat_match::nearest_player_to(double x, double y) const {
    const combat_unit* best = nullptr;
    double best_d = 2.0;   // whole map
    for (const auto& [id, u] : units_) {
        if (u.status != "alive" || u.is_enemy) continue;
        const double d = distance_to(x, y, u.x, u.y);
        if (d <= best_d) {
            best = &u;
            best_d = d;
        }
    }
    return best;
}

std::pair<double, double> combat_match::group_centroid(int group_index) const {
    double sx = 0.0, sy = 0.0;
    int count = 0;
    for (const auto& [id, u] : units_) {
        if (u.status != "alive" || !u.is_enemy) continue;
        if (u.warband_group != group_index) continue;
        sx += u.x;
        sy += u.y;
        count++;
    }
    if (count == 0) return {0.85, 0.5};
    return {sx / count, sy / count};
}

bool combat_match::group_tactic_is(const combat_unit& u, const std::string& tactic) const {
    if (u.warband_group < 0 || u.warband_group >= static_cast<int>(config_.warband.size())) {
        return false;
    }
    return config_.warband[static_cast<size_t>(u.warband_group)].tactic == tactic;
}

void combat_match::update_enemy_goals() {
    const double camp_x = deploy_x_for(2, 0);
    const double camp_y = deploy_y_for(2);
    const std::vector<bool> structure_mask = structure_blocked_mask();

    // §8.1: one shared flow field per active group toward the group's goal.
    // Guard/hold/reinforce groups make for camp; everyone else converges on the
    // nearest player to the group centroid. Units advance by sampling this
    // shared field, so a warband moves cohesively instead of every unit
    // planning its own A*.
    group_flow_fields_.clear();
    for (size_t g = 0; g < group_active_.size(); ++g) {
        if (!group_active_[g]) continue;
        const auto [cx, cy] = group_centroid(static_cast<int>(g));
        double goal_x = camp_x;
        double goal_y = camp_y;
        std::string tactic;
        if (g < config_.warband.size()) tactic = config_.warband[g].tactic;
        if (tactic != "guard" && tactic != "hold" && tactic != "reinforce") {
            const combat_unit* threat = nearest_player_to(cx, cy);
            if (threat) {
                goal_x = threat->x;
                goal_y = threat->y;
            }
        }
        group_flow_fields_[static_cast<int>(g)] =
            build_flow_field(path_grid_, goal_x, goal_y, &structure_mask);
    }

    for (auto& [id, u] : units_) {
        if (u.status != "alive" || !u.is_enemy) continue;
        if (u.warband_group < 0 || u.warband_group >= static_cast<int>(group_active_.size())) continue;
        if (!group_active_[static_cast<size_t>(u.warband_group)]) continue;
        if (u.garrison_structure_id != 0) continue;

        const combat_unit* threat = nearest_player_to(u.x, u.y);
        double goal_x = camp_x;
        double goal_y = camp_y;
        bool has_goal = false;
        bool use_flow = false;

        const double hp_pct = u.max_hp > 0.0 ? u.hp / u.max_hp : 0.0;
        const double retreat_threshold = enemy_style_number(u.unit_class, "retreat_threshold", 0.0);

        if (threat) {
            double d = distance_to(u.x, u.y, threat->x, threat->y);

            if (retreat_threshold > 0.0 && hp_pct < retreat_threshold) {
                // Fall back away from the enemy (orc-style), toward camp.
                const double dx = u.x - threat->x;
                const double dy = u.y - threat->y;
                const double len = std::sqrt(dx * dx + dy * dy);
                if (len > 1e-6) {
                    goal_x = std::clamp(u.x + dx / len * 0.2, 0.0, 1.0);
                    goal_y = std::clamp(u.y + dy / len * 0.15, 0.0, 1.0);
                    has_goal = true;
                }
            } else if (enemy_style_flag(u.unit_class, "surround_to_expose_backs", false)) {
                // Goblin swarm (§11.1/§11.4): fan out behind the target's facing.
                const double base = std::atan2(u.y - threat->y, u.x - threat->x);
                const double spread = ((-u.unit_id % 5) * 0.45);   // deterministic fan
                const double angle = base + spread;
                goal_x = std::clamp(threat->x + std::cos(angle) * 0.12, 0.0, 1.0);
                goal_y = std::clamp(threat->y + std::sin(angle) * 0.12, 0.0, 1.0);
                has_goal = true;
            } else if (u.stats.attack_type == "ranged" || u.stats.attack_type == "magical") {
                // Skirmishers / backline keep range (bandit, dark mage, lobber).
                const double keep = std::max(0.14, u.stats.attack_range * 0.85);
                if (d < keep) {
                    const double dx = u.x - threat->x;
                    const double dy = u.y - threat->y;
                    const double len = std::sqrt(dx * dx + dy * dy);
                    if (len > 1e-6) {
                        goal_x = std::clamp(u.x + dx / len * (keep - d), 0.0, 1.0);
                        goal_y = std::clamp(u.y + dy / len * (keep - d), 0.0, 1.0);
                        has_goal = true;
                    }
                }
            } else if (group_tactic_is(u, "guard")) {
                // Orc shield-wall: hold the camp; engage approaches only.
                const double cd = distance_to(u.x, u.y, camp_x, camp_y);
                if (cd > 0.1) {
                    goal_x = camp_x;
                    goal_y = camp_y;
                    has_goal = true;
                }
            } else {
                // Charge (trolls, orc sell-swords): advance via the shared
                // group flow field (§8.1) for cohesion.
                goal_x = threat->x;
                goal_y = threat->y;
                has_goal = true;
                use_flow = true;
            }
        }

        if (has_goal) {
            if (use_flow) {
                auto fit = group_flow_fields_.find(u.warband_group);
                if (fit != group_flow_fields_.end() && fit->second.valid) {
                    u.use_flow_field = true;
                    u.waypoints.clear();
                    mark_dirty(u);
                    continue;
                }
            }
            u.use_flow_field = false;
            u.waypoints = find_path(path_grid_, u.x, u.y, goal_x, goal_y, &structure_mask);
            if (!u.waypoints.empty()) {
                mark_dirty(u);
                continue;
            }
        }

        // Siege (§11.2): a blocks_ranged structure fully blocking the approach —
        // attack it rather than wandering.
        if (u.stats.attack_type == "melee") {
            const combat_structure* blocker = nullptr;
            double best_bd = 1e9;
            for (const auto& [sid, s] : structures_) {
                if (s.state == "destroyed") continue;
                if (!structure_def_blocks(s, "physical")) continue;
                const double bd = distance_to(u.x, u.y, s.x, s.y);
                if (bd < best_bd) {
                    best_bd = bd;
                    blocker = &s;
                }
            }
            if (blocker && best_bd < 0.5) {
                u.target_id = blocker->id;
                u.waypoints = find_path(path_grid_, u.x, u.y, blocker->x, blocker->y, &structure_mask);
                if (u.waypoints.empty()) u.waypoints.clear();
                mark_dirty(u);
            }
        }
    }
}

// ────────────────────────── Spawning ──────────────────────────

void combat_match::spawn_all_units() {
    spawn_players();
    spawn_warband();
}

void combat_match::spawn_players() {
    std::vector<combat_player> players;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        players = players_;
    }
    for (const auto& p : players) {
        int index = 0;
        for (const auto& m : p.retinue) {
            if (index >= max_units_per_player_) break;
            if (m.member_id == 0) continue;
            double sx = -1.0, sy = -1.0, sf = -999.0;
            auto it = deployments_.find(m.member_id);
            if (it != deployments_.end()) {
                sx = it->second.first.first;
                sy = it->second.first.second;
                sf = it->second.second;
            }
            combat_unit u = make_player_unit(p, m, index, sx, sy, sf);
            units_[u.unit_id] = std::move(u);
            combat_unit& placed = units_[m.member_id];
            dirty_units_.insert(placed.unit_id);
            outbox_.push_back({"unit_spawned", {{"unit_id", placed.unit_id},
                                                {"owner", placed.owner_player_id},
                                                {"team", placed.team},
                                                {"name", placed.display_name},
                                                {"unit_class", placed.unit_class},
                                                {"is_enemy", false}}});
            index++;
        }
    }
}

void combat_match::spawn_warband() {
    int instance = 0;
    int group_index = 0;
    for (const auto& e : config_.warband) {
        for (int i = 0; i < e.count; ++i) {
            combat_unit u = make_enemy_unit(e, instance++, group_index);
            const int64_t id = u.unit_id;
            units_[id] = std::move(u);
            combat_unit& placed = units_[id];
            dirty_units_.insert(id);
            outbox_.push_back({"unit_spawned", {{"unit_id", id},
                                                {"owner", placed.owner_player_id},
                                                {"team", placed.team},
                                                {"name", placed.display_name},
                                                {"unit_class", placed.unit_class},
                                                {"is_enemy", true}}});
        }
        group_index++;
    }
    group_active_.assign(config_.warband.size(), false);
}

combat_unit combat_match::make_player_unit(const combat_player& p,
                                           const retinue_member_snapshot& m, int index,
                                           double placement_x, double placement_y,
                                           double placement_facing) {
    combat_unit u;
    u.unit_id = m.member_id;
    u.owner_player_id = p.player_id;
    u.team = p.team;
    u.display_name = m.display_name;
    u.unit_class = m.unit_class;
    u.level = m.level;
    u.is_enemy = false;
    u.stance = "defensive";
    u.stats = resolve_combatant_stats(config_.player_combatants, m.unit_class, m.level);
    // HP bridge (rts_design.md §5.2): match HP = class max_hp × roster health%.
    const double health_frac = std::clamp(m.health, 0.0, 100.0) / 100.0;
    u.max_hp = std::max(1.0, u.stats.max_hp * health_frac);
    u.hp = u.max_hp;
    if (placement_x >= 0.0) {
        u.x = placement_x;
        u.y = placement_y;
        u.facing = placement_facing == -999.0 ? camp_angle_for(p.team) : placement_facing;
    } else {
        // Auto-deploy at the team zone center, facing the enemy camp.
        u.x = deploy_x_for(p.team, index);
        u.y = deploy_y_for(p.team);
        u.facing = camp_angle_for(p.team);
    }
    return u;
}

combat_unit combat_match::make_enemy_unit(const warband_spawn_entry& e, int instance_index,
                                          int group_index) {
    combat_unit u;
    u.unit_id = next_enemy_unit_id_--;      // negative id namespace (§13.3)
    u.owner_player_id = 0;                  // environment owner
    u.team = 2;
    u.display_name = e.unit_class;
    u.unit_class = e.unit_class;
    u.level = e.level;
    u.is_enemy = true;
    u.stance = "defensive";
    u.warband_group = group_index;
    u.stats = resolve_combatant_stats(config_.enemy_combatants, e.unit_class, e.level);
    u.max_hp = std::max(1.0, u.stats.max_hp);
    u.hp = u.max_hp;
    u.x = deploy_x_for(2, instance_index);
    u.y = deploy_y_for(2);
    u.facing = camp_angle_for(2);
    return u;
}

// ────────────────────────── Temporary structures (§7) ──────────────────────────

std::vector<bool> combat_match::structure_blocked_mask() const {
    // Cell mask of live (building + active) structures — true impassable path
    // obstacles (§8.1/§8.2). Rebuilt on demand; grids are small.
    std::vector<bool> mask;
    if (path_grid_.width <= 0 || path_grid_.height <= 0) return mask;
    mask.assign(static_cast<size_t>(path_grid_.width) * path_grid_.height, false);
    for (const auto& [id, s] : structures_) {
        if (s.state == "destroyed") continue;
        const int tx = std::clamp(static_cast<int>(s.x * path_grid_.width), 0, path_grid_.width - 1);
        const int ty = std::clamp(static_cast<int>(s.y * path_grid_.height), 0, path_grid_.height - 1);
        const int fw = std::max(1, s.footprint);
        for (int dy = 0; dy < fw; ++dy) {
            for (int dx = 0; dx < fw; ++dx) {
                const int ny = std::min(ty + dy, path_grid_.height - 1);
                const int nx = std::min(tx + dx, path_grid_.width - 1);
                mask[static_cast<size_t>(ny) * path_grid_.width + nx] = true;
            }
        }
    }
    return mask;
}

int combat_match::structure_count_owned_by(int64_t player_id, const std::string& type) const {
    int count = 0;
    for (const auto& [id, s] : structures_) {
        if (s.state == "destroyed") continue;
        if (s.owner_player_id == player_id && s.type == type) count++;
    }
    return count;
}

const combat_structure* combat_match::structure_at_point(double x, double y) const {
    for (const auto& [id, s] : structures_) {
        if (s.state == "destroyed") continue;
        const double dx = x - s.x;
        const double dy = y - s.y;
        if (std::sqrt(dx * dx + dy * dy) <= 0.05) return &s;
    }
    return nullptr;
}

bool combat_match::structure_placement_valid(int64_t owner_player_id, const std::string& type,
                                             double x, double y) const {
    if (!config_.structures.contains("structures") || !config_.structures["structures"].is_object()) {
        return false;
    }
    const auto& defs = config_.structures["structures"];
    if (!defs.contains(type) || !defs[type].is_object()) return false;
    const auto& def = defs[type];

    // Per-player cap (D37).
    const int cap = def.value("max_per_player", 99);
    if (structure_count_owned_by(owner_player_id, type) >= cap) return false;

    // Must be an open, passable, un-blocked tile.
    const int tx = static_cast<int>(x * path_grid_.width);
    const int ty = static_cast<int>(y * path_grid_.height);
    if (tile_blocked(tx, ty)) return false;
    if (structure_at_point(x, y) != nullptr) return false;
    // Map obstacles block placement too (§7.3): a boulder/hedge is no build site.
    if (config_.map_metadata.contains("obstacles") &&
        config_.map_metadata["obstacles"].is_array()) {
        for (const auto& obs : config_.map_metadata["obstacles"]) {
            if (!obs.is_object()) continue;
            const double ox = obs.value("x", -1.0);
            const double oy = obs.value("y", -1.0);
            const double r = obs.value("radius", 0.06);
            if (std::hypot(x - ox, y - oy) <= r) return false;
        }
    }
    for (const auto& [id, u] : units_) {
        if (u.status != "alive") continue;
        const double dx = u.x - x;
        const double dy = u.y - y;
        if (std::sqrt(dx * dx + dy * dy) <= 0.06) return false;   // on a unit
    }
    return true;
}

combat_structure* combat_match::find_structure(int64_t id) {
    auto it = structures_.find(id);
    return it == structures_.end() ? nullptr : &it->second;
}

bool combat_match::try_start_construct(const combat_command& cmd) {
    std::lock_guard<std::mutex> lock(units_mutex_);
    if (phase_.load() != match_phase::battle) return false;
    if (!config_.structures.contains("structures") || !config_.structures["structures"].is_object()) {
        return false;
    }
    const std::string type = cmd.payload.value("structure_type", "");
    if (type.empty()) return false;
    const auto& defs = config_.structures["structures"];
    if (!defs.contains(type) || !defs[type].is_object()) return false;

    std::vector<int64_t> unit_ids;
    if (cmd.payload.contains("unit_ids") && cmd.payload["unit_ids"].is_array()) {
        for (const auto& id : cmd.payload["unit_ids"]) {
            if (id.is_number_integer()) unit_ids.push_back(id.get<int64_t>());
        }
    }
    double x = cmd.payload.value("x", -1.0);
    double y = cmd.payload.value("y", -1.0);
    if (unit_ids.empty() || x < 0.0 || y < 0.0) return false;
    x = std::clamp(x, 0.0, 1.0);
    y = std::clamp(y, 0.0, 1.0);

    const auto& def = defs[type];
    if (!structure_placement_valid(cmd.player_id, type, x, y)) return false;

    combat_structure s;
    s.id = allocate_structure_id();
    s.owner_player_id = cmd.player_id;
    s.team = 1;   // players construct (v1)
    s.type = type;
    s.x = x;
    s.y = y;
    s.footprint = def.value("footprint", 1);
    s.max_hp = def.value("max_hp", 100.0);
    s.hp = 0.0;
    s.state = "building";   // solid immediately (D58)
    structures_[s.id] = s;

    outbox_.push_back({"structure_spawned", {{"id", s.id},
                                             {"type", s.type},
                                             {"owner", s.owner_player_id},
                                             {"team", s.team},
                                             {"x", s.x},
                                             {"y", s.y},
                                             {"max_hp", s.max_hp}}});

    // Issue construct orders to the selected builders.
    for (int64_t id : unit_ids) {
        auto uit = units_.find(id);
        if (uit == units_.end()) continue;
        combat_unit& u = uit->second;
        if (u.owner_player_id != cmd.player_id || u.status != "alive") continue;
        combat_order order;
        order.type = combat_order_type::construct;
        order.target_x = x;
        order.target_y = y;
        order.target_unit_id = s.id;
        issue_order(u, order);
        mark_dirty(u);
    }
    return true;
}

void combat_match::update_structures(double dt) {
    // Channeled build (§7.2): up to ~2 builders each add build rate; damage
    // taken persists; reaching 0 HP mid-build destroys the structure.
    constexpr int k_max_builders = 2;
    std::vector<int64_t> destroyed;
    for (auto& [id, s] : structures_) {
        if (s.state == "destroyed") continue;
        if (s.state == "building") {
            int builders = 0;
            for (const auto& [uid, u] : units_) {
                if (u.status != "alive") continue;
                if (u.channeling_structure_id != s.id) continue;
                const double dx = u.x - s.x;
                const double dy = u.y - s.y;
                if (std::sqrt(dx * dx + dy * dy) <= 0.15) builders++;
            }
            builders = std::min(builders, k_max_builders);
            if (builders > 0) {
                double build_time = 10.0;
                if (config_.structures.contains("structures") &&
                    config_.structures["structures"].is_object() &&
                    config_.structures["structures"].contains(s.type) &&
                    config_.structures["structures"][s.type].is_object()) {
                    build_time =
                        config_.structures["structures"][s.type].value("build_time_seconds", 10.0);
                }
                const double rate = (build_time > 0.0) ? s.max_hp / build_time : s.max_hp;
                s.hp = std::min(s.max_hp, s.hp + rate * static_cast<double>(builders) * dt);
                if (s.hp >= s.max_hp) {
                    s.hp = s.max_hp;
                    s.state = "active";
                    outbox_.push_back({"structure_built", {{"id", s.id},
                                                           {"type", s.type},
                                                           {"owner", s.owner_player_id}}});
                }
            }
        }
        // Damage persistence: only a structure damaged down to 0 HP is destroyed
        // (§7.2). A fresh build sits at 0 HP until builders channel it — that
        // pre-build state must not count as destruction.
        if (s.hp <= 0.0 && s.damaged) destroyed.push_back(s.id);
    }
    for (int64_t sid : destroyed) destroy_structure(sid);
}

void combat_match::destroy_structure(int64_t structure_id) {
    auto it = structures_.find(structure_id);
    if (it == structures_.end()) return;
    combat_structure& s = it->second;
    if (s.state == "destroyed") return;
    s.state = "destroyed";

    // Free any channeling builders.
    for (auto& [uid, u] : units_) {
        if (u.channeling_structure_id == s.id) {
            u.channeling_structure_id = 0;
            clear_orders(u);
        }
    }
    // Expel the garrisoned archer at the tower footprint (§7.4).
    expel_garrison(s.id);

    outbox_.push_back({"structure_destroyed", {{"id", s.id},
                                               {"type", s.type},
                                               {"owner", s.owner_player_id}}});
    structures_.erase(it);
}

void combat_match::expel_garrison(int64_t structure_id) {
    auto it = structures_.find(structure_id);
    if (it == structures_.end()) return;
    combat_structure& s = it->second;
    const int64_t garrisoned = s.garrisoned_unit_id;
    s.garrisoned_unit_id = 0;
    if (garrisoned == 0) return;
    auto uit = units_.find(garrisoned);
    if (uit == units_.end()) return;
    combat_unit& u = uit->second;
    if (u.garrison_structure_id != s.id) return;
    u.garrison_structure_id = 0;
    // Expelled at current HP, standing at the tower footprint (the structure
    // vacated the tile — no "no free space" case).
    u.x = s.x;
    u.y = s.y;
    mark_dirty(u);
    outbox_.push_back({"unit_expelled", {{"unit_id", u.unit_id},
                                         {"structure_id", s.id}}});
}

void combat_match::try_garrison_units() {
    // (§7.4) Moving an archer onto a completed tower auto-garrisons it.
    for (auto& [uid, u] : units_) {
        if (u.status != "alive") continue;
        if (u.garrison_structure_id != 0) continue;
        if (u.is_enemy) continue;   // players garrison (v1)
        const combat_structure* s = structure_at_point(u.x, u.y);
        if (!s || s->state != "active") continue;
        if (!config_.structures.contains("structures") || !config_.structures["structures"].is_object()) {
            continue;
        }
        const auto& def = config_.structures["structures"][s->type];
        const auto* garrison_cfg = def.contains("garrison") && def["garrison"].is_object()
                                       ? &def["garrison"]
                                       : nullptr;
        if (!garrison_cfg) continue;
        const int capacity = garrison_cfg->value("capacity", 1);
        if (capacity < 1) continue;
        if (s->garrisoned_unit_id != 0) continue;
        const auto allowed = garrison_cfg->value("allowed_classes", nlohmann::json::array());
        bool class_ok = allowed.empty();   // empty = any class
        for (const auto& c : allowed) {
            if (c.is_string() && c == u.unit_class) class_ok = true;
        }
        if (!class_ok) continue;
        // Garrison: snap into the tower.
        u.garrison_structure_id = s->id;
        u.x = s->x;
        u.y = s->y;
        u.target_id = 0;
        clear_orders(u);
        structures_[s->id].garrisoned_unit_id = u.unit_id;
        mark_dirty(u);
        outbox_.push_back({"unit_garrisoned", {{"unit_id", u.unit_id},
                                               {"structure_id", s->id}}});
    }
}

bool combat_match::structure_def_blocks(const combat_structure& s,
                                        const std::string& attack_type) const {
    if (!config_.structures.contains("structures") || !config_.structures["structures"].is_object()) {
        return false;
    }
    const auto& defs = config_.structures["structures"];
    if (!defs.contains(s.type) || !defs[s.type].is_object()) return false;
    const auto& def = defs[s.type];
    if (attack_type == "magical") return def.value("blocks_magical_ranged", false);
    return def.value("blocks_physical_ranged", false);
}

bool combat_match::structure_blocks_line(double ax, double ay, double bx, double by,
                                         const std::string& attack_type) const {
    // rts_design.md §5.7/§6.4: a blocks_ranged structure between attacker and
    // target stops the shot (arrow visual hits the structure). Magic is blocked
    // by walls by default (penetrates_walls authoring later).
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9) return false;
    const double ux = dx / len;
    const double uy = dy / len;
    for (const auto& [id, s] : structures_) {
        if (s.state == "destroyed") continue;
        if (!structure_def_blocks(s, attack_type)) continue;
        // Distance from structure center to the ray; block within radius.
        const double relx = ax - s.x;
        const double rely = ay - s.y;
        const double proj = relx * ux + rely * uy;
        if (proj < 0.0 || proj > len) continue;
        const double perp_sq = (s.y - (ay - uy * proj)) * (s.y - (ay - uy * proj)) +
                               (s.x - (ax - ux * proj)) * (s.x - (ax - ux * proj));
        if (perp_sq <= 0.03 * 0.03) return true;
    }
    return false;
}

// ────────────────────────── Orders & commands ──────────────────────────

void combat_match::issue_order(combat_unit& u, const combat_order& order) {
    if (order.queued) {
        u.orders.push_back(order);
        return;
    }
    // §7.2/§9: a new explicit order cancels any channel and aborts the build
    // (queued orders only run after a channel completes, so they don't).
    if (u.channeling_structure_id != 0) {
        abort_build(u.channeling_structure_id);
    }
    clear_orders(u);
    u.orders.push_back(order);
}

void combat_match::clear_orders(combat_unit& u) {
    u.orders.clear();
    u.waypoints.clear();
}

void combat_match::complete_current_order(combat_unit& u) {
    if (u.orders.empty()) return;
    if (u.orders.front().type == combat_order_type::move) {
        u.target_id = 0;   // Move never auto-engages (D41)
    }
    u.orders.pop_front();
}

std::pair<double, double> combat_match::formation_offset(const std::string& formation_id,
                                                         int index, int count,
                                                         double rotation) const {
    double ox = 0.0, oy = 0.0;
    if (config_.formations.contains("formations") && config_.formations["formations"].is_object() &&
        config_.formations["formations"].contains(formation_id) &&
        config_.formations["formations"][formation_id].is_object()) {
        const auto& fm = config_.formations["formations"][formation_id];
        const std::string shape = fm.value("shape", "line");
        const double spacing = fm.value("spacing", 0.04);
        const double row_spacing = fm.value("row_spacing", spacing);
        const double radius_step = fm.value("radius_step", 0.05);
        if (shape == "line") {
            ox = (index - (count - 1) / 2.0) * spacing;
        } else if (shape == "column") {
            oy = (index - (count - 1) / 2.0) * spacing;
        } else if (shape == "wedge") {
            // Row r holds 2r+1 units, depth = r*row_spacing.
            int row = 0, row_start = 0, row_size = 1;
            while (row_start + row_size <= index) {
                row_start += row_size;
                row++;
                row_size = 2 * row + 1;
            }
            ox = (index - row_start - row) * spacing;   // -row..row lateral
            oy = row * row_spacing;
        } else if (shape == "circle") {
            const double radius = radius_step * (1.0 + (count - 1) / 8.0);
            const double angle = 2.0 * k_pi * index / count;
            ox = std::cos(angle) * radius;
            oy = std::sin(angle) * radius;
        }
    }
    // Rotate the offset by the formation's world-space handle (§8.3).
    const double cosr = std::cos(rotation);
    const double sinr = std::sin(rotation);
    return {ox * cosr - oy * sinr, ox * sinr + oy * cosr};
}

void combat_match::apply_formation_command(const combat_command& cmd) {
    std::lock_guard<std::mutex> lock(units_mutex_);
    const std::string id = cmd.payload.value("formation_id", "");
    const double rotation = cmd.payload.value("rotation", 0.0);
    if (id.empty() || id == "none") {
        active_formations_.erase(cmd.player_id);
    } else {
        active_formations_[cmd.player_id] = {id, rotation};
    }
}

void combat_match::apply_command(combat_command cmd) {
    // Normalize a null payload to an empty object: clients or replay records may
    // carry `payload: null`, and json::value() throws type_error.306 on null.
    if (!cmd.payload.is_object()) {
        cmd.payload = nlohmann::json::object();
    }
    const auto parse_ids = [&cmd]() -> std::vector<int64_t> {
        std::vector<int64_t> ids;
        if (cmd.payload.contains("unit_ids") && cmd.payload["unit_ids"].is_array()) {
            for (const auto& id : cmd.payload["unit_ids"]) {
                if (id.is_number_integer()) ids.push_back(id.get<int64_t>());
            }
        }
        return ids;
    };

    const bool queued = cmd.payload.value("queued", false);

    if (cmd.type == "move" || cmd.type == "attack_move" || cmd.type == "patrol") {
        const auto unit_ids = parse_ids();
        double tx = cmd.payload.value("target_x", -1.0);
        double ty = cmd.payload.value("target_y", -1.0);
        if (unit_ids.empty() || tx < 0.0 || ty < 0.0) return;
        tx = std::clamp(tx, 0.0, 1.0);
        ty = std::clamp(ty, 0.0, 1.0);
        std::lock_guard<std::mutex> lock(units_mutex_);
        std::vector<int64_t> ordered = unit_ids;
        // §8.3: a persistent formation shapes the move — assign slot offsets.
        std::string fid;
        double rotation = 0.0;
        auto fit = active_formations_.find(cmd.player_id);
        if (fit != active_formations_.end()) {
            fid = fit->second.first;
            rotation = fit->second.second;
        }
        std::vector<std::pair<double, double>> targets;
        if (!fid.empty()) {
            // Stable slot order: nearest units take the front slots.
            std::sort(ordered.begin(), ordered.end(), [&](int64_t a, int64_t b) {
                auto ai = units_.find(a);
                auto bi = units_.find(b);
                const double da = (ai != units_.end())
                    ? distance_to(ai->second.x, ai->second.y, tx, ty) : 1e9;
                const double db = (bi != units_.end())
                    ? distance_to(bi->second.x, bi->second.y, tx, ty) : 1e9;
                return da < db;
            });
            for (size_t i = 0; i < ordered.size(); ++i) {
                const auto [ox, oy] = formation_offset(fid, static_cast<int>(i),
                                                       static_cast<int>(ordered.size()), rotation);
                targets.push_back({std::clamp(tx + ox, 0.0, 1.0), std::clamp(ty + oy, 0.0, 1.0)});
            }
        } else {
            targets.assign(ordered.size(), {tx, ty});
        }
        for (size_t i = 0; i < ordered.size(); ++i) {
            auto it = units_.find(ordered[i]);
            if (it == units_.end()) continue;
            combat_unit& u = it->second;
            if (u.owner_player_id != cmd.player_id) continue;
            if (u.status != "alive") continue;
            combat_order order;
            if (cmd.type == "patrol") {
                order.type = combat_order_type::patrol;
            } else if (cmd.type == "attack_move") {
                order.type = combat_order_type::attack_move;
            } else {
                order.type = combat_order_type::move;
            }
            order.target_x = targets[i].first;
            order.target_y = targets[i].second;
            order.queued = queued;
            // Patrol ping-pong anchor = where the unit stands when ordered (§9).
            if (order.type == combat_order_type::patrol) {
                u.patrol_home_x = u.x;
                u.patrol_home_y = u.y;
                u.patrol_home_set = true;
            }
            issue_order(u, order);
            mark_dirty(u);
        }
    } else if (cmd.type == "formation") {
        apply_formation_command(cmd);
    } else if (cmd.type == "attack") {
        const auto unit_ids = parse_ids();
        const int64_t target_unit_id = cmd.payload.value("target_unit_id", static_cast<int64_t>(0));
        const bool has_ground = cmd.payload.contains("ground_x") && cmd.payload.contains("ground_y");
        std::lock_guard<std::mutex> lock(units_mutex_);
        for (int64_t id : unit_ids) {
            auto it = units_.find(id);
            if (it == units_.end()) continue;
            combat_unit& u = it->second;
            if (u.owner_player_id != cmd.player_id) continue;
            if (u.status != "alive") continue;
            combat_order order;
            if (has_ground && u.stats.is_aoe) {
                order.type = combat_order_type::attack_ground;   // Lob (M5)
                order.target_x = cmd.payload.value("ground_x", 0.5);
                order.target_y = cmd.payload.value("ground_y", 0.5);
            } else if (target_unit_id != 0) {
                order.type = combat_order_type::attack_unit;
                order.target_unit_id = target_unit_id;
            } else {
                order.type = combat_order_type::attack_move;
                order.target_x = cmd.payload.value("target_x", 0.5);
                order.target_y = cmd.payload.value("target_y", 0.5);
            }
            order.queued = queued;
            issue_order(u, order);
            mark_dirty(u);
        }
    } else if (cmd.type == "stop") {
        const auto unit_ids = parse_ids();
        std::lock_guard<std::mutex> lock(units_mutex_);
        for (int64_t id : unit_ids) {
            auto it = units_.find(id);
            if (it == units_.end()) continue;
            combat_unit& u = it->second;
            if (u.owner_player_id != cmd.player_id || u.status != "alive") continue;
            combat_order order;
            order.type = combat_order_type::stop;
            issue_order(u, order);
            mark_dirty(u);
        }
    } else if (cmd.type == "hold") {
        const auto unit_ids = parse_ids();
        std::lock_guard<std::mutex> lock(units_mutex_);
        for (int64_t id : unit_ids) {
            auto it = units_.find(id);
            if (it == units_.end()) continue;
            combat_unit& u = it->second;
            if (u.owner_player_id != cmd.player_id || u.status != "alive") continue;
            combat_order order;
            order.type = combat_order_type::hold;
            issue_order(u, order);
            u.stance = "hold";
            mark_dirty(u);
        }
    } else if (cmd.type == "stance") {
        const auto unit_ids = parse_ids();
        const std::string stance = cmd.payload.value("stance", "defensive");
        if (stance != "aggressive" && stance != "defensive" && stance != "hold") return;
        std::lock_guard<std::mutex> lock(units_mutex_);
        for (int64_t id : unit_ids) {
            auto it = units_.find(id);
            if (it == units_.end()) continue;
            combat_unit& u = it->second;
            if (u.owner_player_id != cmd.player_id || u.status != "alive") continue;
            u.stance = stance;
            mark_dirty(u);
        }
    } else if (cmd.type == "deploy") {
        // rts_design.md §4.4: place squad members inside the team deploy zone.
        // Only valid during the deploy phase; default facing toward the enemy
        // camp is applied at spawn unless the placement specifies one. A
        // multi-unit command spreads deterministically and nudges apart so
        // units don't stack (plan M2: non-overlapping).
        std::lock_guard<std::mutex> lock(units_mutex_);
        if (phase_.load() != match_phase::deploy) return;
        const auto unit_ids = parse_ids();
        double tx = cmd.payload.value("x", -1.0);
        double ty = cmd.payload.value("y", -1.0);
        if (unit_ids.empty() || tx < 0.0 || ty < 0.0) return;
        tx = std::clamp(tx, 0.0, 1.0);
        ty = std::clamp(ty, 0.0, 1.0);
        double facing = cmd.payload.value("facing", -999.0);   // -999 = use camp default
        int team = -1;
        // Deploy spacing uses the largest body among the units being placed so
        // big units (e.g. trolls) don't stack at placement.
        double max_radius = k_default_unit_radius;
        {
            std::lock_guard<std::mutex> sl(state_mutex_);
            for (const auto& p : players_) {
                if (p.player_id == cmd.player_id) {
                    team = p.team;
                    for (const auto& m : p.retinue) {
                        if (std::find(unit_ids.begin(), unit_ids.end(), m.member_id) == unit_ids.end()) {
                            continue;
                        }
                        max_radius = std::max(
                            max_radius,
                            resolve_combatant_stats(config_.player_combatants, m.unit_class, m.level).radius);
                    }
                    break;
                }
            }
        }
        if (team < 0) return;
        if (!point_in_deploy_zone(team, tx, ty)) return;

        // Zone bounds (rect, or a radius box around the spawn fallback).
        double zx0 = 0.03, zy0 = 0.35, zx1 = 0.23, zy1 = 0.65;
        bool have_rect = false;
        if (config_.map_metadata.contains("deploy_zones") &&
            config_.map_metadata["deploy_zones"].is_array()) {
            for (const auto& z : config_.map_metadata["deploy_zones"]) {
                if (z.value("team", 1) == team) {
                    zx0 = z.value("x", 0.03);
                    zy0 = z.value("y", 0.35);
                    zx1 = zx0 + z.value("w", 0.2);
                    zy1 = zy0 + z.value("h", 0.3);
                    have_rect = true;
                    break;
                }
            }
        }
        if (!have_rect) {
            const double sx = deploy_x_for(team, 0);
            const double sy = deploy_y_for(team);
            zx0 = sx - 0.15; zx1 = sx + 0.15; zy0 = sy - 0.15; zy1 = sy + 0.15;
        }
        const double min_sep = 2.0 * max_radius * 1.1;
        const int n = static_cast<int>(unit_ids.size());
        const int cols = (n > 1) ? static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n)))) : 1;
        for (int i = 0; i < n; ++i) {
            const int64_t id = unit_ids[static_cast<size_t>(i)];
            double px = tx;
            double py = ty;
            if (n > 1) {
                const int row = i / cols;
                const int col = i % cols;
                px = tx + (col - (cols - 1) * 0.5) * min_sep;
                py = ty + (row - (n - 1) / cols) * min_sep * 0.75;
            }
            // Nudge away from any existing placement until separated.
            for (int attempt = 0; attempt < 10; ++attempt) {
                const std::pair<double, double>* clash = nullptr;
                double len = 0.0, dx = 0.0, dy = 0.0;
                for (const auto& [oid, dep] : deployments_) {
                    if (oid == id) continue;
                    const double ex = dep.first.first;
                    const double ey = dep.first.second;
                    const double d = std::hypot(px - ex, py - ey);
                    if (d < min_sep) {
                        clash = &dep.first;
                        dx = px - ex; dy = py - ey; len = d;
                        break;
                    }
                }
                if (!clash) break;
                if (len < 1e-6) { dx = 1.0; dy = 0.0; len = 1.0; }
                px = std::clamp(px + dx / len * min_sep, zx0 + 0.005, zx1 - 0.005);
                py = std::clamp(py + dy / len * min_sep, zy0 + 0.005, zy1 - 0.005);
            }
            px = std::clamp(px, zx0 + 0.005, zx1 - 0.005);
            py = std::clamp(py, zy0 + 0.005, zy1 - 0.005);
            const double f = (facing == -999.0) ? camp_angle_for(team) : facing;
            deployments_[id] = {{px, py}, f};
        }
    } else if (cmd.type == "construct") {
        try_start_construct(cmd);
    } else if (cmd.type == "ability") {
        std::lock_guard<std::mutex> lock(units_mutex_);
        outbox_.push_back({"ability", {{"player_id", cmd.player_id},
                                       {"ability_id", cmd.payload.value("ability_id", "")}}});
    } else if (cmd.type == "leave") {
        // Forfeit (§3.5): the player's units WITHDRAW UNWOUNDED (no casualty
        // records) and the leaver is flagged for a defeat stamp. Their
        // structures persist as team-owned; garrisons empty.
        withdraw_player(cmd.player_id);
        // The team continues without the leaver; end conditions resolve the
        // match normally (the leaver's units are gone).
    }
}

// ────────────────────────── Movement ──────────────────────────

double combat_match::distance(const combat_unit& a, const combat_unit& b) const {
    return distance_to(a.x, a.y, b.x, b.y);
}

double combat_match::distance_to(double ax, double ay, double bx, double by) const {
    const double dx = bx - ax;
    const double dy = by - ay;
    return std::sqrt(dx * dx + dy * dy);
}

double combat_match::terrain_scale_at(double x, double y) const {
    // §8.4: tile cost 0 blocks (handled elsewhere); cost > 1 is slower terrain.
    if (path_grid_.width <= 0 || path_grid_.height <= 0) return 1.0;
    const int tx = std::clamp(static_cast<int>(x * path_grid_.width), 0, path_grid_.width - 1);
    const int ty = std::clamp(static_cast<int>(y * path_grid_.height), 0, path_grid_.height - 1);
    const double c = path_grid_.costs[static_cast<size_t>(ty) * path_grid_.width + tx];
    return (c > 1.0 && std::isfinite(c)) ? c : 1.0;
}

bool combat_match::tile_blocked(int tx, int ty) const {
    if (path_grid_.width <= 0 || path_grid_.height <= 0) return false;
    if (tx < 0 || ty < 0 || tx >= path_grid_.width || ty >= path_grid_.height) return true;
    if (path_grid_.static_blocked[static_cast<size_t>(ty) * path_grid_.width + tx]) return true;
    // Live structures are true impassable path obstacles (§8.1).
    const double cx = (tx + 0.5) / path_grid_.width;
    const double cy = (ty + 0.5) / path_grid_.height;
    return structure_at_point(cx, cy) != nullptr;
}

void combat_match::move_toward(combat_unit& u, double tx, double ty, double dt,
                               bool avoid_blocked) {
    const double dx = tx - u.x;
    const double dy = ty - u.y;
    const double dist = std::sqrt(dx * dx + dy * dy);
    const double step =
        k_base_speed * u.stats.movement_speed * dt / terrain_scale_at(u.x, u.y);
    if (dist <= step || dist < 1e-9) {
        u.x = tx;
        u.y = ty;
    } else {
        const double nx = dx / dist;
        const double ny = dy / dist;
        const double nx_ = u.x + nx * step;
        const double ny_ = u.y + ny * step;
        if (avoid_blocked) {
            const int tx_ = static_cast<int>(nx_ * path_grid_.width);
            const int ty_ = static_cast<int>(ny_ * path_grid_.height);
            if (tile_blocked(tx_, ty_)) return;   // don't enter an impassable cell
        }
        u.x = nx_;
        u.y = ny_;
    }
    u.x = std::clamp(u.x, 0.0, 1.0);
    u.y = std::clamp(u.y, 0.0, 1.0);
    set_facing(u, std::atan2(ty - u.y, tx - u.x));
    mark_dirty(u);
}

double combat_match::rotation_to_target(const combat_unit& u, double tx, double ty) const {
    return std::atan2(ty - u.y, tx - u.x);
}

void combat_match::set_facing(combat_unit& u, double target_angle) {
    u.facing = std::remainder(target_angle, 2.0 * k_pi);
    if (u.facing < 0) u.facing += 2.0 * k_pi;
}

void combat_match::face_toward(combat_unit& u, double tx, double ty, double dt) {
    const double target = std::atan2(ty - u.y, tx - u.x);
    double diff = std::remainder(target - u.facing, 2.0 * k_pi);
    const double max_step = u.stats.turn_rate * dt;
    const double step = std::clamp(diff, -max_step, max_step);
    u.facing = std::remainder(u.facing + step, 2.0 * k_pi);
    if (u.facing < 0) u.facing += 2.0 * k_pi;
    mark_dirty(u);
}

void combat_match::start_path_for(combat_unit& u, const combat_order& order) {
    const std::vector<bool> blocked = structure_blocked_mask();
    u.waypoints = find_path(path_grid_, u.x, u.y, order.target_x, order.target_y, &blocked);
}

void combat_match::follow_waypoints(combat_unit& u, double dt) {
    while (!u.waypoints.empty()) {
        const auto [wx, wy] = u.waypoints.front();
        const double d = distance_to(u.x, u.y, wx, wy);
        const double step =
            k_base_speed * u.stats.movement_speed * dt / terrain_scale_at(u.x, u.y);
        if (d <= step) {
            u.x = wx;
            u.y = wy;
            set_facing(u, std::atan2(wy - u.y, wx - u.x));
            mark_dirty(u);
            u.waypoints.pop_front();
            continue;
        }
        move_toward(u, wx, wy, dt, true);
        return;
    }
}

void combat_match::execute_orders(combat_unit& u, double dt) {
    if (u.orders.empty()) return;
    combat_order& order = u.orders.front();

    switch (order.type) {
        case combat_order_type::move: {
            if (!u.waypoints.empty()) {
                follow_waypoints(u, dt);
                if (u.waypoints.empty()) complete_current_order(u);
                return;
            }
            const double d = distance_to(u.x, u.y, order.target_x, order.target_y);
            if (d <= 0.01) {
                complete_current_order(u);
                return;
            }
            start_path_for(u, order);
            if (u.waypoints.empty()) {
                move_toward(u, order.target_x, order.target_y, dt, true);
            } else {
                follow_waypoints(u, dt);
                if (u.waypoints.empty()) complete_current_order(u);
            }
            return;
        }
        case combat_order_type::attack_move: {
            // Move along the path; engage hostiles en route (§9).
            const combat_unit* target = find_alive_target(u, acquisition_radius_for(u));
            if (target) {
                u.target_id = target->unit_id;
                return;   // engaged — combat_resolve fires; target death resumes
            }
            if (!u.waypoints.empty()) {
                follow_waypoints(u, dt);
                if (u.waypoints.empty()) complete_current_order(u);
                return;
            }
            const double d = distance_to(u.x, u.y, order.target_x, order.target_y);
            if (d <= 0.01) {
                complete_current_order(u);
                return;
            }
            start_path_for(u, order);
            if (u.waypoints.empty()) {
                move_toward(u, order.target_x, order.target_y, dt, true);
            } else {
                follow_waypoints(u, dt);
                if (u.waypoints.empty()) complete_current_order(u);
            }
            return;
        }
        case combat_order_type::attack_unit: {
            combat_unit* target = find_unit(order.target_unit_id);
            if (!target || target->status != "alive") {
                complete_current_order(u);
                return;
            }
            u.target_id = target->unit_id;
            if (in_attack_range(u, *target)) return;    // combat_resolve fires
            const double d = distance_to(u.x, u.y, target->x, target->y);
            if (u.stats.attack_type == "melee" && d > pursuit_radius_for(u)) {
                complete_current_order(u);   // chase given up
                return;
            }
            move_toward(u, target->x, target->y, dt, true);
            return;
        }
        case combat_order_type::patrol: {
            // §9: loop/ping-pong waypoints, engaging along the path. While a
            // hostile is acquired the unit fights (acquire_targets keeps the
            // target); once clear it resumes the next leg.
            const combat_unit* target = find_alive_target(u, acquisition_radius_for(u));
            if (target) {
                u.target_id = target->unit_id;
                return;
            }
            u.target_id = 0;
            if (!u.waypoints.empty()) {
                follow_waypoints(u, dt);
                if (!u.waypoints.empty()) return;
            }
            if (distance_to(u.x, u.y, order.target_x, order.target_y) > 0.01) {
                start_path_for(u, order);
                if (!u.waypoints.empty()) {
                    follow_waypoints(u, dt);
                } else {
                    move_toward(u, order.target_x, order.target_y, dt, true);
                }
                return;
            }
            // Reached the endpoint: flip the ping-pong anchor and head back.
            if (!u.patrol_home_set) {
                u.patrol_home_x = u.x;
                u.patrol_home_y = u.y;
                u.patrol_home_set = true;
            }
            const double home_x = u.patrol_home_x;
            const double home_y = u.patrol_home_y;
            u.patrol_home_x = order.target_x;
            u.patrol_home_y = order.target_y;
            order.target_x = home_x;
            order.target_y = home_y;
            u.waypoints.clear();
            return;
        }
        case combat_order_type::stop:
            u.waypoints.clear();
            complete_current_order(u);
            return;
        case combat_order_type::hold:
            // Stand firm; never leave the square (engagement handled by
            // combat_resolve within attack range). Stays active.
            u.waypoints.clear();
            return;
        case combat_order_type::stance:
            complete_current_order(u);
            return;
        case combat_order_type::construct: {
            // Channeled build (§7.2): path to the site, then channel in place.
            combat_structure* st = find_structure(order.target_unit_id);
            if (!st || st->state == "destroyed") {
                u.channeling_structure_id = 0;
                complete_current_order(u);
                return;
            }
            u.channeling_structure_id = st->id;
            const double d = distance_to(u.x, u.y, st->x, st->y);
            if (d > u.stats.radius * 2.0 + 0.01) {
                move_toward(u, st->x, st->y, dt, true);
            } else {
                // Adjacent: stay put and channel (update_structures adds rate).
                const combat_order keep = order;   // copy before clearing the queue
                clear_orders(u);
                u.orders.push_back(keep);
            }
            mark_dirty(u);
            return;
        }
        case combat_order_type::attack_ground:
            // Lob (D51): path within max range of the ground point, then hold so
            // combat_resolve throws a flask at it every cooldown.
            {
                const double d = distance_to(u.x, u.y, order.target_x, order.target_y);
                if (d > buffed_range(u)) {
                    move_toward(u, order.target_x, order.target_y, dt, true);
                } else {
                    u.waypoints.clear();
                }
            }
            return;
        case combat_order_type::formation:
            // Formations are applied at order time as a persistent modifier
            // (§8.3) — the enum case is never queued.
            complete_current_order(u);
            return;
    }
}

void combat_match::move_units(double dt) {
    for (auto& [id, u] : units_) {
        if (u.status != "alive") continue;
        if (u.garrison_structure_id != 0) continue;   // garrisoned — stays in the tower
        if (u.stance == "hold" && !u.orders.empty() &&
            u.orders.front().type == combat_order_type::hold) {
            continue;   // never leaves the square
        }
        if (!u.orders.empty()) {
            execute_orders(u, dt);
            continue;   // explicit orders drive movement (Move never engages)
        }
        // Idle: melee pursuit of the acquired target within pursuit radius.
        const combat_unit* target = find_unit(u.target_id);
        bool engaged = false;
        if (target && target->status == "alive" && u.stats.attack_type == "melee") {
            const double d = distance_to(u.x, u.y, target->x, target->y);
            if (!in_attack_range(u, *target) && d <= pursuit_radius_for(u)) {
                move_toward(u, target->x, target->y, dt, true);
            }
            engaged = true;   // stand & fight when adjacent — never wander off
        } else if (target && target->status == "alive" &&
                   u.stats.attack_type == "ranged" &&
                   !in_attack_range(u, *target) && u.stance == "aggressive") {
            // Aggressive ranged closes to range.
            move_toward(u, target->x, target->y, dt, true);
            engaged = true;
        } else if (u.target_id != 0 && find_structure(u.target_id) != nullptr) {
            // Melee closing on a structure target (siege §11.2).
            const combat_structure* s = find_structure(u.target_id);
            if (u.stats.attack_type == "melee" &&
                distance_to(u.x, u.y, s->x, s->y) > buffed_range(u)) {
                move_toward(u, s->x, s->y, dt, true);
                engaged = true;
            }
        }
        if (!engaged) {
            if (!u.waypoints.empty()) {
                // AI movement goals (enemy groups), idle units.
                follow_waypoints(u, dt);
            } else if (u.is_enemy && u.use_flow_field) {
                // §8.1: sample the group's shared flow field for the next step.
                auto fit = group_flow_fields_.find(u.warband_group);
                if (fit != group_flow_fields_.end()) {
                    double fdx = 0.0, fdy = 0.0;
                    if (flow_direction_at(fit->second, u.x, u.y, fdx, fdy)) {
                        const double step = 0.06;
                        move_toward(u, u.x + fdx * step, u.y + fdy * step, dt, true);
                    }
                }
            }
        }
    }
}

void combat_match::collide_units() {
    // Everything hard (§5.6): units block each other. Friendly units block
    // friendlies too. 2-3 shove iterations keep armies flowing without overlap.
    // Structures are true obstacles: units shoved onto a structure cell are
    // pushed back out.
    constexpr int k_iterations = 3;
    std::vector<int64_t> ids;
    for (const auto& [id, u] : units_) {
        if (u.status == "alive" && u.garrison_structure_id == 0) ids.push_back(id);
    }
    for (int it = 0; it < k_iterations; ++it) {
        bool moved = false;
        for (size_t i = 0; i < ids.size(); ++i) {
            auto ai = units_.find(ids[i]);
            if (ai == units_.end() || ai->second.status != "alive") continue;
            // Push units out of structure cells.
            for (const auto& [sid, s] : structures_) {
                if (s.state == "destroyed") continue;
                combat_unit& a = ai->second;
                double dx = a.x - s.x;
                double dy = a.y - s.y;
                double d = std::sqrt(dx * dx + dy * dy);
                if (d < 0.045 && d > 1e-9) {
                    a.x = std::clamp(s.x + dx / d * 0.045, 0.0, 1.0);
                    a.y = std::clamp(s.y + dy / d * 0.045, 0.0, 1.0);
                    mark_dirty(a);
                    moved = true;
                }
            }
            for (size_t j = i + 1; j < ids.size(); ++j) {
                auto bi = units_.find(ids[j]);
                if (bi == units_.end() || bi->second.status != "alive") continue;
                combat_unit& a = ai->second;
                combat_unit& b = bi->second;
                double dx = b.x - a.x;
                double dy = b.y - a.y;
                double d = std::sqrt(dx * dx + dy * dy);
                const double min_d = a.stats.radius + b.stats.radius;
                if (d >= min_d) continue;
                double push = min_d - d;
                double nx = 0.0, ny = 0.0;
                if (d > 1e-9) {
                    nx = dx / d;
                    ny = dy / d;
                } else {
                    // Exact overlap — pick an axis deterministically.
                    nx = 1.0;
                    ny = 0.0;
                    push = min_d;
                }
                a.x = std::clamp(a.x - nx * push * 0.5, 0.0, 1.0);
                a.y = std::clamp(a.y - ny * push * 0.5, 0.0, 1.0);
                b.x = std::clamp(b.x + nx * push * 0.5, 0.0, 1.0);
                b.y = std::clamp(b.y + ny * push * 0.5, 0.0, 1.0);
                mark_dirty(a);
                mark_dirty(b);
                moved = true;
            }
        }
        if (!moved) break;
    }
}

// ────────────────────────── Acquisition & combat ──────────────────────────

double combat_match::acquisition_radius_for(const combat_unit& u) const {
    if (u.stance == "aggressive") return k_aggressive_acquire;
    if (u.stance == "hold") return u.stats.attack_range;
    return k_defensive_acquire;
}

double combat_match::pursuit_radius_for(const combat_unit& u) const {
    if (u.stance == "aggressive") return k_aggressive_pursuit;
    if (u.stance == "hold") return u.stats.attack_range;
    return k_defensive_pursuit;
}

bool combat_match::order_allows_engagement(const combat_unit& u) const {
    if (u.orders.empty()) return true;
    switch (u.orders.front().type) {
        case combat_order_type::move:
        case combat_order_type::attack_ground:
        case combat_order_type::formation:
        case combat_order_type::construct:
            return false;
        // attack_move / patrol engage along the path (§9).
        default:
            return true;   // stop / hold / attack / attack_move / patrol engage
    }
}

bool combat_match::point_in_deploy_zone(int team, double x, double y) const {
    // Deploy rects (rts_design.md §17.2): {team, x, y, w, h} normalized. Absent
    // zones fall back to a radius around the team's spawn point.
    if (config_.map_metadata.contains("deploy_zones") && config_.map_metadata["deploy_zones"].is_array()) {
        for (const auto& z : config_.map_metadata["deploy_zones"]) {
            if (z.value("team", 1) != team) continue;
            const double zx = z.value("x", 0.0);
            const double zy = z.value("y", 0.0);
            const double zw = z.value("w", 0.2);
            const double zh = z.value("h", 0.2);
            return x >= zx && x <= zx + zw && y >= zy && y <= zy + zh;
        }
        return false;
    }
    const double sx = deploy_x_for(team, 0);
    const double sy = deploy_y_for(team);
    const double dx = x - sx;
    const double dy = y - sy;
    return std::sqrt(dx * dx + dy * dy) <= 0.15;
}

const combat_unit* combat_match::find_alive_target(const combat_unit& u, double radius) const {
    const combat_unit* best = nullptr;
    double best_d = radius;
    for (const auto& [id, t] : units_) {
        if (t.status != "alive" || t.team == u.team) continue;
        // Melee can never target a garrisoned archer — only ranged/magic can
        // hit through the arrow slit (§7.4).
        if (u.stats.attack_type == "melee" && t.garrison_structure_id != 0) continue;
        const double d = distance_to(u.x, u.y, t.x, t.y);
        if (d <= best_d) {
            best = &t;
            best_d = d;
        }
    }
    return best;
}

combat_unit* combat_match::find_unit(int64_t id) {
    auto it = units_.find(id);
    return it == units_.end() ? nullptr : &it->second;
}

void combat_match::acquire_targets() {
    for (auto& [id, u] : units_) {
        if (u.status != "alive") continue;
        // Keep AI siege targets on structures (melee demolishing a wall).
        if (u.target_id != 0 && find_structure(u.target_id) != nullptr) continue;
        if (!order_allows_engagement(u)) {
            u.target_id = 0;   // Move/patrol never auto-engage (D41)
            continue;
        }
        const double radius = acquisition_radius_for(u);
        // Keep an existing target while it stays within the keep radius.
        const combat_unit* cur = find_unit(u.target_id);
        if (cur && cur->status == "alive") {
            const double keep = (u.stats.attack_type == "melee")
                                    ? pursuit_radius_for(u)
                                    : u.stats.attack_range * 1.5;
            if (distance_to(u.x, u.y, cur->x, cur->y) <= keep) continue;
        }
        const combat_unit* best = find_alive_target(u, radius);
        u.target_id = best ? best->unit_id : 0;
    }
}

bool combat_match::have_line_of_sight(const combat_unit& a, const combat_unit& b) const {
    // rts_design.md §5.7: unit bodies NEVER block ranged — only structures do.
    // A blocks_ranged structure (or per-attack magic wall) between the two
    // stops the shot; unit-vs-unit LOS is always clear.
    return !structure_blocks_line(a.x, a.y, b.x, b.y, a.stats.attack_type);
}

std::string combat_match::arc_for(const combat_unit& attacker, const combat_unit& target) const {
    // §5.4: arcs are determined by the DEFENDER's facing. The angle from the
    // target toward the attacker decides front/side/back.
    const double theta = std::atan2(attacker.y - target.y, attacker.x - target.x);
    const double rel = std::remainder(target.facing - theta, 2.0 * k_pi);
    const double abs_rel = std::fabs(rel);
    if (abs_rel <= k_pi / 2.0) return "front";
    if (abs_rel <= 3.0 * k_pi / 4.0) return "side";
    return "back";
}

double combat_match::buffed_range(const combat_unit& u) const {
    double r = u.stats.attack_range;
    if (u.garrison_structure_id != 0 &&
        config_.structures.contains("structures") && config_.structures["structures"].is_object()) {
        const auto& defs = config_.structures["structures"];
        for (const auto& [id, s] : structures_) {
            if (s.id != u.garrison_structure_id || s.state == "destroyed") continue;
            if (defs.contains(s.type) && defs[s.type].is_object() &&
                defs[s.type].contains("garrison") && defs[s.type]["garrison"].is_object()) {
                r *= 1.0 + defs[s.type]["garrison"].value("range_bonus_pct", 0.0) / 100.0;
            }
            break;
        }
    }
    return r;
}

double combat_match::buffed_damage(const combat_unit& u) const {
    const double base = damage_for(u);
    if (u.garrison_structure_id != 0 &&
        config_.structures.contains("structures") && config_.structures["structures"].is_object()) {
        const auto& defs = config_.structures["structures"];
        for (const auto& [id, s] : structures_) {
            if (s.id != u.garrison_structure_id || s.state == "destroyed") continue;
            if (defs.contains(s.type) && defs[s.type].is_object() &&
                defs[s.type].contains("garrison") && defs[s.type]["garrison"].is_object()) {
                return base * (1.0 + defs[s.type]["garrison"].value("damage_bonus_pct", 0.0) / 100.0);
            }
            break;
        }
    }
    return base;
}

double combat_match::damage_for(const combat_unit& u) const {
    if (u.stats.attack_type == "ranged") return u.stats.ranged_damage;
    if (u.stats.attack_type == "magical") return u.stats.magical_damage;
    return u.stats.melee_damage;
}

bool combat_match::in_attack_range(const combat_unit& u, const combat_unit& target) const {
    return distance_to(u.x, u.y, target.x, target.y) <= buffed_range(u);
}

void combat_match::damage_structure(combat_structure& s, double dmg) {
    if (s.state == "destroyed") return;
    s.hp = std::max(0.0, s.hp - dmg);
    if (dmg > 0.0) s.damaged = true;
    if (s.hp <= 0.0) {
        destroy_structure(s.id);
    }
}

bool combat_match::structure_is_protected(int64_t /*structure_id*/, const std::string& type) const {
    // rts_design.md §6.3.1 / D57: over-throw clears fire-through cover
    // (barricades/spikes) but keeps walls and towers protected.
    if (!config_.structures.contains("structures") || !config_.structures["structures"].is_object()) {
        return false;
    }
    const auto& defs = config_.structures["structures"];
    if (!defs.contains(type) || !defs[type].is_object()) return false;
    const auto& def = defs[type];
    return def.value("blocks_physical_ranged", false) ||
           def.value("blocks_magical_ranged", false) ||
           def.value("arrow_slit", false);
}

bool combat_match::friendly_blocking_structure_in_blast(int team, double cx, double cy,
                                                        double radius) const {
    for (const auto& [id, s] : structures_) {
        if (s.state == "destroyed") continue;
        if (s.team != team) continue;
        if (!structure_is_protected(id, s.type)) continue;
        if (distance_to(cx, cy, s.x, s.y) <= radius) return true;
    }
    return false;
}

bool combat_match::find_safe_center(const combat_unit& u, double base_x, double base_y,
                                    double radius, double& cx, double& cy) const {
    // §6.3.1 over-throw: seek a landing center that keeps the target in the
    // blast while CLEARING every friendly (units AND protected friendly
    // structures). The attacker's own body counts as a friendly (no self-
    // splash). Shift away from the nearest friendly up to cap = splash radius,
    // bounded by max attack range.
    const double max_range = buffed_range(u);
    cx = base_x;
    cy = base_y;
    const double d0 = distance_to(u.x, u.y, cx, cy);
    if (d0 > max_range) {
        cx = u.x + (cx - u.x) / d0 * max_range;
        cy = u.y + (cy - u.y) / d0 * max_range;
    }

    for (int it = 0; it < 2; ++it) {
        const combat_unit* near_friendly = nullptr;
        double fd = radius;
        for (const auto& [id, f] : units_) {
            if (f.status != "alive") continue;
            if (f.team != u.team) continue;
            const double d = distance_to(cx, cy, f.x, f.y);
            if (d < fd) {
                near_friendly = &f;
                fd = d;
            }
        }
        if (!near_friendly) break;
        double dx = cx - near_friendly->x;
        double dy = cy - near_friendly->y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) {
            dx = 1.0;
            dy = 0.0;
            len = 1.0;
        }
        const double shift = std::min(radius - fd, radius);
        cx = std::clamp(cx + dx / len * shift, 0.0, 1.0);
        cy = std::clamp(cy + dy / len * shift, 0.0, 1.0);
        // Bound by max range from the attacker.
        const double nd = distance_to(u.x, u.y, cx, cy);
        if (nd > max_range) {
            cx = u.x + (cx - u.x) / nd * max_range;
            cy = u.y + (cy - u.y) / nd * max_range;
        }
    }

    // Final clearance check: no friendly unit and no protected friendly
    // structure may remain inside the blast.
    for (const auto& [id, f] : units_) {
        if (f.status != "alive" || f.team != u.team) continue;
        if (distance_to(cx, cy, f.x, f.y) <= radius) return false;   // hold fire (§6.3.2)
    }
    if (friendly_blocking_structure_in_blast(u.team, cx, cy, radius)) return false;
    return true;
}

void combat_match::try_attack_aoe(combat_unit& u, double cx, double cy,
                                  std::vector<int64_t>& deaths) {
    // rts_design.md §6.1: AoE ignores arcs/dodge, deals unchecked splash to every
    // unit AND structure in radius (incl. building-state), flat mitigation vs the
    // target's magical defense. True walls shelter (block the line from the
    // epicenter); fire-through cover does not.
    const double radius = u.stats.splash_radius;
    const double base_damage = u.stats.magical_damage;
    nlohmann::json hits = nlohmann::json::array();

    for (auto& [tid, t] : units_) {
        if (t.status != "alive") continue;
        if (distance_to(cx, cy, t.x, t.y) > radius) continue;
        if (structure_blocks_line(cx, cy, t.x, t.y, "magical")) continue;   // sheltered
        const double dmg = base_damage * 100.0 / (100.0 + t.stats.magical_defense);
        const double before = t.hp;
        t.hp = std::max(0.0, t.hp - dmg);
        mark_dirty(t);
        hits.push_back({{"target", t.unit_id}, {"kind", "unit"}, {"damage", dmg},
                        {"arc", "aoe"}, {"result", "landed"}});
        if (t.hp <= 0.0 && t.status == "alive") deaths.push_back(t.unit_id);
    }

    std::vector<int64_t> destroyed;
    for (auto& [sid, s] : structures_) {
        if (s.state == "destroyed") continue;
        if (distance_to(cx, cy, s.x, s.y) > radius) continue;
        const double dmg = base_damage;   // structures: uniform armor, no mitigation
        hits.push_back({{"target", s.id}, {"kind", "structure"}, {"type", s.type},
                        {"damage", dmg}, {"arc", "aoe"}, {"result", "landed"}});
        damage_structure(s, dmg);
    }

    outbox_.push_back({"attack", {
        {"attacker", u.unit_id},
        {"result", "landed"},
        {"aoe", true},
        {"center_x", cx},
        {"center_y", cy},
        {"radius", radius},
        {"attack_type", "magical"},
        {"hits", hits}
    }});
}

void combat_match::try_attack(combat_unit& u, combat_unit& target) {
    const std::string arc = arc_for(u, target);

    double base_defense = 0.0;
    if (u.stats.attack_type == "melee") base_defense = target.stats.melee_defense;
    else if (u.stats.attack_type == "ranged") base_defense = target.stats.ranged_defense;
    else base_defense = target.stats.magical_defense;

    double dir_mult = target.stats.direction_defense_back;
    double dodge = target.stats.dodge_back;
    if (arc == "front") {
        dir_mult = target.stats.direction_defense_front;
        dodge = target.stats.dodge_front;
    } else if (arc == "side") {
        dir_mult = target.stats.direction_defense_side;
        dodge = target.stats.dodge_side;
    }

    const double eff_def = base_defense * dir_mult;
    double final_damage = buffed_damage(u) * 100.0 / (100.0 + eff_def);

    std::string result = "landed";
    if (dodge > 0.0) {
        std::bernoulli_distribution dodge_dist(dodge);
        if (dodge_dist(rng_)) {
            final_damage = 0.0;
            result = "dodged";
        }
    }

    const double before = target.hp;
    if (result == "landed") {
        target.hp = std::max(0.0, target.hp - final_damage);
        mark_dirty(target);
    }

    outbox_.push_back({"attack", {
        {"attacker", u.unit_id},
        {"target", target.unit_id},
        {"result", result},
        {"blocked_by", 0},
        {"damage", final_damage},           // damage actually dealt this hit
        {"arc", arc},
        {"attack_type", u.stats.attack_type},
        {"hp", target.hp},
        {"max_hp", target.max_hp}
    }});
}

void combat_match::combat_resolve(double dt) {
    std::vector<int64_t> deaths;
    for (auto& [id, u] : units_) {
        if (u.status != "alive") continue;
        if (u.cooldown_remaining > 0.0) u.cooldown_remaining -= dt;

        // ── AoE / indirect fire (§6) ──
        if (u.stats.is_aoe) {
            const double radius = u.stats.splash_radius;
            double cx = 0.0, cy = 0.0;
            bool fire = false;
            if (!u.orders.empty() && u.orders.front().type == combat_order_type::attack_ground) {
                // Explicit Lob: lands exactly where the player pointed (D51).
                cx = u.orders.front().target_x;
                cy = u.orders.front().target_y;
                fire = true;
            } else {
                const combat_unit* at = find_unit(u.target_id);
                if (at && at->status == "alive" &&
                    find_safe_center(u, at->x, at->y, radius, cx, cy)) {
                    fire = true;
                } else if (at) {
                    // Hold fire (§6.3.2): no safe arc ("no safe shot — pull the
                    // melee or take a risky shot"). Throttled via the cooldown.
                    if (u.cooldown_remaining <= 0.0) {
                        outbox_.push_back({"hold_fire", {{"unit_id", u.unit_id},
                                                         {"reason", "no_safe_arc"}}});
                        u.cooldown_remaining = 2.0;   // throttle the status event
                    }
                    continue;
                } else {
                    u.target_id = 0;
                }
            }
            if (fire && u.cooldown_remaining <= 0.0) {
                try_attack_aoe(u, cx, cy, deaths);
                u.cooldown_remaining = u.stats.attack_interval;
            }
            continue;
        }

        // Visit the current target: a unit or a deployable structure (§7).
        combat_unit* target = find_unit(u.target_id);
        if (!target || target->status != "alive") {
            combat_structure* st = find_structure(u.target_id);
            if (!st || st->state == "destroyed") {
                u.target_id = 0;
                continue;
            }
            // Melee can demolish structures ("uniform armor — no arcs" §7.1);
            // ranged/magic are blocked by walls so they cannot shoot them.
            if (u.stats.attack_type != "melee") continue;
            if (u.cooldown_remaining > 0.0) continue;
            if (distance_to(u.x, u.y, st->x, st->y) > buffed_range(u)) continue;
            const double dmg = buffed_damage(u);   // flat — no defense, no dodge
            outbox_.push_back({"structure_attack", {{"attacker", u.unit_id},
                                                    {"structure", st->id},
                                                    {"type", st->type},
                                                    {"damage", dmg}}});
            damage_structure(*st, dmg);
            u.cooldown_remaining = u.stats.attack_interval;
            continue;
        }
        // Melee cannot hit a garrisoned archer through the tower (§7.4).
        if (u.stats.attack_type == "melee" && target->garrison_structure_id != 0) {
            u.target_id = 0;
            continue;
        }
        // Face the target; a garrisoned shooter fires from its tower.
        face_toward(u, target->x, target->y, dt);
        if (!in_attack_range(u, *target)) continue;
        if (!have_line_of_sight(u, *target)) continue;
        if (u.cooldown_remaining > 0.0) continue;

        try_attack(u, *target);
        u.cooldown_remaining = u.stats.attack_interval;
        if (target->hp <= 0.0 && target->status == "alive") {
            deaths.push_back(target->unit_id);
        }
    }
    for (int64_t did : deaths) {
        kill_unit(did);
    }
}

void combat_match::kill_unit(int64_t unit_id) {
    auto it = units_.find(unit_id);
    if (it == units_.end()) return;
    combat_unit& u = it->second;
    // Free the unit's channel (a builder death aborts + resets the build, §7.2)
    // and clear a garrison slot.
    if (u.channeling_structure_id != 0) {
        abort_build(u.channeling_structure_id);
    }
    if (u.garrison_structure_id != 0) {
        // Garrison death leaves the tower empty, buff off, re-garrisonable.
        auto sit = structures_.find(u.garrison_structure_id);
        if (sit != structures_.end()) sit->second.garrisoned_unit_id = 0;
    }
    outbox_.push_back({"unit_died", {{"unit_id", u.unit_id},
                                     {"owner", u.owner_player_id},
                                     {"team", u.team},
                                     {"is_enemy", u.is_enemy}}});
    // Progression counters: tally enemy deaths by class (the unit is removed
    // from units_ below, so the count is kept separately).
    if (u.is_enemy) enemy_deaths_[u.unit_class]++;

    if (death_handling_ == "respawn_after_seconds") {
        respawn_timers_[unit_id] = static_cast<double>(respawn_delay_seconds_);
        u.status = "dead";
        u.target_id = 0;
        mark_dirty(u);
        return;
    }

    // permanent + wounded (no-death) both record the player-side fall. The end
    // HP% becomes the wound severity under `wounded` rulesets. Enemy deaths are
    // purely in-match — never persisted.
    if (!u.is_enemy) {
        const double hp_pct = u.max_hp > 0.0 ? u.hp / u.max_hp * 100.0 : 0.0;
        casualties_.push_back({u.unit_id, u.owner_player_id, hp_pct});
    }
    removed_this_tick_.push_back(u.unit_id);
    units_.erase(it);
}

void combat_match::abort_build(int64_t structure_id) {
    // §7.2: an interrupt (builder death, or a new explicit order cancelling the
    // channel) aborts the build and resets its progress. The structure stays in
    // `building` at 0 HP so it can be re-channelled; only real damage destroys it.
    auto it = structures_.find(structure_id);
    if (it == structures_.end() || it->second.state == "destroyed") return;
    combat_structure& s = it->second;
    for (auto& [uid, u] : units_) {
        if (u.channeling_structure_id != s.id) continue;
        u.channeling_structure_id = 0;
        if (!u.orders.empty() &&
            u.orders.front().type == combat_order_type::construct &&
            u.orders.front().target_unit_id == s.id) {
            complete_current_order(u);
        }
    }
    if (s.state == "building") {
        s.hp = 0.0;
        outbox_.push_back({"build_aborted", {{"id", s.id},
                                             {"type", s.type},
                                             {"owner", s.owner_player_id}}});
    }
}

void combat_match::withdraw_player(int64_t character_id) {
    // §3.5: a leaver's units withdraw UNWOUNDED — removed with no casualty
    // record. Garrisons empty, channels detach, and structures persist as
    // team-owned. The player is flagged forfeited so persistence records a
    // defeat regardless of how the team's match resolves.
    std::lock_guard<std::mutex> lock(units_mutex_);
    {
        std::lock_guard<std::mutex> sl(state_mutex_);
        for (auto& p : players_) {
            if (p.player_id == character_id) {
                p.connected = false;
                p.forfeited = true;
            }
        }
    }
    std::vector<int64_t> owned;
    for (const auto& [id, u] : units_) {
        if (u.owner_player_id == character_id) owned.push_back(id);
    }
    for (int64_t id : owned) {
        auto it = units_.find(id);
        if (it == units_.end()) continue;
        combat_unit& u = it->second;
        if (u.channeling_structure_id != 0) u.channeling_structure_id = 0;
        if (u.garrison_structure_id != 0) {
            auto sit = structures_.find(u.garrison_structure_id);
            if (sit != structures_.end()) sit->second.garrisoned_unit_id = 0;
        }
        outbox_.push_back({"unit_retreated", {{"unit_id", u.unit_id},
                                              {"owner", u.owner_player_id}}});
        removed_this_tick_.push_back(u.unit_id);
        units_.erase(it);
    }
}

std::set<int64_t> combat_match::forfeited_players() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::set<int64_t> out;
    for (const auto& p : players_) {
        if (p.forfeited) out.insert(p.player_id);
    }
    return out;
}

// ────────────────────────── State broadcast ──────────────────────────

void combat_match::broadcast_state(const std::function<void(const combat_message&)>& broadcast) {
    combat_message msg;
    const int64_t tick = tick_.load();
    msg.match_id = match_id_;
    msg.tick = tick;
    msg.phase_name = phase_name_string();
    msg.countdown = phase_.load() == match_phase::countdown ? countdown_remaining_.load() : -1.0;
    msg.deploy_time = phase_.load() == match_phase::deploy ? deploy_time_remaining_.load() : -1.0;
    msg.battle_time = battle_time_.load();

    {
        std::lock_guard<std::mutex> lock(units_mutex_);
        const bool full = (tick % k_full_snapshot_interval_ticks == 0);
        msg.is_full_state = full;
        msg.type = full ? "match_state" : "match_update";
        if (full) {
            for (const auto& [id, u] : units_) msg.units.push_back(u);
            for (const auto& [id, s] : structures_) msg.structures.push_back(s);
        } else {
            for (int64_t id : dirty_units_) {
                auto it = units_.find(id);
                if (it != units_.end()) msg.units.push_back(it->second);
            }
            // Structures are few; emit them wholesale on partials (the client
            // overwrites whole rows, idempotent).
            for (const auto& [id, s] : structures_) msg.structures.push_back(s);
        }
        dirty_units_.clear();
        msg.removed_units.swap(removed_this_tick_);
        msg.events.swap(outbox_);
    }
    broadcast(msg);
}

// ────────────────────────── End conditions ──────────────────────────

void combat_match::check_end_conditions(const std::function<void(const combat_message&)>& broadcast) {
    if (ended_) return;
    if (phase_.load() != match_phase::battle) return;

    std::lock_guard<std::mutex> lock(units_mutex_);

    const bool players_alive = alive_count_for_team(1) > 0;
    const bool warband_alive = alive_count_for_team(2) > 0;

    if (!players_alive) {
        winner_team_.store(0);   // players lose (§13.3)
        finish_match("warband_victory");
        broadcast_ended(broadcast);
        return;
    }
    if (!warband_alive) {
        winner_team_.store(1);   // players win
        finish_match("warband_wiped");
        broadcast_ended(broadcast);
        return;
    }
    // Safety cap (§3.4): draw unless the warband was already wiped (checked
    // above, so a simultaneous wipe wins).
    if (battle_time_.load() >= static_cast<double>(duration_seconds_)) {
        winner_team_.store(-1);   // draw
        finish_match("safety_cap_elapsed");
        broadcast_ended(broadcast);
    }
}

void combat_match::finish_match(const std::string& reason) {
    if (ended_) return;
    ended_ = true;
    end_reason_ = reason;
    phase_.store(match_phase::ended);
    ended_at_.store(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

void combat_match::broadcast_ended(const std::function<void(const combat_message&)>& broadcast) {
    combat_message msg;
    msg.type = "match_ended";
    msg.match_id = match_id_;
    msg.tick = tick_.load();
    msg.phase_name = "ended";
    nlohmann::json payload;
    payload["winner"] = winner_team_.load();   // 1 win / 0 loss / -1 draw (§13.3)
    payload["winner_team"] = winner_team_.load();   // legacy alias — client migration aid
    payload["reason"] = end_reason_;
    nlohmann::json casualties = nlohmann::json::array();
    for (const auto& c : casualties_) {
        casualties.push_back({{"member_id", c.member_id}, {"owner", c.owner_player_id},
                              {"end_hp", c.end_hp}});
    }
    payload["casualties"] = casualties;

    // rts_design.md §14: XP is fixed per-fielded-member, warband-derived,
    // completion-only. Victory multiplies the base; draw = base only (D66).
    const double victory_mult =
        config_.rewards.value("victory_multiplier", 1.5);
    const double gain = (winner_team_.load() == 1) ? xp_base() * victory_mult : xp_base();
    const std::vector<double> xp_curve =
        config_.rewards.value("xp_curve", std::vector<double>{100.0, 250.0, 450.0, 700.0});
    nlohmann::json xp_by_member = nlohmann::json::array();
    nlohmann::json level_ups = nlohmann::json::array();
    if (gain > 0.0) {
        for (const auto& p : players_snapshot()) {
            for (const auto& m : p.retinue) {
                xp_by_member.push_back({{"member_id", m.member_id}, {"xp", gain}});
                // Preview level-up count (authoritative levels persist later):
                // thresholds crossed by `gain` starting at the bottom of the
                // member's pre-match level.
                double bottom = 0.0;
                const size_t idx = static_cast<size_t>(std::max(0, m.level - 2));
                if (m.level >= 2 && idx < xp_curve.size()) bottom = xp_curve[idx];
                int ups = 0;
                for (double t : xp_curve) {
                    if (t > bottom + gain) break;
                    if (t > bottom) ups++;
                }
                if (ups > 0) {
                    level_ups.push_back({{"member_id", m.member_id}, {"level_ups", ups}});
                }
            }
        }
    }
    payload["xp_by_member"] = xp_by_member;
    payload["level_ups"] = level_ups;
    // Rewards pipeline (D22): morale runs now; gold/equipment tables are
    // config slots defaulted to zero/empty.
    payload["rewards_stub"] = {
        {"morale", {{"victory", winner_team_.load() == 1}, {"defeat", winner_team_.load() == 0},
                    {"draw", winner_team_.load() == -1}}},
        {"gold", {}},
        {"equipment", {}},
        {"cosmetics", nlohmann::json::array()}
    };
    // Determinism (D27): the seed + a stable state hash let a replay compare
    // that the same seed and command log produced an identical battle.
    payload["seed"] = seed_;
    payload["state_hash"] = state_hash_unlocked();   // units_mutex_ already held
    msg.json_payload = payload;
    broadcast(msg);
}

combat_message combat_match::build_full_state() const {
    combat_message msg;
    msg.type = "match_state";
    msg.match_id = match_id_;
    msg.tick = tick_.load();
    msg.is_full_state = true;
    msg.phase_name = phase_name_string();
    msg.countdown = phase_.load() == match_phase::countdown ? countdown_remaining_.load() : -1.0;
    msg.deploy_time = phase_.load() == match_phase::deploy ? deploy_time_remaining_.load() : -1.0;
    msg.battle_time = battle_time_.load();
    {
        std::lock_guard<std::mutex> lock(units_mutex_);
        for (const auto& [id, u] : units_) msg.units.push_back(u);
        for (const auto& [id, s] : structures_) msg.structures.push_back(s);
    }
    return msg;
}

std::vector<casualty_record> combat_match::casualties_snapshot() const {
    std::lock_guard<std::mutex> lock(units_mutex_);
    return casualties_;
}

std::map<std::string, int> combat_match::enemy_deaths() const {
    std::lock_guard<std::mutex> lock(units_mutex_);
    return enemy_deaths_;
}

std::map<std::string, int> combat_match::enemy_fled() const {
    std::lock_guard<std::mutex> lock(units_mutex_);
    return enemy_fled_;
}

uint64_t combat_match::state_hash() const {
    std::lock_guard<std::mutex> lock(units_mutex_);
    return state_hash_unlocked();
}

uint64_t combat_match::state_hash_unlocked() const {
    // FNV-1a over the sorted (id, hp, x, y, kind) of every unit + structure.
    // Quantized to integers so tiny float formatting differences don't matter;
    // same seed + same commands yields an identical hash (D27).
    // Assumes units_mutex_ is already held (check_end_conditions calls it while
    // holding the lock; the public state_hash() wraps it).
    struct hash_row {
        int64_t id;
        int64_t hp;
        int64_t x;
        int64_t y;
        int64_t kind;
    };
    std::vector<hash_row> rows;
    for (const auto& [id, u] : units_) {
        rows.push_back({id, static_cast<int64_t>(std::llround(u.hp * 100.0)),
                        static_cast<int64_t>(std::llround(u.x * 10000.0)),
                        static_cast<int64_t>(std::llround(u.y * 10000.0)), 0});
    }
    for (const auto& [id, s] : structures_) {
        rows.push_back({id, static_cast<int64_t>(std::llround(s.hp * 100.0)), 0, 0, 1});
    }
    std::sort(rows.begin(), rows.end(), [](const hash_row& a, const hash_row& b) {
        if (a.id != b.id) return a.id < b.id;
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.hp != b.hp) return a.hp < b.hp;
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
    uint64_t hash = 1469598103934665603ull;
    for (const auto& r : rows) {
        const int64_t values[5] = {r.id, r.hp, r.x, r.y, r.kind};
        for (int64_t v : values) {
            const uint64_t x = static_cast<uint64_t>(v);
            for (int b = 0; b < 8; ++b) {
                hash ^= (x >> (b * 8)) & 0xFFu;
                hash *= 1099511628211ull;
            }
        }
    }
    return hash;
}

std::string combat_match::phase_name_string() const {
    switch (phase_.load()) {
        case match_phase::lobby: return "lobby";
        case match_phase::deploy: return "deploy";
        case match_phase::countdown: return "countdown";
        case match_phase::battle: return "battle";
        case match_phase::ended: return "ended";
    }
    return "lobby";
}

int combat_match::alive_count_for_team(int team) const {
    int count = 0;
    for (const auto& [id, u] : units_) {
        if (u.status == "alive" && u.team == team) count++;
    }
    return count;
}

std::set<int> combat_match::active_teams() const {
    std::set<int> teams;
    for (const auto& [id, u] : units_) {
        teams.insert(u.team);
    }
    return teams;
}

int combat_match::match_duration_seconds() const {
    return duration_seconds_;
}

size_t combat_match::warband_total() const {
    size_t total = 0;
    for (const auto& e : config_.warband) total += static_cast<size_t>(e.count);
    return total;
}

double combat_match::deploy_x_for(int team, int index) const {
    double base_x = 0.5;
    if (config_.map_metadata.contains("deploy_zones") && config_.map_metadata["deploy_zones"].is_array()) {
        for (const auto& z : config_.map_metadata["deploy_zones"]) {
            if (z.value("team", 1) == team) {
                base_x = z.value("x", 0.25) + z.value("w", 0.2) * 0.5;
                break;
            }
        }
    } else if (config_.map_metadata.contains("spawn_points") && config_.map_metadata["spawn_points"].is_array()) {
        for (const auto& sp : config_.map_metadata["spawn_points"]) {
            if (sp.value("team", 1) == team) {
                base_x = sp.value("x", 0.5);
                break;
            }
        }
    } else {
        base_x = team == 1 ? 0.25 : 0.75;
    }
    return std::clamp(base_x + (index % 5) * k_spawn_scatter, 0.0, 1.0);
}

double combat_match::deploy_y_for(int team) const {
    double base_y = 0.5;
    if (config_.map_metadata.contains("deploy_zones") && config_.map_metadata["deploy_zones"].is_array()) {
        for (const auto& z : config_.map_metadata["deploy_zones"]) {
            if (z.value("team", 1) == team) {
                base_y = z.value("y", 0.35) + z.value("h", 0.3) * 0.5;
                break;
            }
        }
    } else if (config_.map_metadata.contains("spawn_points") && config_.map_metadata["spawn_points"].is_array()) {
        for (const auto& sp : config_.map_metadata["spawn_points"]) {
            if (sp.value("team", 1) == team) {
                base_y = sp.value("y", 0.5);
                break;
            }
        }
    } else {
        base_y = 0.5;
    }
    return std::clamp(base_y, 0.0, 1.0);
}

double combat_match::camp_angle_for(int team) const {
    const int enemy_team = (team == 1) ? 2 : 1;
    const double ex = deploy_x_for(enemy_team, 0);
    const double ey = deploy_y_for(enemy_team);
    const double mx = deploy_x_for(team, 0);
    const double my = deploy_y_for(team);
    return std::atan2(ey - my, ex - mx);
}

} // namespace combat