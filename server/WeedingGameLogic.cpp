#include "WeedingGameLogic.hpp"
#include <iostream>
#include <algorithm>
#include <queue>
#include <cmath>
#include <unordered_map>

namespace weeding_logic {

nlohmann::json initialize_board(
    GameConfigCache& config_cache,
    int grid_size,
    const std::vector<std::pair<int, int>>& out_of_bounds,
    int difficulty,
    std::mt19937& rng)
{
    nlohmann::json board = nlohmann::json::array();
    const auto& plants = config_cache.getWeedingPlants();

    if (!plants.is_object() || plants.empty()) {
        // Fallback: empty board if no plants config
        for (int y = 0; y < grid_size; ++y) {
            nlohmann::json row = nlohmann::json::array();
            for (int x = 0; x < grid_size; ++x) {
                nlohmann::json cell = nlohmann::json::object();
                cell["plant_type"] = nullptr;
                cell["progress"] = 0;
                cell["actions_needed"] = 0;
                cell["is_smother_crop"] = false;
                cell["is_blocked"] = false;
                cell["is_accessible"] = false;
                row.push_back(cell);
            }
            board.push_back(row);
        }

        // Mark out_of_bounds
        for (const auto& oob : out_of_bounds) {
            if (oob.first >= 0 && oob.first < grid_size && oob.second >= 0 && oob.second < grid_size) {
                board[oob.second][oob.first]["is_blocked"] = true;
            }
        }
        compute_accessibility(board, grid_size);
        return board;
    }

    // Collect all plant IDs that are weeds (not smother crops)
    std::vector<std::string> weed_ids;
    for (auto it = plants.begin(); it != plants.end(); ++it) {
        if (!it.value().contains("is_smother_crop") || !it.value()["is_smother_crop"].get<bool>()) {
            weed_ids.push_back(it.key());
        }
    }

    // Select eligible weeds based on each plant's min_difficulty field
    std::vector<std::string> eligible_weeds;
    for (const auto& wid : weed_ids) {
        int min_diff = plants[wid].value("min_difficulty", 1);
        if (difficulty >= min_diff) {
            eligible_weeds.push_back(wid);
        }
    }

    // Build board
    std::uniform_int_distribution<int> weed_pick(0, (int)eligible_weeds.size() - 1);

    // Partition into cells: count valid cells
    struct Cell { int x, y; };
    std::vector<Cell> valid_cells;
    for (int y = 0; y < grid_size; ++y) {
        nlohmann::json row = nlohmann::json::array();
        for (int x = 0; x < grid_size; ++x) {
            nlohmann::json cell = nlohmann::json::object();
            cell["plant_type"] = nullptr;
            cell["progress"] = 0;
            cell["actions_needed"] = 0;
            cell["is_smother_crop"] = false;
            cell["is_blocked"] = false;
            cell["is_accessible"] = false;
            row.push_back(cell);

            bool blocked = false;
            for (const auto& oob : out_of_bounds) {
                if (oob.first == x && oob.second == y) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked) {
                valid_cells.push_back({x, y});
            }
        }
        board.push_back(row);
    }

    // Mark out_of_bounds
    for (const auto& oob : out_of_bounds) {
        if (oob.first >= 0 && oob.first < grid_size && oob.second >= 0 && oob.second < grid_size) {
            board[oob.second][oob.first]["is_blocked"] = true;
        }
    }

    // Fill every non-blocked cell with a weed
    for (const auto& cell : valid_cells) {
        std::string weed_id = eligible_weeds[weed_pick(rng)];
        board[cell.y][cell.x]["plant_type"] = weed_id;

        int plant_hp = plants[weed_id].value("hp", 100);
        board[cell.y][cell.x]["actions_needed"] = plant_hp;
        board[cell.y][cell.x]["progress"] = 0;
    }

    compute_accessibility(board, grid_size);
    return board;
}

void compute_accessibility(nlohmann::json& board, int grid_size) {
    // Reset accessibility
    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            board[y][x]["is_accessible"] = false;
        }
    }

    // BFS from bottom row
    std::queue<std::pair<int, int>> q;
    std::set<std::pair<int, int>> visited;

    // Start from all bottom-row empty or smother crop squares
    for (int x = 0; x < grid_size; ++x) {
        int y = grid_size - 1;
        if (board[y][x].contains("is_blocked") && board[y][x]["is_blocked"].get<bool>()) continue;
        bool is_empty = board[y][x]["plant_type"].is_null();
        bool is_smother = board[y][x].contains("is_smother_crop") && board[y][x]["is_smother_crop"].get<bool>();
        (void)is_empty;
        (void)is_smother;
        q.push({x, y});
        visited.insert({x, y});
    }

    // Direction vectors: down, up, left, right
    const int dx[] = {0, 0, -1, 1};
    const int dy[] = {1, -1, 0, 0};

    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();

        for (int d = 0; d < 4; ++d) {
            int nx = cx + dx[d];
            int ny = cy + dy[d];
            if (nx < 0 || nx >= grid_size || ny < 0 || ny >= grid_size) continue;
            if (board[ny][nx].contains("is_blocked") && board[ny][nx]["is_blocked"].get<bool>()) continue;
            if (visited.count({nx, ny})) continue;

            bool is_empty = board[ny][nx]["plant_type"].is_null();
            bool is_smother = board[ny][nx].contains("is_smother_crop") && board[ny][nx]["is_smother_crop"].get<bool>();
            if (is_empty || is_smother) {
                q.push({nx, ny});
                visited.insert({nx, ny});
            }
        }
    }

    // Now mark any weed square as accessible if it has an empty/smother neighbor (including diagonally would be more forgiving... just use cardinal)
    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            if (board[y][x].contains("is_blocked") && board[y][x]["is_blocked"].get<bool>()) continue;
            if (board[y][x]["is_smother_crop"].get<bool>()) continue;
            bool is_weed = !board[y][x]["plant_type"].is_null();
            if (!is_weed) continue;

            // Check if any adjacent square is in the visited (accessible) set
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx[d];
                int ny = y + dy[d];
                if (nx >= 0 && nx < grid_size && ny >= 0 && ny < grid_size) {
                    if (visited.count({nx, ny})) {
                        board[y][x]["is_accessible"] = true;
                        break;
                    }
                }
            }
        }
    }

    // Mark all visited cells (bottom row + reachable empty/smother) as accessible
    for (const auto& pos : visited) {
        int vx = pos.first;
        int vy = pos.second;
        board[vy][vx]["is_accessible"] = true;
    }

    // Smother crops propagate the flood and are themselves accessible
    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            if (board[y][x].contains("is_blocked") && board[y][x]["is_blocked"].get<bool>()) continue;
            if (board[y][x].contains("is_smother_crop") && board[y][x]["is_smother_crop"].get<bool>()) {
                board[y][x]["is_accessible"] = true;
            }
        }
    }
}

