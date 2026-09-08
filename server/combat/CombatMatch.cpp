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
} // namespace

combat_match::combat_match(std::string match_id, std::string match_code, std::string mode,
                           nlohmann::json ruleset, nlohmann::json map_metadata)
    : match_id_(std::move(match_id)),
      match_code_(std::move(match_code)),
      mode_(std::move(mode)),
      ruleset_(std::move(ruleset)),
      map_metadata_(std::move(map_metadata))
{
    if (ruleset_.contains("players") && ruleset_["players"].contains("max")) {
        max_players_ = ruleset_["players"]["max"].get<int>();
    }
    if (ruleset_.contains("unit_caps") && ruleset_["unit_caps"].contains("per_player")) {
        max_units_per_player_ = ruleset_["unit_caps"]["per_player"].get<int>();
    }
    death_handling_ = ruleset_.value("death_handling", "permanent");
    respawn_delay_seconds_ = ruleset_.value("respawn_delay_seconds", 10);
    duration_seconds_ = ruleset_.value("match_duration_seconds", 1800);
    countdown_remaining_.store(k_countdown_seconds);
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
            return;
        }
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
    return ruleset_.value("id", "");
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
    summary["map_id"] = map_metadata_.value("name", "");
    summary["player_count"] = players_.size();
    summary["max_players"] = max_players_;
    summary["phase"] = "lobby";
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
        apply_command(cmd);
    }
}

// ────────────────────────── Battle (match thread) ──────────────────────────

void combat_match::run_battle(const std::function<void(const combat_message&)>& broadcast,
                              const std::function<void(const combat_match&)>& on_end)
{
    if (phase_.load() != match_phase::lobby) return;
    phase_.store(match_phase::countdown);
    countdown_remaining_.store(k_countdown_seconds);

    {
        std::lock_guard<std::mutex> lock(units_mutex_);
        spawn_all_units();
    }

    const auto tick_period = std::chrono::milliseconds(100);
    const double tick_seconds = std::chrono::duration<double>(tick_period).count();
    auto next_tick = std::chrono::steady_clock::now();

    while (!ended_) {
        next_tick += tick_period;
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

    on_end(*this);
}

void combat_match::simulate_tick(double delta_seconds) {
    tick_.fetch_add(1);
    const match_phase phase = phase_.load();

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

    // Move units toward their destinations (pure combat — no building).
    for (auto& [id, u] : units_) {
        if (u.status != "alive") continue;
        if (u.destination_x < 0.0) continue;
        const double dx = u.destination_x - u.x;
        const double dy = u.destination_y - u.y;
        const double dist = std::sqrt(dx * dx + dy * dy);
        const double step = unit_speed_ * delta_seconds;
        if (dist <= step) {
            u.x = u.destination_x;
            u.y = u.destination_y;
            u.destination_x = -1.0;
            u.destination_y = -1.0;
        } else {
            u.x += dx / dist * step;
            u.y += dy / dist * step;
        }
        mark_dirty(u);
    }

    // Respawns (rulesets with death_handling == respawn_after_seconds).
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
                const int team = player_team(u.owner_player_id);
                u.x = spawn_x_for(team, 0);
                u.y = spawn_y_for(team);
                u.destination_x = -1.0;
                u.destination_y = -1.0;
                mark_dirty(u);
                outbox_.push_back({"unit_respawned", {{"unit_id", u.unit_id},
                                                      {"owner", u.owner_player_id}}});
            }
            it = respawn_timers_.erase(it);
        }
    }
}

void combat_match::spawn_all_units() {
    // Re-read players under the state lock, then spawn.
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
            combat_unit u;
            u.unit_id = m.member_id;
            u.owner_player_id = p.player_id;
            u.display_name = m.display_name;
            u.unit_class = m.unit_class;
            u.level = m.level;
            u.max_hp = 100.0;
            u.hp = u.max_hp;
            u.x = spawn_x_for(p.team, index);
            u.y = spawn_y_for(p.team);
            u.status = "alive";
            units_[u.unit_id] = u;
            dirty_units_.insert(u.unit_id);
            outbox_.push_back({"unit_spawned", {{"unit_id", u.unit_id},
                                                {"owner", p.player_id},
                                                {"team", p.team},
                                                {"name", m.display_name},
                                                {"unit_class", m.unit_class}}});
            index++;
        }
    }
}

