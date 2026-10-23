#include "CombatMatchManager.hpp"
#include "RetinueDB.hpp"
#include "Progression.hpp"
#include "MissionOfferings.hpp"
#include "Database.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace combat {

namespace {
constexpr int64_t k_ended_match_retention_seconds = 600;   // purge ended matches after 10 min
} // namespace

combat_match_manager::~combat_match_manager() {
    shutdown();
}

void combat_match_manager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        if (shutdown_) return;   // idempotent
        shutdown_ = true;
    }
    pending_cv_.notify_all();

    // Ask every active battle to stop so its worker returns promptly instead of
    // blocking the join for the rest of the match.
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        for (auto& [id, match] : matches_by_id_) {
            if (match) match->request_stop();
        }
    }

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();

    // No producers remain: drop the callbacks so nothing can post to a loop that
    // is about to be torn down.
    publish_fn_ = nullptr;
    defer_fn_ = nullptr;
}

void combat_match_manager::initialize(std::shared_ptr<combat_codec> codec,
                                      int sim_threads,
                                      std::function<void(const std::string&, const std::string&)> publish_fn,
                                      std::function<void(std::function<void()>)> defer_fn)
{
    codec_ = std::move(codec);
    publish_fn_ = std::move(publish_fn);
    defer_fn_ = std::move(defer_fn);
    sim_threads = std::clamp(sim_threads, 1, 256);
    start_pool(sim_threads);
}

void combat_match_manager::start_pool(int n) {
    for (int i = 0; i < n; i++) {
        // Joinable: shutdown() stops active matches and joins these before the
        // uWS App/loop is destroyed.
        workers_.emplace_back([this] { worker_loop(); });
    }
    std::cout << "[combat] started " << n << " simulation worker thread(s)" << std::endl;
}

void combat_match_manager::worker_loop() {
    for (;;) {
        std::shared_ptr<combat_match> match;
        {
            std::unique_lock<std::mutex> lock(pending_mutex_);
            pending_cv_.wait(lock, [&] {
                return shutdown_ || !pending_battle_.empty();
            });
            if (shutdown_ && pending_battle_.empty()) return;
            if (pending_battle_.empty()) continue;
            match = std::move(pending_battle_.front());
            pending_battle_.pop_front();
        }
        if (!match) continue;
        // Double-check the match still wants a battle (host may have left).
        if (match->phase() != match_phase::lobby) continue;

        match->run_battle(
            [this, match](const combat_message& msg) { publish_match_message(match->id(), msg); },
            [this](const combat_match& ended_match) { on_match_end(ended_match); }
        );
    }
}

void combat_match_manager::request_battle(const std::shared_ptr<combat_match>& match) {
    if (!match || match->phase() != match_phase::lobby) return;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_battle_.push_back(match);
    }
    pending_cv_.notify_one();
}

void combat_match_manager::publish_match_message(const std::string& match_id,
                                                 const combat_message& msg) {
    if (!codec_ || !publish_fn_) return;
    const std::string wire = codec_->serialize_message(msg);
    publish_fn_("match:" + match_id, wire);
}

std::string combat_match_manager::make_match_id() {
    const int64_t n = next_match_number_.fetch_add(1);
    return "m" + std::to_string(n);
}

std::string combat_match_manager::make_match_code() {
    // 6 chars from a confusion-free alphabet (no 0/O/1/I).
    static const char* alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::string code;
    code.reserve(6);
    for (int i = 0; i < 6; i++) {
        code += alphabet[code_rng_() % 32];
    }
    return code;
}

std::string combat_match_manager::create_pve_match(
    combat_match_config config,
    int64_t host_character_id,
    const std::string& host_display_name,
    const std::vector<retinue_member_snapshot>& host_retinue,
    std::string& error)
{
    purge_ended_matches();

    std::string match_id = make_match_id();
    std::string match_code = make_match_code();
    // Ensure code uniqueness (retry on the astronomically unlikely collision).
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        while (matches_by_code_.count(match_code) > 0) {
            match_code = make_match_code();
        }
    }

    auto match = std::make_shared<combat_match>(match_id, match_code, "pve",
                                                std::move(config));
    // Record the command log for deterministic replay when a replay dir is set.
    match->enable_replay_log(!replay_dir_.empty());
    if (!match->add_player(host_character_id, host_display_name, host_retinue)) {
        error = "could not add host to match";
        return "";
    }

    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        matches_by_id_[match_id] = match;
        matches_by_code_[match_code] = match;
    }
    return match_id;
}