int estimate_weed_clearing_rounds(const nlohmann::json& board, int grid_size, const nlohmann::json& plants_config, int difficulty) {
    // Base actions: sum all weed clearing costs
    int total_base_actions = 0;
    std::unordered_map<std::string, int> weed_type_counts;
    int total_weeds = 0;

    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            if (board[y][x].contains("is_blocked") && board[y][x]["is_blocked"].get<bool>()) continue;
            if (board[y][x]["plant_type"].is_null()) continue;
            if (board[y][x].contains("is_smother_crop") && board[y][x]["is_smother_crop"].get<bool>()) continue;

            std::string plant_type = board[y][x]["plant_type"].get<std::string>();

            // Only count weeds eligible at this difficulty
            if (plants_config.contains(plant_type) && plants_config[plant_type].contains("min_difficulty")) {
                if (plants_config[plant_type]["min_difficulty"].get<int>() > difficulty) continue;
            }

            // Compute actions needed from hp / best tool damage
            int plant_hp = plants_config[plant_type].value("hp", 100);
            int best_damage = 1;
            if (plants_config[plant_type].contains("tools")) {
                for (auto tit = plants_config[plant_type]["tools"].begin(); tit != plants_config[plant_type]["tools"].end(); ++tit) {
                    int dmg = tit.value()["damage"].get<int>();
                    if (dmg > best_damage) best_damage = dmg;
                }
            }
            int actions_needed = static_cast<int>(std::ceil(static_cast<double>(plant_hp) / static_cast<double>(best_damage)));
            weed_type_counts[plant_type]++;
            total_weeds++;
        }
    }

    if (total_weeds == 0) return 1;

    // Tool switch costs: +1 per unique weed type per row
    int switch_costs = 0;
    for (int y = 0; y < grid_size; ++y) {
        std::set<std::string> row_types;
        for (int x = 0; x < grid_size; ++x) {
            if (board[y][x].contains("is_blocked") && board[y][x]["is_blocked"].get<bool>()) continue;
            if (board[y][x]["plant_type"].is_null()) continue;
            if (board[y][x].contains("is_smother_crop") && board[y][x]["is_smother_crop"].get<bool>()) continue;

            std::string plant_type = board[y][x]["plant_type"].get<std::string>();
            row_types.insert(plant_type);
        }
        switch_costs += static_cast<int>(row_types.size());
    }

    int base_actions = total_base_actions + switch_costs;

    // Weighted spread probability
    double weighted_spread = 0.0;
    for (const auto& [type, count] : weed_type_counts) {
        double prob = 0.0;
        if (plants_config.contains(type) && plants_config[type].contains("spread_probability")) {
            prob = plants_config[type]["spread_probability"].get<double>();
        }
        weighted_spread += prob * static_cast<double>(count);
    }
    weighted_spread /= static_cast<double>(total_weeds);

    // Compounding spread penalty: (1 + weighted_spread)^2
    double adjusted_actions = static_cast<double>(base_actions) * (1.0 + weighted_spread) * (1.0 + weighted_spread);

    int par = std::max(1, static_cast<int>(std::ceil(adjusted_actions / 2.0)));
    std::cerr << "[par] base_actions=" << base_actions
              << " spread=" << weighted_spread
              << " adjusted=" << adjusted_actions
              << " par=" << par
              << " total_weeds=" << total_weeds
              << std::endl;
    return par;
}