void combat_match::apply_command(const combat_command& cmd) {
    if (cmd.type == "move") {
        std::vector<int64_t> unit_ids;
        if (cmd.payload.contains("unit_ids") && cmd.payload["unit_ids"].is_array()) {
            for (const auto& id : cmd.payload["unit_ids"]) {
                if (id.is_number_integer()) unit_ids.push_back(id.get<int64_t>());
            }
        }
        double tx = cmd.payload.value("target_x", -1.0);
        double ty = cmd.payload.value("target_y", -1.0);
        if (unit_ids.empty() || tx < 0.0 || ty < 0.0) return;
        tx = std::clamp(tx, 0.0, 1.0);
        ty = std::clamp(ty, 0.0, 1.0);
        std::lock_guard<std::mutex> lock(units_mutex_);
        for (int64_t id : unit_ids) {
            auto it = units_.find(id);
            if (it == units_.end()) continue;
            combat_unit& u = it->second;
            if (u.owner_player_id != cmd.player_id) continue;
            if (u.status != "alive") continue;
            u.destination_x = tx;
            u.destination_y = ty;
            mark_dirty(u);
        }
    } else if (cmd.type == "attack") {
        // Scaffold: attack is relayed as an event only — damage/combat
        // mechanics are a later milestone.
        std::lock_guard<std::mutex> lock(units_mutex_);
        outbox_.push_back({"attack", {{"player_id", cmd.player_id},
                                      {"target_unit", cmd.payload.value("target_unit_id", 0)}}});
    } else if (cmd.type == "ability") {
        std::lock_guard<std::mutex> lock(units_mutex_);
        outbox_.push_back({"ability", {{"player_id", cmd.player_id},
                                       {"ability_id", cmd.payload.value("ability_id", "")}}});
    } else if (cmd.type == "leave") {
        // Forfeit: the player's army abandons the field (permanent casualties).
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            for (auto& p : players_) {
                if (p.player_id == cmd.player_id) p.connected = false;
            }
        }
        std::lock_guard<std::mutex> lock(units_mutex_);
        std::vector<int64_t> owned;
        for (const auto& [id, u] : units_) {
            if (u.owner_player_id == cmd.player_id) owned.push_back(id);
        }
        for (int64_t id : owned) kill_unit(id);
    }
}

void combat_match::kill_unit(int64_t unit_id) {
    auto it = units_.find(unit_id);
    if (it == units_.end() || it->second.status != "alive") return;
    combat_unit& u = it->second;
    u.status = "dead";
    outbox_.push_back({"unit_died", {{"unit_id", u.unit_id}, {"owner", u.owner_player_id}}});

    if (death_handling_ == "respawn_after_seconds") {
        respawn_timers_[unit_id] = static_cast<double>(respawn_delay_seconds_);
    } else {
        // permanent (or revive_resource — revive is not implemented yet,
        // so those units are lost until it is).
        // Record the HP% the unit fell with — under `wounded` rulesets it
        // becomes the member's wound severity (heal from where they ended).
        casualties_.push_back({u.unit_id, u.owner_player_id, u.hp});
        removed_this_tick_.push_back(u.unit_id);
        units_.erase(it);
    }
}

void combat_match::broadcast_state(const std::function<void(const combat_message&)>& broadcast) {
    combat_message msg;
    const int64_t tick = tick_.load();
    msg.match_id = match_id_;
    msg.tick = tick;
    msg.phase_name = phase_name_string();
    msg.countdown = phase_.load() == match_phase::countdown ? countdown_remaining_.load() : -1.0;
    msg.battle_time = battle_time_.load();

    {
        std::lock_guard<std::mutex> lock(units_mutex_);
        const bool full = (tick % k_full_snapshot_interval_ticks == 0);
        msg.is_full_state = full;
        msg.type = full ? "match_state" : "match_update";
        if (full) {
            for (const auto& [id, u] : units_) msg.units.push_back(u);
        } else {
            for (int64_t id : dirty_units_) {
                auto it = units_.find(id);
                if (it != units_.end()) msg.units.push_back(it->second);
            }
        }
        dirty_units_.clear();
        msg.removed_units.swap(removed_this_tick_);
        msg.events.swap(outbox_);
    }
    broadcast(msg);
}

