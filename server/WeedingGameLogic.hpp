#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <set>
#include <random>
#include "GameConfigCache.hpp"

// Forward declare sqlite
namespace sqlite { class database; }

struct WeedingBoard {
    int grid_size;
    nlohmann::json board;  // 2D array: each cell = { plant_type, progress, actions_needed, is_smother_crop, is_blocked, is_accessible }
};

struct WeedingActionRequest {
    std::string action_type;  // "use_tool", "switch_tool", "plant", "forfeit"
    std::string tool_id;
    int target_x = -1;
    int target_y = -1;
};

struct WeedingActionResult {
    bool valid = false;
    std::string error;
    int round = 1;
    int actions_remaining = 2;
    bool pending_switch = false;
    std::string equipped_tool;
    std::vector<nlohmann::json> board_changes;  // delta rows, not full board
    bool won = false;
    int score = 0;
    int par = 0;
    std::string message;
};

namespace weeding_logic {

// Initialize a fresh board for a given level
nlohmann::json initialize_board(
    GameConfigCache& config_cache,
    int grid_size,
    const std::vector<std::pair<int, int>>& out_of_bounds,
    int difficulty,
    std::mt19937& rng
);

// Compute accessibility mask (flood-fill from bottom row through empty/smother squares)
void compute_accessibility(nlohmann::json& board, int grid_size);

// Compute par score
int compute_par(const nlohmann::json& board, int grid_size, const nlohmann::json& plants_config);

// Validate that a target square is accessible
bool is_accessible(const nlohmann::json& board, int grid_size, int x, int y);

// Process a single action and return updated state
WeedingActionResult process_action(
    GameConfigCache& config_cache,
    const nlohmann::json& session_json,
    const WeedingActionRequest& action,
    std::mt19937& rng
);

// Run plant spread on an empty-squares
void run_plant_spread(nlohmann::json& board, int grid_size, const nlohmann::json& plants_config, std::mt19937& rng);

// Check if all valid squares are smother crops
bool check_win(const nlohmann::json& board, int grid_size);

// Extract board_changes from a diff between old and new board states
std::vector<nlohmann::json> extract_board_changes(const nlohmann::json& old_board, const nlohmann::json& new_board, int grid_size);

} // namespace weeding_logic