bool is_accessible(const nlohmann::json& board, int grid_size, int x, int y) {
    if (x < 0 || x >= grid_size || y < 0 || y >= grid_size) return false;
    return board[y][x].contains("is_accessible") && board[y][x]["is_accessible"].get<bool>();
}

static int get_damage_for_tool(const nlohmann::json& plants_config, const std::string& plant_type, const std::string& tool_id) {
    if (!plants_config.contains(plant_type)) return 999;
    const auto& plant = plants_config[plant_type];
    if (!plant.contains("tools") || !plant["tools"].contains(tool_id)) return 999;
    return plant["tools"][tool_id]["damage"].get<int>();
}

static bool tool_affects_adjacent(const nlohmann::json& plants_config, const std::string& plant_type, const std::string& tool_id) {
    if (!plants_config.contains(plant_type)) return false;
    const auto& plant = plants_config[plant_type];
    if (!plant.contains("tools") || !plant["tools"].contains(tool_id)) return false;
    return plant["tools"][tool_id].contains("affects_adjacent") && plant["tools"][tool_id]["affects_adjacent"].get<bool>();
}

static std::string adjacent_mode(const nlohmann::json& plants_config, const std::string& plant_type, const std::string& tool_id) {
    if (!plants_config.contains(plant_type)) return "";
    const auto& plant = plants_config[plant_type];
    if (!plant.contains("tools") || !plant["tools"].contains(tool_id)) return "";
    if (!plant["tools"][tool_id].contains("adjacent_mode")) return "";
    return plant["tools"][tool_id]["adjacent_mode"].get<std::string>();
}

