#pragma once
#include "CombatCodec.hpp"
#include "CombatMatch.hpp"
#include "CombatTypes.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace combat {

// Holds every combat match in RAM and runs battles on a bounded pool of
// worker threads (one worker per active match). Broadcasts go through uWS
// topic publish (thread-safe from any thread); SQLite writes are marshalled
// back to the uWS loop thread via Loop::defer so the database stays
// single-owner. See docs/combat_protocol.md.
class combat_match_manager {
public:
    combat_match_manager() = default;
    ~combat_match_manager();

    void initialize(std::shared_ptr<combat_codec> codec,
                    int sim_threads,
                    std::function<void(const std::string&, const std::string&)> publish_fn,
                    std::function<void(std::function<void()>)> defer_fn);

    // Creates a PvE lobby match with the host as first player.
    // Returns the match id, or "" with `error` set on failure.
    std::string create_pve_match(const std::string& ruleset_id,
                                 const nlohmann::json& ruleset,
                                 const nlohmann::json& map_metadata,
                                 int64_t host_character_id,
                                 const std::string& host_display_name,
                                 const std::vector<retinue_member_snapshot>& host_retinue,
                                 std::string& error);

    // Joins a lobby match by code. Returns the match, or nullptr with `error` set.
    std::shared_ptr<combat_match> join_match(const std::string& match_code,
                                             int64_t character_id,
                                             const std::string& display_name,
                                             const std::vector<retinue_member_snapshot>& retinue,
                                             std::string& error);

    void leave_match(const std::string& match_id, int64_t character_id);
    void mark_ready(const std::string& match_id, int64_t character_id, bool ready);
    bool connect_player(const std::string& match_id, int64_t character_id);
    void handle_disconnect(const std::string& match_id, int64_t character_id);

    std::shared_ptr<combat_match> get_match(const std::string& match_id);
    std::shared_ptr<combat_match> get_match_by_code(const std::string& match_code);
    std::vector<nlohmann::json> list_lobby_matches() const;

    const std::shared_ptr<combat_codec>& codec() const { return codec_; }

    // Serializes and publishes a message to a match topic (called by match threads).
    void publish_match_message(const std::string& match_id, const combat_message& msg);

private:
    void request_battle(const std::shared_ptr<combat_match>& match);
    void worker_loop();
    void start_pool(int n);
    void purge_ended_matches();
    void on_match_end(const combat_match& match);
    std::string make_match_id();
    std::string make_match_code();

    std::shared_ptr<combat_codec> codec_;
    std::function<void(const std::string&, const std::string&)> publish_fn_;
    std::function<void(std::function<void()>)> defer_fn_;

    mutable std::mutex registry_mutex_;
    std::unordered_map<std::string, std::shared_ptr<combat_match>> matches_by_id_;
    std::unordered_map<std::string, std::shared_ptr<combat_match>> matches_by_code_;

    std::mutex pending_mutex_;
    std::condition_variable pending_cv_;
    std::deque<std::shared_ptr<combat_match>> pending_battle_;
    std::vector<std::thread> workers_;
    bool shutdown_ = false;

    std::atomic<int64_t> next_match_number_{1};
    std::mt19937 code_rng_{std::random_device{}()};
};

} // namespace combat