void combat_match::check_end_conditions(const std::function<void(const combat_message&)>& broadcast) {
    if (ended_) return;
    if (phase_.load() != match_phase::battle) return;

    std::lock_guard<std::mutex> lock(units_mutex_);

    // Wipe: any active team with zero alive units loses.
    const std::set<int> teams = active_teams();
    for (int team : teams) {
        if (alive_count_for_team(team) == 0) {
            if (mode_ == "pve" || teams.size() == 1) {
                // Solo/allied side wiped — defeat (winner 0 = no winner).
                winner_team_.store(0);
                finish_match("all_units_defeated");
                broadcast_ended(broadcast);
                return;
            }
            winner_team_.store(team == 1 ? 2 : 1);
            finish_match("team_wiped");
            broadcast_ended(broadcast);
            return;
        }
    }

    // Duration: survival is victory in PvE; most survivors win in PvP.
    if (battle_time_.load() >= static_cast<double>(duration_seconds_)) {
        if (mode_ == "pve") {
            winner_team_.store(alive_count_for_team(1) > 0 ? 1 : 0);
        } else {
            int best_team = -1;
            int best_count = -1;
            for (int team : teams) {
                const int count = alive_count_for_team(team);
                if (count > best_count) {
                    best_count = count;
                    best_team = team;
                } else if (count == best_count) {
                    best_team = -1;   // tie
                }
            }
            winner_team_.store(best_team);
        }
        finish_match("duration_expired");
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
    payload["winner_team"] = winner_team_.load();
    payload["reason"] = end_reason_;
    nlohmann::json casualties = nlohmann::json::array();
    for (const auto& c : casualties_) {
        casualties.push_back({{"member_id", c.member_id}, {"owner", c.owner_player_id},
                              {"end_hp", c.end_hp}});
    }
    payload["casualties"] = casualties;
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
    msg.battle_time = battle_time_.load();
    {
        std::lock_guard<std::mutex> lock(units_mutex_);
        for (const auto& [id, u] : units_) msg.units.push_back(u);
    }
    return msg;
}

std::vector<casualty_record> combat_match::casualties_snapshot() const {
    std::lock_guard<std::mutex> lock(units_mutex_);
    return casualties_;
}

std::string combat_match::phase_name_string() const {
    switch (phase_.load()) {
        case match_phase::lobby: return "lobby";
        case match_phase::countdown: return "countdown";
        case match_phase::battle: return "battle";
        case match_phase::ended: return "ended";
    }
    return "lobby";
}

int combat_match::alive_count_for_team(int team) const {
    int count = 0;
    for (const auto& [id, u] : units_) {
        if (u.status == "alive") {
            int t = player_team(u.owner_player_id);
            if (t == team) count++;
        }
    }
    return count;
}

std::set<int> combat_match::active_teams() const {
    std::set<int> teams;
    for (const auto& [id, u] : units_) {
        teams.insert(player_team(u.owner_player_id));
    }
    return teams;
}

int combat_match::match_duration_seconds() const {
    return duration_seconds_;
}

double combat_match::spawn_x_for(int team, int index) const {
    double base_x = 0.5;
    double base_y = 0.5;
    if (map_metadata_.contains("spawn_points") && map_metadata_["spawn_points"].is_array()) {
        for (const auto& sp : map_metadata_["spawn_points"]) {
            if (sp.value("team", 1) == team) {
                base_x = sp.value("x", 0.5);
                base_y = sp.value("y", 0.5);
                break;
            }
        }
    } else {
        // No spawn points in the map — split the field in half by team.
        base_x = team == 1 ? 0.25 : 0.75;
        base_y = 0.5;
    }
    return std::clamp(base_x + (index % 5) * k_spawn_scatter, 0.0, 1.0);
}

double combat_match::spawn_y_for(int team) const {
    // Matches spawn_x_for's base lookup; scatter handled on x only.
    double base_y = 0.5;
    if (map_metadata_.contains("spawn_points") && map_metadata_["spawn_points"].is_array()) {
        for (const auto& sp : map_metadata_["spawn_points"]) {
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

} // namespace combat