WeedingActionResult process_action(
    GameConfigCache& config_cache,
    nlohmann::json& session_json,
    const WeedingActionRequest& action,
    std::mt19937& rng,
    bool track_changes)
{
    WeedingActionResult result;
    result.valid = false;

    const auto& plants = config_cache.getWeedingPlants();
    const auto& tools = config_cache.getWeedingTools();
    int grid_size = session_json["grid_size"].get<int>();
    auto& board = session_json["board"];

    // --- FORFEIT ---
    if (action.action_type == "forfeit") {
        result.valid = true;
        result.won = false;
        result.score = 0;
        result.message = "forfeit";
        return result;
    }

    // --- SWITCH TOOL ---
    if (action.action_type == "switch_tool") {
        if (action.tool_id.empty()) {
            result.error = "tool_id required";
            return result;
        }
        if (!tools.contains(action.tool_id)) {
            result.error = "Unknown tool: " + action.tool_id;
            return result;
        }

        std::string current_tool = session_json["equipped_tool"].get<std::string>();
        if (action.tool_id == current_tool) {
            // Same tool — no cost, no effect
            result.valid = true;
            result.round = session_json["round"].get<int>();
            result.actions_remaining = session_json["actions_remaining"].get<int>();
            result.equipped_tool = current_tool;
            result.par = session_json["par"].get<int>();
            result.won = false;
            return result;
        }

        bool was_switch = session_json["last_action_was_switch"].get<bool>();
        if (!was_switch) {
            int remaining = session_json["actions_remaining"].get<int>();
            if (remaining < 1) {
                result.error = "Not enough actions remaining";
                return result;
            }
            remaining -= 1;
            session_json["actions_remaining"] = remaining;

            if (remaining == 0) {
                auto& board = session_json["board"];
                int grid_size = session_json["grid_size"].get<int>();
                run_plant_spread(board, grid_size, plants, rng);
                session_json["round"] = session_json["round"].get<int>() + 1;
                session_json["actions_remaining"] = 2;
                compute_accessibility(board, grid_size);
                bool won = check_win(board, grid_size);
                session_json["won"] = won;
                result.won = won;
            }
        }

        session_json["equipped_tool"] = action.tool_id;
        session_json["last_action_was_switch"] = true;

        result.valid = true;
        result.round = session_json["round"].get<int>();
        result.actions_remaining = session_json["actions_remaining"].get<int>();
        result.equipped_tool = action.tool_id;
        result.par = session_json["par"].get<int>();
        result.won = session_json["won"].get<bool>();
        return result;
    }

    // --- USE TOOL ---
    if (action.action_type == "use_tool") {
        if (action.tool_id.empty()) {
            result.error = "tool_id required";
            return result;
        }
        if (action.target_x < 0 || action.target_y < 0) {
            result.error = "target_x and target_y required";
            return result;
        }

        int tx = action.target_x;
        int ty = action.target_y;
        nlohmann::json& board = session_json["board"];

        // Bounds check
        if (tx >= grid_size || ty >= grid_size) {
            result.error = "Target out of bounds";
            return result;
        }

        // Check blocked
        if (board[ty][tx].contains("is_blocked") && board[ty][tx]["is_blocked"].get<bool>()) {
            result.error = "That square is blocked";
            return result;
        }

        // Check it's a weed (not empty, not smother crop)
        if (board[ty][tx]["plant_type"].is_null()) {
            result.error = "No weed at that square";
            return result;
        }
        if (board[ty][tx].contains("is_smother_crop") && board[ty][tx]["is_smother_crop"].get<bool>()) {
            result.error = "That square already has a smother crop";
            return result;
        }

        // Check accessibility
        if (!board[ty][tx]["is_accessible"].get<bool>()) {
            result.error = "That square is not accessible";
            return result;
        }

        // Check tool applies to this weed
        std::string plant_type = board[ty][tx]["plant_type"].get<std::string>();
        int damage = get_damage_for_tool(plants, plant_type, action.tool_id);
        if (damage >= 999) {
            result.error = "This tool cannot clear " + plant_type;
            return result;
        }

        // Check we have enough actions remaining
        int cost = 1;
        int remaining = session_json["actions_remaining"].get<int>();
        if (cost > remaining) {
            result.error = "Not enough actions remaining";
            return result;
        }

        // Save old board state for diff
        nlohmann::json old_board;
        if (track_changes) {
            old_board = board;
        }

        // Apply the action
        int current_progress = board[ty][tx].contains("progress") ? board[ty][tx]["progress"].get<int>() : 0;
        current_progress += damage;
        board[ty][tx]["progress"] = current_progress;

        bool multi_row_column = false;
        std::vector<std::pair<int, int>> cleared_cells;

        if (current_progress >= 100) {
            // Weed is cleared! Check for adjacent clearing
            if (tool_affects_adjacent(plants, plant_type, action.tool_id) && adjacent_mode(plants, plant_type, action.tool_id) == "row_or_column") {
                multi_row_column = true;

                // Find contiguous same-type weeds in row and column
                int row_len = 1;
                int row_start = tx, row_end = tx;
                // Scan left in row
                for (int cx = tx - 1; cx >= 0; --cx) {
                    if (board[ty][cx].contains("is_blocked") && board[ty][cx]["is_blocked"].get<bool>()) break;
                    if (board[ty][cx]["plant_type"].is_null() || board[ty][cx]["plant_type"].get<std::string>() != plant_type) break;
                    if (board[ty][cx].contains("is_smother_crop") && board[ty][cx]["is_smother_crop"].get<bool>()) break;
                    row_len++;
                    row_start = cx;
                }
                // Scan right in row
                for (int cx = tx + 1; cx < grid_size; ++cx) {
                    if (board[ty][cx].contains("is_blocked") && board[ty][cx]["is_blocked"].get<bool>()) break;
                    if (board[ty][cx]["plant_type"].is_null() || board[ty][cx]["plant_type"].get<std::string>() != plant_type) break;
                    if (board[ty][cx].contains("is_smother_crop") && board[ty][cx]["is_smother_crop"].get<bool>()) break;
                    row_len++;
                    row_end = cx;
                }

                // Scan up in column
                int col_len = 1;
                int col_start = ty, col_end = ty;
                for (int cy = ty - 1; cy >= 0; --cy) {
                    if (board[cy][tx].contains("is_blocked") && board[cy][tx]["is_blocked"].get<bool>()) break;
                    if (board[cy][tx]["plant_type"].is_null() || board[cy][tx]["plant_type"].get<std::string>() != plant_type) break;
                    if (board[cy][tx].contains("is_smother_crop") && board[cy][tx]["is_smother_crop"].get<bool>()) break;
                    col_len++;
                    col_start = cy;
                }
                for (int cy = ty + 1; cy < grid_size; ++cy) {
                    if (board[cy][tx].contains("is_blocked") && board[cy][tx]["is_blocked"].get<bool>()) break;
                    if (board[cy][tx]["plant_type"].is_null() || board[cy][tx]["plant_type"].get<std::string>() != plant_type) break;
                    if (board[cy][tx].contains("is_smother_crop") && board[cy][tx]["is_smother_crop"].get<bool>()) break;
                    col_len++;
                    col_end = cy;
                }

                // Pick the longest line
                if (row_len >= col_len) {
                    for (int cx = row_start; cx <= row_end; ++cx) {
                        cleared_cells.push_back({cx, ty});
                    }
                } else {
                    for (int cy = col_start; cy <= col_end; ++cy) {
                        cleared_cells.push_back({tx, cy});
                    }
                }
            } else {
                cleared_cells.push_back({tx, ty});
            }

            // Clear all cells
            for (const auto& cell : cleared_cells) {
                board[cell.second][cell.first]["plant_type"] = nullptr;
                board[cell.second][cell.first]["progress"] = 0;
                board[cell.second][cell.first]["actions_needed"] = 0;
                board[cell.second][cell.first]["is_smother_crop"] = false;
            }
        }

        // Consume actions
        remaining -= cost;
        session_json["actions_remaining"] = remaining;
        session_json["last_action_was_switch"] = false;

        // If actions depleted, run plant spread
        if (remaining == 0) {
            run_plant_spread(board, grid_size, plants, rng);
            session_json["round"] = session_json["round"].get<int>() + 1;
            session_json["actions_remaining"] = 2;
        }

        // Recompute accessibility
        compute_accessibility(board, grid_size);

        // Check win
        bool won = check_win(board, grid_size);
        session_json["won"] = won;

        // Generate board changes
        if (track_changes) {
            result.board_changes = extract_board_changes(old_board, board, grid_size);
        }
        result.valid = true;
        result.round = session_json["round"].get<int>();
        result.actions_remaining = session_json["actions_remaining"].get<int>();
        result.equipped_tool = action.tool_id;
        result.par = session_json["par"].get<int>();
        result.won = won;

        if (won) {
            int par = session_json["par"].get<int>();
            int current_round = session_json["round"].get<int>();
            result.score = std::max(par - current_round + 10, 1);
            result.message = "All land cleared!";

            // Update session_json for won
            session_json["score"] = result.score;
        }

        return result;
    }

    // --- PLANT ---
    if (action.action_type == "plant") {
        if (action.tool_id.empty()) {
            result.error = "tool_id required";
            return result;
        }
        if (action.target_x < 0 || action.target_y < 0) {
            result.error = "target_x and target_y required";
            return result;
        }

        int tx = action.target_x;
        int ty = action.target_y;
        nlohmann::json& board = session_json["board"];

        // Bounds check
        if (tx >= grid_size || ty >= grid_size) {
            result.error = "Target out of bounds";
            return result;
        }

        // Check not blocked
        if (board[ty][tx].contains("is_blocked") && board[ty][tx]["is_blocked"].get<bool>()) {
            result.error = "Cannot plant on a blocked square";
            return result;
        }

        // Check empty
        if (!board[ty][tx]["plant_type"].is_null()) {
            result.error = "Square is not empty";
            return result;
        }

        // Check tool can plant
        if (!tools.contains(action.tool_id)) {
            result.error = "Unknown tool: " + action.tool_id;
            return result;
        }
        if (!tools[action.tool_id].contains("can_plant") || !tools[action.tool_id]["can_plant"].get<bool>()) {
            result.error = "This tool cannot plant";
            return result;
        }

        // Check accessibility
        if (!board[ty][tx]["is_accessible"].get<bool>()) {
            result.error = "That square is not accessible";
            return result;
        }

        // Get plant crop id — use client-provided crop_id if given, otherwise fall back to tool config
        std::string crop_id = action.crop_id.empty()
            ? tools[action.tool_id].value("plant_crop_id", "rye")
            : action.crop_id;
        if (!plants.contains(crop_id)) {
            result.error = "Unknown crop: " + crop_id;
            return result;
        }

        // Check actions
        int cost = 1;
        int remaining = session_json["actions_remaining"].get<int>();
        if (cost > remaining) {
            result.error = "Not enough actions remaining";
            return result;
        }

        // Save old board for diff
        nlohmann::json old_board;
        if (track_changes) {
            old_board = board;
        }

        // Plant the smother crop
        board[ty][tx]["plant_type"] = crop_id;
        board[ty][tx]["is_smother_crop"] = true;
        board[ty][tx]["progress"] = 0;
        board[ty][tx]["actions_needed"] = 0;

        // Consume actions
        remaining -= cost;
        session_json["actions_remaining"] = remaining;
        session_json["last_action_was_switch"] = false;

        // If actions depleted, run plant spread
        if (remaining == 0) {
            run_plant_spread(board, grid_size, plants, rng);
            session_json["round"] = session_json["round"].get<int>() + 1;
            session_json["actions_remaining"] = 2;
        }

        // Recompute accessibility
        compute_accessibility(board, grid_size);

        // Check win
        bool won = check_win(board, grid_size);
        session_json["won"] = won;

        // Board changes
        if (track_changes) {
            result.board_changes = extract_board_changes(old_board, board, grid_size);
        }
        result.valid = true;
        result.round = session_json["round"].get<int>();
        result.actions_remaining = session_json["actions_remaining"].get<int>();
        result.equipped_tool = action.tool_id;
        result.par = session_json["par"].get<int>();
        result.won = won;

        if (won) {
            int par = session_json["par"].get<int>();
            int current_round = session_json["round"].get<int>();
            result.score = std::max(par - current_round + 10, 1);
            result.message = "All land cleared!";
            session_json["score"] = result.score;
        }

        return result;
    }

    result.error = "Unknown action type: " + action.action_type;
    return result;
}