std::shared_ptr<combat_match> combat_match_manager::join_match(
    const std::string& match_code,
    int64_t character_id,
    const std::string& display_name,
    const std::vector<retinue_member_snapshot>& retinue,
    std::string& error)
{
    std::shared_ptr<combat_match> match = get_match_by_code(match_code);
    if (!match) {
        error = "match not found";
        return nullptr;
    }
    if (match->contains_player(character_id)) {
        error = "already in match";
        return nullptr;
    }
    if (!match->is_joinable()) {
        error = "match is full or has started";
        return nullptr;
    }
    if (!match->add_player(character_id, display_name, retinue)) {
        error = "could not join match";
        return nullptr;
    }
    return match;
}

void combat_match_manager::leave_match(const std::string& match_id, int64_t character_id) {
    handle_disconnect(match_id, character_id);
}

void combat_match_manager::mark_ready(const std::string& match_id, int64_t character_id,
                                      bool ready) {
    auto match = get_match(match_id);
    if (!match) return;
    if (match->phase() == match_phase::deploy) {
        match->set_player_deploy_ready(character_id, ready);
        return;
    }
    match->set_player_ready(character_id, ready);
    if (ready && match->phase() == match_phase::lobby && match->all_players_ready()) {
        request_battle(match);
    }
}

bool combat_match_manager::connect_player(const std::string& match_id, int64_t character_id) {
    auto match = get_match(match_id);
    if (!match) return false;
    if (!match->contains_player(character_id)) return false;
    // Only allow one live socket per player.
    const auto players = match->players_snapshot();
    for (const auto& p : players) {
        if (p.player_id == character_id && p.connected) return false;
    }
    match->set_player_connected(character_id, true);
    return true;
}

void combat_match_manager::handle_disconnect(const std::string& match_id, int64_t character_id) {
    auto match = get_match(match_id);
    if (!match) return;
    const match_phase phase = match->phase();
    if (phase == match_phase::lobby) {
        // Remove the player so a lobby can't be stuck on a vanished socket.
        if (match->remove_player(character_id)) {
            std::cout << "[combat] player " << character_id
                      << " left lobby match " << match_id << std::endl;
        }
    } else {
        match->set_player_connected(character_id, false);
        if (phase == match_phase::countdown || phase == match_phase::battle) {
            // Forfeit the player's army through the normal command path.
            combat_command cmd;
            cmd.player_id = character_id;
            cmd.type = "leave";
            match->enqueue_command(cmd);
        }
    }
}

std::shared_ptr<combat_match> combat_match_manager::get_match(const std::string& match_id) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = matches_by_id_.find(match_id);
    return it == matches_by_id_.end() ? nullptr : it->second;
}

std::shared_ptr<combat_match> combat_match_manager::get_match_by_code(const std::string& match_code) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = matches_by_code_.find(match_code);
    return it == matches_by_code_.end() ? nullptr : it->second;
}

std::vector<nlohmann::json> combat_match_manager::list_lobby_matches() const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    std::vector<nlohmann::json> result;
    for (const auto& [id, match] : matches_by_id_) {
        if (match->phase() == match_phase::lobby) {
            result.push_back(match->lobby_summary());
        }
    }
    return result;
}

void combat_match_manager::purge_ended_matches() {
    const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::lock_guard<std::mutex> lock(registry_mutex_);
    for (auto it = matches_by_id_.begin(); it != matches_by_id_.end();) {
        const auto& match = it->second;
        if (match->phase() == match_phase::ended &&
            match->ended_at_seconds() > 0 &&
            now - match->ended_at_seconds() > k_ended_match_retention_seconds) {
            matches_by_code_.erase(match->code());
            it = matches_by_id_.erase(it);
        } else {
            ++it;
        }
    }
}

