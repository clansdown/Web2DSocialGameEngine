#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace combat {

// Phases of a combat match. Lobby runs on the uWS loop thread; countdown,
// battle, and ended-run bookkeeping live on the match's worker thread.
// rts_design.md §13.1: lobby → deploy → countdown → battle → ended.
enum class match_phase {
    lobby,
    deploy,
    countdown,
    battle,
    ended
};

// The full command vocabulary a unit can hold in its order queue
// (rts_design.md §9). Single executor, FIFO queue; a new explicit order
// replaces the queue (queued orders append).
enum class combat_order_type {
    move,           // Path to a point. Never auto-engages (D41).
    attack_move,    // Move along a path, engage hostiles en route.
    attack_unit,    // Focus-fire a specific target.
    attack_ground,  // AoE "Lob": Attack accepts a ground point (M5; reserved now).
    stop,           // Cancel orders; stand; engage in aggro radius.
    hold,           // Stand firm; engage within range, never leave the square.
    patrol,         // Loop/ping-pong waypoints, engaging along the path.
    stance,         // aggressive | defensive | hold (changes acquisition).
    formation,      // Persistent movement modifier (M6; reserved now).
    construct       // Channeled structure build (M3; reserved now).
};

struct combat_order {
    combat_order_type type = combat_order_type::stop;
    double target_x = 0.0;
    double target_y = 0.0;
    int64_t target_unit_id = 0;
    bool queued = false;
};

// A snapshot of the enemy warband's composition at match creation. The warband
// is fixed at create (D62); joining/leaving never changes it.
struct warband_spawn_entry {
    std::string unit_class;
    int level = 1;
    int count = 0;
    std::string group;          // group label for AI cohesion/behavior
    std::string tactic;         // charge | hold | skirmish | guard | reinforce
    std::string aggro_model;    // local | call_to_arms | global
};

// Everything the simulation thread needs from config. Resolved on the uWS loop
// thread at match creation into a snapshot — the sim thread NEVER touches
// GameConfigCache (hot-reload singleton) or SQLite directly (threading rule).
struct combat_match_config {
    nlohmann::json ruleset;
    nlohmann::json map_metadata;
    nlohmann::json player_combatants;   // class configs (JSON copies)
    nlohmann::json enemy_combatants;
    nlohmann::json structures;           // temp-structure defs (M3)
    nlohmann::json formations;           // formation shapes (M6)
    nlohmann::json profiles;             // enemy AI styles/axes (M4)
    nlohmann::json rewards;              // XP curve / multipliers (M2)
    nlohmann::json opportunity;          // the mission opportunity (warband fixed, xp_base, tier)
    // Auto-generated offering id (empty for authored/legacy creates). The
    // mission_attempt row id links this match to its attempt log entry.
    std::string offering_id;
    int64_t mission_attempt_id = 0;
    // Counter-based milestone conditions (progression.json "milestones"),
    // snapshotted at create and evaluated after the match's deaths are counted.
    nlohmann::json milestone_conditions = nlohmann::json::array();
    std::vector<warband_spawn_entry> warband;
    // Deterministic seed (D27). 0 = seed from std::random_device; a non-zero
    // value (from --combat-seed) makes matches reproducible for replay/tests.
    uint32_t seed = 0;
};

// Per-unit class stats resolved from the combatant config at spawn time
// (level-indexed arrays extrapolated per combatants.hpp conventions).
struct combat_unit_stats {
    double max_hp = 100.0;
    double melee_damage = 0.0;
    double ranged_damage = 0.0;
    double magical_damage = 0.0;
    double melee_defense = 0.0;
    double ranged_defense = 0.0;
    double magical_defense = 0.0;
    double direction_defense_front = 1.0;   // default per rts_design.md §5.4
    double direction_defense_side = 0.6;
    double direction_defense_back = 0.35;
    double dodge_front = 0.0;
    double dodge_side = 0.0;
    double dodge_back = 0.0;
    double attack_range = 0.1;              // normalized units
    double attack_interval = 1.5;           // seconds between attacks
    std::string attack_type = "melee";      // melee | ranged | magical
    double turn_rate = 3.0;                 // radians/sec rotate-toward-target
    double movement_speed = 1.0;            // multiplier on the 0.04 base (D10)
    bool is_aoe = false;                    // splash-only (M5)
    double splash_radius = 0.0;             // normalized units (~1.5 tiles)
    double radius = 0.03;                   // body size (normalized units); from the
                                            // required `size` config field. Drives the
                                            // sim collision/deploy spacing and the
                                            // client's drawn sprite size.
};

// One soldier in battle. unit_id == retinue_members.id for player units;
// warband enemies use a NEGATIVE id namespace with environment owner 0
// (rts_design.md §13.3). The combat game is pure combat with no construction,
// so player units map 1:1 onto the retinue.
struct combat_unit {
    int64_t unit_id = 0;
    int64_t owner_player_id = 0;
    int team = 1;
    std::string display_name;
    std::string unit_class;
    int level = 1;
    double x = 0.0;                 // normalized 0..1 map coordinates
    double y = 0.0;
    double facing = 0.0;            // radians, +x axis; drawn toward heading
    double hp = 100.0;
    double max_hp = 100.0;
    std::string status = "alive";   // alive | dead (respawnable) | retreated
    bool is_enemy = false;          // environment-owned warband unit