void run_plant_spread(nlohmann::json& board, int grid_size, const nlohmann::json& plants_config, std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    const int dx[] = {0, 1, 0, -1};
    const int dy[] = {-1, 0, 1, 0};

    // Collect new weeds atomically to prevent cascading: a new weed at (0,1) should NOT
    // be visible to cell (0,2) within the same turn's spread run.
    std::map<std::pair<int,int>, std::string> new_weeds;

    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            if (board[y][x].contains("is_blocked") && board[y][x]["is_blocked"].get<bool>()) continue;
            if (!board[y][x]["plant_type"].is_null()) continue;
            if (board[y][x].contains("is_smother_crop") && board[y][x]["is_smother_crop"].get<bool>()) continue;

            std::unordered_map<std::string, double> type_prob;
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx[d];
                int ny = y + dy[d];
                if (nx < 0 || nx >= grid_size || ny < 0 || ny >= grid_size) continue;
                if (board[ny][nx].contains("is_blocked") && board[ny][nx]["is_blocked"].get<bool>()) continue;
                if (board[ny][nx]["plant_type"].is_null()) continue;
                if (board[ny][nx].contains("is_smother_crop") && board[ny][nx]["is_smother_crop"].get<bool>()) continue;

                std::string weed_type = board[ny][nx]["plant_type"].get<std::string>();
                if (plants_config.contains(weed_type) && plants_config[weed_type].contains("spread_probability")) {
                    double prob = plants_config[weed_type]["spread_probability"].get<double>();
                    type_prob[weed_type] += prob;
                }
            }

            if (type_prob.empty()) continue;

            std::string best_type;
            double best_prob = 0;
            for (const auto& [type, prob] : type_prob) {
                if (prob > best_prob) {
                    best_prob = prob;
                    best_type = type;
                }
            }

            if (dist(rng) < best_prob) {
                new_weeds[{x, y}] = best_type;
            }
        }
    }

    // Apply all new weeds atomically
    for (const auto& [pos, best_type] : new_weeds) {
        int x = pos.first, y = pos.second;
        board[y][x]["plant_type"] = best_type;
        board[y][x]["is_smother_crop"] = false;
        board[y][x]["actions_needed"] = plants_config[best_type].value("hp", 100);
        board[y][x]["progress"] = 0;
    }
}

bool check_win(const nlohmann::json& board, int grid_size) {
    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            if (board[y][x].contains("is_blocked") && board[y][x]["is_blocked"].get<bool>()) continue;
            bool is_empty = board[y][x]["plant_type"].is_null();
            bool is_smother = board[y][x].contains("is_smother_crop") && board[y][x]["is_smother_crop"].get<bool>();
            // If it's not empty and not a smother crop, a weed remains
            if (!is_empty && !is_smother) return false;
        }
    }
    return true;
}

std::vector<nlohmann::json> extract_board_changes(const nlohmann::json& old_board, const nlohmann::json& new_board, int grid_size) {
    std::vector<nlohmann::json> changes;
    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            if (old_board[y][x] != new_board[y][x]) {
                nlohmann::json change = new_board[y][x];
                change["x"] = x;
                change["y"] = y;
                changes.push_back(change);
            }
        }
    }
    return changes;
}

} // namespace weeding_logic