void combat_match_manager::on_match_end(const combat_match& match) {
    // Persist casualties on the uWS loop thread (SQLite is single-owner there).
    // `wounded` rulesets apply the no-death model: members who fell are wounded
    // to their individual end-of-combat HP% (health drops, recovery clock
    // restarts, status stays active) instead of becoming casualties. Recent
    // battle win/loss timestamps feed the derived household-morale decay terms.
    // Draw stamps neither (§14.3). Fielded members earn completion-IXP
    // (rts_design.md §14.1) — warband-derived, victory-multiplied.
    const bool wounded_mode = (match.death_handling() == "wounded");
    std::map<int64_t, std::vector<retinue_db::wound_spec>> wounded_by_player;
    std::map<int64_t, std::vector<int64_t>> casualties_by_player;
    for (const auto& c : match.casualties_snapshot()) {
        if (wounded_mode) {
            wounded_by_player[c.owner_player_id].push_back({c.member_id, c.end_hp});
        } else {
            casualties_by_player[c.owner_player_id].push_back(c.member_id);
        }
    }
    const int winner = match.winner();
    std::map<int64_t, bool> won_by_player;
    // PvE winner semantics (rts_design.md §13.3): 1 = players win, 0 = players
    // lose, -1 = draw. A draw stamps NEITHER morale timestamp (§14.3).
    if (winner == 1) {
        for (const auto& p : match.players_snapshot()) won_by_player[p.player_id] = true;
    } else if (winner == 0) {
        for (const auto& p : match.players_snapshot()) won_by_player[p.player_id] = false;
    }
    // §3.5: a forfeiting player records a defeat regardless of team outcome.
    const std::set<int64_t> forfeited = match.forfeited_players();
    for (int64_t pid : forfeited) won_by_player[pid] = false;

    // Per-player XP maps (fixed per-fielded-member, completion-only). A leaver
    // earns nothing for a match they abandoned (§3.5).
    const double xp_base = match.xp_base();
    const double victory_mult =
        match.rewards_config().value("victory_multiplier", 1.5);
    const double gain = (winner == 1) ? xp_base * victory_mult : xp_base;
    const std::vector<double> xp_curve =
        match.rewards_config().value("xp_curve", std::vector<double>{});
    std::map<int64_t, std::map<int64_t, double>> xp_by_player;
    if (gain > 0.0) {
        for (const auto& p : match.players_snapshot()) {
            if (forfeited.count(p.player_id) != 0) continue;
            for (const auto& m : p.retinue) {
                xp_by_player[p.player_id][m.member_id] = gain;
            }
        }
    }

    // Class max_level caps from the match's combatant-config snapshot (never
    // GameConfigCache on this thread).
    std::map<std::string, int> class_max_levels;
    if (match.player_combatants().is_object()) {
        for (const auto& [cid, cfg] : match.player_combatants().items()) {
            if (cfg.is_object() && cfg.contains("max_level") && cfg["max_level"].is_number()) {
                class_max_levels[cid] = cfg["max_level"].get<int>();
            }
        }
    }

    // Determinism replay (M10): capture the tick-exact command log while the
    // match thread still owns the state, then write it on the loop thread.
    if (!replay_dir_.empty()) {
        nlohmann::json record = match.replay_record();
        const std::string dir = replay_dir_;
        const std::string replay_match_id = match.id();
        auto write_replay = [dir, replay_match_id, record]() {
            try {
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                std::ofstream out(dir + "/" + replay_match_id + ".json", std::ios::binary);
                if (out) out << record.dump(2);
            } catch (const std::exception& e) {
                std::cerr << "[combat] failed to write replay: " << e.what() << std::endl;
            }
        };
        if (defer_fn_) {
            defer_fn_(write_replay);
        } else {
            write_replay();
        }
    }

    // Mission offerings + counters (rts_design.md §3.1/§14): tally enemy deaths
    // by class, capture the warband end state, and snapshot the attempt.
    const std::map<std::string, int> enemy_deaths = match.enemy_deaths();
    const std::map<std::string, int> enemy_fled = match.enemy_fled();
    const std::string offering_id = match.offering_id();
    const int64_t attempt_id = match.mission_attempt_id();
    const std::vector<progression::milestone_condition> milestone_conditions =
        progression::parse_milestone_conditions(
            nlohmann::json{{"milestones", match.milestone_conditions()}});

    nlohmann::json end_state;
    end_state["deaths"] = nlohmann::json::object();
    for (const auto& [cls, n] : enemy_deaths) end_state["deaths"][cls] = n;
    end_state["fled"] = nlohmann::json::object();
    for (const auto& [cls, n] : enemy_fled) end_state["fled"][cls] = n;
    nlohmann::json survivors = nlohmann::json::array();
    for (const auto& u : match.build_full_state().units) {
        if (u.is_enemy && u.status == "alive") {
            survivors.push_back({{"unit_class", u.unit_class}, {"hp", u.hp}});
        }
    }
    end_state["survivors"] = survivors;

    nlohmann::json participants = nlohmann::json::array();
    std::vector<int64_t> participant_ids;
    for (const auto& p : match.players_snapshot()) {
        participant_ids.push_back(p.player_id);
        nlohmann::json members = nlohmann::json::array();
        for (const auto& m : p.retinue) members.push_back(m.member_id);
        participants.push_back({{"character_id", p.player_id}, {"fielded_member_ids", members}});
    }
    const std::string attempt_result =
        winner == 1 ? "victory" : (winner == 0 ? "defeat" : "draw");
    const std::string participants_json = participants.dump();
    const std::string end_state_json = end_state.dump();

    if (!defer_fn_) return;
    defer_fn_([wounded_by_player, casualties_by_player, won_by_player, xp_by_player,
               xp_curve, class_max_levels, enemy_deaths, offering_id, attempt_id,
               milestone_conditions, participant_ids, participants_json, end_state_json,
               attempt_result, winner]() {
        try {
            auto& db = Database::getInstance().gameDB();
            std::function<int(const std::string&)> max_level_for =
                [class_max_levels](const std::string& unit_class) {
                    auto it = class_max_levels.find(unit_class);
                    return it == class_max_levels.end() ? 99 : it->second;
                };
            for (const auto& [player_id, wounds] : wounded_by_player) {
                retinue_db::apply_wounds(db, player_id, wounds);
            }
            for (const auto& [player_id, member_ids] : casualties_by_player) {
                retinue_db::apply_casualties(db, player_id, member_ids);
            }
            for (const auto& [player_id, xp_map] : xp_by_player) {
                retinue_db::grant_experience(db, player_id, xp_map, xp_curve, max_level_for);
            }

            const int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

            // Attempt log + per-class slain counters + counter-based milestones.
            // Counters increment for every participant on every attempt (victory
            // or not); deaths only — a routed enemy is not slain.
            if (attempt_id > 0) {
                mission_offerings::record_attempt_end(db, attempt_id, attempt_result,
                                                      participants_json, end_state_json, now);
            }
            for (int64_t pid : participant_ids) {
                if (!enemy_deaths.empty()) {
                    mission_offerings::increment_counters(db, pid, enemy_deaths, now);
                }
                if (!milestone_conditions.empty()) {
                    progression::evaluate_milestone_conditions(
                        db, static_cast<int>(pid), milestone_conditions, now);
                }
            }

            if (!won_by_player.empty()) {
                for (const auto& [player_id, won] : won_by_player) {
                    if (won) {
                        db << "UPDATE fiefdoms SET last_victory_ts = ? WHERE owner_id = ?;"
                           << now << player_id;
                    } else {
                        db << "UPDATE fiefdoms SET last_defeat_ts = ? WHERE owner_id = ?;"
                           << now << player_id;
                    }
                }
            }

            // A won mission consumes the offering for every participant (one-shot).
            if (!offering_id.empty() && winner == 1) {
                for (int64_t pid : participant_ids) {
                    mission_offerings::record_completion(db, pid, offering_id, now);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[combat] failed to persist combat results: " << e.what() << std::endl;
        }
    });
}

} // namespace combat
