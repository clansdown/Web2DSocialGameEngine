#include "CombatMatchManager.hpp"
#include "RetinueDB.hpp"
#include "Database.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace combat {

namespace {
constexpr int64_t k_ended_match_retention_seconds = 600;   // purge ended matches after 10 min
} // namespace

combat_match_manager::~combat_match_manager() {
    shutdown_ = true;
    pending_cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
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
        workers_.emplace_back([this] { worker_loop(); });
        workers_.back().detach();   // process-lifetime workers; no shutdown join needed
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
    const std::string& ruleset_id,
    const nlohmann::json& ruleset,
    const nlohmann::json& map_metadata,
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
                                                ruleset, map_metadata);
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
    std::map<int64_t, std::vector<int64_t>> casualties_by_player;
    for (const auto& c : match.casualties_snapshot()) {
        casualties_by_player[c.owner_player_id].push_back(c.member_id);
    }
    if (casualties_by_player.empty() || !defer_fn_) return;
    defer_fn_([casualties_by_player]() {
        try {
            auto& db = Database::getInstance().gameDB();
            for (const auto& [player_id, member_ids] : casualties_by_player) {
                retinue_db::apply_casualties(db, player_id, member_ids);
            }
        } catch (const std::exception& e) {
            std::cerr << "[combat] failed to persist casualties: " << e.what() << std::endl;
        }
    });
}

} // namespace combat