    combat_unit_stats stats;

    // Order queue + runtime state (match thread only).
    std::deque<combat_order> orders;
    int64_t target_id = 0;          // current attack target (0 = none)
    double cooldown_remaining = 0.0;
    std::string stance = "defensive";   // aggressive | defensive | hold
    std::string formation_tag;          // formation membership (M6)
    int64_t channeling_structure_id = 0; // >0 while channeling a construct (§7.2)
    int64_t garrison_structure_id = 0;  // >0 while garrisoned in a tower (§7.4)
    int warband_group = -1;             // index into config_.warband for enemy AI (§11)

    // Movement: waypoints produced by A* toward the head order's target.
    std::deque<std::pair<double, double>> waypoints;

    // Patrol ping-pong anchor (§9): the head order's target is the far
    // endpoint; this is the point the unit loops back to.
    double patrol_home_x = 0.0;
    double patrol_home_y = 0.0;
    bool patrol_home_set = false;

    // Enemy group movement (§8.1): when set, the unit advances by sampling its
    // group's shared flow field instead of holding an A* waypoint list.
    bool use_flow_field = false;
};

// A deployable combat structure (rts_design.md §7). RAM-only — no SQL table.
// Declared now (wire array added alongside units); populated in M3.
struct combat_structure {
    int64_t id = 0;
    int64_t owner_player_id = 0;
    int team = 1;
    std::string type;
    double x = 0.0;
    double y = 0.0;
    int footprint = 1;
    double hp = 0.0;
    double max_hp = 1.0;
    std::string state = "building";     // building | active | destroyed
    bool damaged = false;               // took damage (only then can 0 HP destroy it)
    int64_t garrisoned_unit_id = 0;     // tower garrison (M3)
};

// A player's army snapshot, captured when they join the match.
struct retinue_member_snapshot {
    int64_t member_id = 0;
    std::string display_name;
    std::string unit_class;
    int level = 1;
    bool is_knight = false;
    double health = 100.0;          // persistent roster health % (HP bridge, §5.2)
    bool fieldable = true;          // ≥ min_deploy_hp; filtered at snapshot build
};

struct combat_player {
    int64_t player_id = 0;          // == characters.id
    int team = 0;
    std::string display_name;
    bool ready = false;             // lobby ready (all → lobby→deploy)
    bool deploy_ready = false;      // deploy ready (all → cut deploy short)
    bool connected = false;
    // Forfeit (§3.5): the player left mid-battle. Their units withdrew
    // UNWOUNDED (no casualty records) and they record a defeat regardless of
    // how the team's match resolves.
    bool forfeited = false;
    std::vector<retinue_member_snapshot> retinue;
};

// A decision from a player. Created on the uWS loop thread, applied by the
// match thread at the next tick boundary.
struct combat_command {
    int64_t player_id = 0;
    int64_t sequence = 0;
    std::string type;               // move | attack | stop | hold | patrol |
                                    // stance | formation | construct | ability | leave
    // Default to an empty OBJECT (never null): apply_command reads keys from it
    // with value(), which throws type_error.306 on a null value. Commands built
    // without a payload (e.g. the "leave" forfeit on disconnect) rely on this.
    nlohmann::json payload = nlohmann::json::object();
};

// A transient thing the client should be told about (damage, death, spawn).
struct combat_event {
    std::string type;               // unit_spawned | unit_died | structure_spawned
                                    // | attack | hold_fire | ability | system
    nlohmann::json payload;
};

// A casualty record: which retinue member of which character was lost, and
// their HP% at the moment they fell (0..100). The end HP drives wound severity
// under the `wounded` (no-death) ruleset — members heal from where they ended.
struct casualty_record {
    int64_t member_id = 0;
    int64_t owner_player_id = 0;
    double end_hp = 100.0;      // unit HP% when it fell (combat hp scale 0..100)
};

// Wire-neutral message. Game logic builds these structs; the active
// combat_codec converts them to/from wire bytes. Game logic NEVER knows the
// wire format — this is the codec pluggability point.
struct combat_message {
    std::string type;               // auth | welcome | match_state | match_update | match_ended
                                    // | chat | voice | command | ready | roster | leave | request_state | error
    std::string match_id;
    int64_t tick = -1;
    bool is_full_state = false;

    // Hot-path state fields (match_state / match_update):
    std::vector<combat_unit> units;           // dirty units, or all units for full state
    std::vector<combat_structure> structures; // dirty structures, or all for full state (M3)
    std::vector<int64_t> removed_units;       // permanently removed this tick
    std::vector<combat_event> events;         // transient events this tick
    std::string phase_name;                   // lobby | deploy | countdown | battle | ended
    double countdown = -1.0;                  // seconds remaining (countdown phase only)
    double deploy_time = -1.0;                // seconds remaining (deploy phase only; M2)
    double battle_time = 0.0;                 // seconds since battle start

    // General payload (welcome, chat, voice, errors, configs, results):
    nlohmann::json json_payload;
};

} // namespace combat