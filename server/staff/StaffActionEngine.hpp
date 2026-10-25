#pragma once
// Staff action execution + the shared audit journal.
//
// Mutating staff actions and every scoped chat moderation action pass through
// this engine. It snapshots before-state, appends a pending journal row, applies
// the effect, and finalizes the row. Undo is a new compensating journal row and
// is preflighted all-or-nothing.

#include <cstdint>
#include <string>

#include "staff/StaffTypes.hpp"

class GameConfigCache;

namespace staff {

class StaffActionEngine {
public:
    static StaffActionEngine& instance();

    void initialize(GameConfigCache* config_cache);

    staff_action_result execute(const staff_context& context,
                                const staff_action_request& request);
    staff_preflight_result preflight_undo(const std::string& action_id);
    staff_action_result undo(const staff_context& context, const std::string& action_id,
                             const std::string& reason);
    void recover_interrupted_actions();

private:
    StaffActionEngine() = default;
};

// Shared journal helpers used by chat moderation (scoped and staff-wide).
std::string record_scoped_chat_action(int64_t actor_character_id, int64_t actor_account_id,
                                      const std::string& action_type,
                                      const std::string& chat_id,
                                      int64_t target_character_id,
                                      const json& before_state, const json& after_state,
                                      const json& undo_parameters,
                                      const std::string& reason);
std::string record_read_event(const staff_context& context, const std::string& action_type,
                              const std::string& target_type, const std::string& target_id,
                              const std::string& scope, const json& detail = json::object());

// System-generated notifications (never staff-authored free text).
void notify_account(int64_t account_id, const std::string& kind, const std::string& text_id,
                    const json& text_args, const std::string& route = std::string(),
                    const std::string& action_id = std::string(),
                    int64_t character_id = 0);
void notify_character(int64_t character_id, const std::string& kind, const std::string& text_id,
                      const json& text_args, const std::string& route = std::string(),
                      const std::string& action_id = std::string());
void notify_support_reply(const std::string& ticket_id, int64_t account_id,
                          const std::string& subject, bool staff_created,
                          const std::string& action_id);

}  // namespace staff
