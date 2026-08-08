#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace combat {

// Phases of a combat match. Lobby runs on the uWS loop thread; countdown,
// battle, and ended-run bookkeeping live on the match's worker thread.
enum class match_phase {
    lobby,
    countdown,
    battle,
    ended
};

// One soldier in battle. unit_id == retinue_members.id — the combat game is
// pure combat with no unit construction, so units map 1:1 onto the retinue.
struct combat_unit {
    int64_t unit_id = 0;
    int64_t owner_player_id = 0;
    std::string display_name;
    std::string unit_class;
    int level = 1;
    double x = 0.0;                 // normalized 0..1 map coordinates
    double y = 0.0;
    double destination_x = -1.0;    // -1 = no destination
    double destination_y = -1.0;
    double hp = 100.0;
    double max_hp = 100.0;
    std::string status = "alive";   // alive | dead (respawnable) | retreated
};

// A player's army snapshot, captured when they join the match.
struct retinue_member_snapshot {
    int64_t member_id = 0;
    std::string display_name;
    std::string unit_class;
    int level = 1;
    bool is_knight = false;
};

struct combat_player {
    int64_t player_id = 0;          // == characters.id
    int team = 0;
    std::string display_name;
    bool ready = false;
    bool connected = false;
    std::vector<retinue_member_snapshot> retinue;
};

// A decision from a player. Created on the uWS loop thread, applied by the
// match thread at the next tick boundary.
struct combat_command {
    int64_t player_id = 0;
    int64_t sequence = 0;
    std::string type;               // move | attack | ability | leave
    nlohmann::json payload;
};

// A transient thing the client should be told about (damage, death, spawn).
struct combat_event {
    std::string type;               // unit_spawned | unit_died | unit_respawned | attack | ability | system
    nlohmann::json payload;
};

// A casualty record: which retinue member of which character was lost.
struct casualty_record {
    int64_t member_id = 0;
    int64_t owner_player_id = 0;
};

// Wire-neutral message. Game logic builds these structs; the active
// combat_codec converts them to/from wire bytes. Game logic NEVER knows the
// wire format — this is the codec pluggability point.
struct combat_message {
    std::string type;               // auth | welcome | match_state | match_update | match_ended
                                    // | chat | voice | command | ready | leave | request_state | error
    std::string match_id;
    int64_t tick = -1;
    bool is_full_state = false;

    // Hot-path state fields (match_state / match_update):
    std::vector<combat_unit> units;         // dirty units, or all units for full state
    std::vector<int64_t> removed_units;     // permanently removed this tick
    std::vector<combat_event> events;       // transient events this tick
    std::string phase_name;                 // lobby | countdown | battle | ended
    double countdown = -1.0;                // seconds remaining (countdown phase only)
    double battle_time = 0.0;               // seconds since battle start

    // General payload (welcome, chat, voice, errors, configs, results):
    nlohmann::json json_payload;
};

} // namespace combat
