#include "registration/InvitationHandlers.hpp"

#include <optional>
#include <string>

#include "Database.hpp"
#include "GameConfigCache.hpp"
#include "staff/StaffActionEngine.hpp"
#include "staff/StaffAuth.hpp"
#include "staff/StaffStore.hpp"
#include "registration/InvitationService.hpp"
#include "registration/InvitationStore.hpp"
#include "registration/RegistrationRateLimiter.hpp"

namespace {

using json = nlohmann::json;

ApiResponse with_token(ApiResponse response, const std::optional<std::string>& new_token) {
    if (new_token) response.data["token"] = *new_token;
    return response;
}

std::int64_t account_id_for(const std::optional<std::string>& username) {
    if (!username) return 0;
    return staff::account_id_for_username(*username).value_or(0);
}

json invitation_to_json(const invitation_code_record& record, bool staff_view) {
    json out = {
        {"id", record.id},
        {"code", display_invitation_code(record.code)},
        {"created_by_kind", record.created_by_kind},
        {"created_at", record.created_at},
        {"used", record.used_at > 0},
        {"used_at", record.used_at},
        {"revoked", record.revoked_at > 0},
        {"revoked_at", record.revoked_at},
    };
    if (staff_view) {
        out["created_by_account_id"] = record.created_by_account_id;
        out["created_by_username"] =
            staff::username_for_account(record.created_by_account_id).value_or("");
        out["used_by_account_id"] = record.used_by_account_id;
        out["used_by_username"] = record.used_by_account_id > 0
                                      ? staff::username_for_account(record.used_by_account_id)
                                            .value_or("")
                                      : std::string();
        out["revoked_by_account_id"] = record.revoked_by_account_id;
    }
    return out;
}

staff::staff_context resolve_staff(const std::optional<std::string>& username,
                                   ApiResponse& response) {
    if (!username) {
        response.needs_auth = true;
        return staff::staff_context{};
    }
    return staff::context_for_username(username);
}

}  // namespace

ApiResponse handleGetRegistrationConfig(GameConfigCache& config_cache,
                                        const nlohmann::json& body,
                                        const std::optional<std::string>& username,
                                        const ClientInfo& client,
                                        const std::optional<std::string>& new_token) {
    (void)body;
    (void)username;
    (void)client;
    (void)new_token;
    ApiResponse response;
    const registration_settings settings =
        parse_registration_settings(config_cache.getRegistrationConfig());
    response.data["mode"] = registration_mode_name(settings.mode);
    response.data["invite_required"] = registration_code_required(settings);
    response.data["players_can_generate"] =
        settings.mode == registration_mode::invite_only &&
        settings.players_can_generate && settings.player_daily_limit > 0 &&
        settings.player_lifetime_limit > 0;
    return response;
}

namespace {

ApiResponse handle_get_invitation_status(GameConfigCache& config_cache,
                                         const nlohmann::json& body,
                                         const std::optional<std::string>& username,
                                         const ClientInfo& client,
                                         const std::optional<std::string>& new_token) {
    (void)body;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const std::int64_t account_id = account_id_for(username);
    if (account_id <= 0) {
        response.error = "account_not_found";
        return response;
    }

    const registration_settings settings =
        parse_registration_settings(config_cache.getRegistrationConfig());
    const invitation_status status = get_invitation_status(account_id, settings);
    response.data["mode"] = registration_mode_name(status.mode);
    response.data["invite_required"] = status.invite_required;
    response.data["players_can_generate"] = status.players_can_generate;
    response.data["enabled"] = status.allowance.enabled;
    response.data["can_generate"] = status.allowance.can_generate;
    response.data["daily_limit"] = status.allowance.daily_limit;
    response.data["lifetime_limit"] = status.allowance.lifetime_limit;
    response.data["generated_today"] = status.allowance.generated_today;
    response.data["generated_total"] = status.allowance.generated_total;
    response.data["daily_remaining"] = status.allowance.daily_remaining;
    response.data["lifetime_remaining"] = status.allowance.lifetime_remaining;
    json codes = json::array();
    for (const auto& record : status.codes) {
        codes.push_back(invitation_to_json(record, false));
    }
    response.data["codes"] = std::move(codes);
    return with_token(response, new_token);
}

ApiResponse handle_create_invitation_code(GameConfigCache& config_cache,
                                          const nlohmann::json& body,
                                          const std::optional<std::string>& username,
                                          const ClientInfo& client,
                                          const std::optional<std::string>& new_token) {
    (void)body;
    (void)client;
    ApiResponse response;
    if (!username) {
        response.needs_auth = true;
        return response;
    }
    const std::int64_t account_id = account_id_for(username);
    if (account_id <= 0) {
        response.error = "account_not_found";
        return response;
    }

    const registration_settings settings =
        parse_registration_settings(config_cache.getRegistrationConfig());
    std::string error;
    const auto record = generate_for_player(account_id, settings, error);
    if (!record) {
        response.error = error.empty() ? "invitation_code_generation_failed" : error;
        return response;
    }
    response.data["invitation"] = invitation_to_json(*record, false);
    response.data["code"] = display_invitation_code(record->code);
    return with_token(response, new_token);
}

ApiResponse handle_staff_list_invitation_codes(
    GameConfigCache& config_cache, const nlohmann::json& body,
    const std::optional<std::string>& username, const ClientInfo& client,
    const std::optional<std::string>& new_token) {
    (void)client;
    ApiResponse response;
    const staff::staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "registration.invites.view", response)) {
        return response;
    }

    const registration_settings settings =
        parse_registration_settings(config_cache.getRegistrationConfig());
    response.data["mode"] = registration_mode_name(settings.mode);
    response.data["invitations_enabled"] = settings.mode != registration_mode::off;
    json codes = json::array();

    if (settings.mode != registration_mode::off) {
        invitation_code_filter filter;
        filter.status = body.value("status", std::string("all"));
        filter.created_by_kind = body.value("created_by_kind", std::string());
        filter.query = body.value("query", std::string());
        filter.limit = body.value("limit", 100);
        filter.offset = body.value("offset", 0);
        for (const auto& record : InvitationStore::instance().list_all(filter)) {
            codes.push_back(invitation_to_json(record, true));
        }
        staff::record_read_event(
            context, "read.invitation_codes", "invitation_codes", "list", "staff",
            {{"status", filter.status},
             {"created_by_kind", filter.created_by_kind},
             {"query_present", !filter.query.empty()}});
    }
    response.data["codes"] = std::move(codes);
    return with_token(response, new_token);
}

ApiResponse handle_staff_create_invitation_code(
    GameConfigCache& config_cache, const nlohmann::json& body,
    const std::optional<std::string>& username, const ClientInfo& client,
    const std::optional<std::string>& new_token) {
    (void)client;
    ApiResponse response;
    const staff::staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "registration.invites.generate", response)) {
        return response;
    }

    const registration_settings settings =
        parse_registration_settings(config_cache.getRegistrationConfig());
    if (settings.mode != registration_mode::invite_only) {
        response.error = "invite_generation_disabled";
        return response;
    }

    // The code is generated up front so the journal target id is known before
    // the action is appended. The secret itself is never written to staff.db.
    const invitation_code_draft draft = InvitationStore::instance().make_code_draft();
    staff::staff_action_request request;
    request.action_type = "registration.invite_generate";
    request.target_type = "invitation_code";
    request.target_id = draft.id;
    request.reason = body.value("reason", std::string());
    request.parameters = {{"code", draft.code}};

    const staff::staff_action_result result =
        staff::StaffActionEngine::instance().execute(context, request);
    if (!result.ok) {
        response.error = result.error;
        return response;
    }

    response.data["action_id"] = result.action_id;
    response.data["code"] = display_invitation_code(draft.code);
    if (auto record = InvitationStore::instance().find_by_id(draft.id)) {
        response.data["invitation"] = invitation_to_json(*record, true);
    } else {
        response.data["invitation"] = {{"id", draft.id},
                                        {"code", display_invitation_code(draft.code)}};
    }
    return with_token(response, new_token);
}

ApiResponse handle_staff_revoke_invitation_code(
    GameConfigCache& config_cache, const nlohmann::json& body,
    const std::optional<std::string>& username, const ClientInfo& client,
    const std::optional<std::string>& new_token) {
    (void)client;
    ApiResponse response;
    const staff::staff_context context = resolve_staff(username, response);
    if (response.needs_auth) return response;
    if (!staff::require_capability(context, "registration.invites.revoke", response)) {
        return response;
    }

    const registration_settings settings =
        parse_registration_settings(config_cache.getRegistrationConfig());
    if (settings.mode == registration_mode::off) {
        response.error = "invites_disabled";
        return response;
    }

    const std::string code_id = body.value("code_id", std::string());
    if (code_id.empty()) {
        response.error = "code_id required";
        return response;
    }
    const auto existing = InvitationStore::instance().find_by_id(code_id);
    if (!existing) {
        response.error = "invitation_code_not_found";
        return response;
    }

    staff::staff_action_request request;
    request.action_type = "registration.invite_revoke";
    request.target_type = "invitation_code";
    request.target_id = code_id;
    request.reason = body.value("reason", std::string());
    request.parameters = {{"code_id", code_id}};

    const staff::staff_action_result result =
        staff::StaffActionEngine::instance().execute(context, request);
    if (!result.ok) {
        response.error = result.error;
        return response;
    }

    response.data["action_id"] = result.action_id;
    if (auto record = InvitationStore::instance().find_by_id(code_id)) {
        response.data["invitation"] = invitation_to_json(*record, true);
    }
    return with_token(response, new_token);
}

}  // namespace

void register_invitation_handlers(std::unordered_map<std::string, ApiHandler>& handlers) {
    auto reg = [&](const std::string& name, auto fn) {
        handlers[name] = [fn](GameConfigCache& c, const nlohmann::json& b,
                              const std::optional<std::string>& u, const ClientInfo& cl,
                              const std::optional<std::string>& t) {
            return fn(c, b, u, cl, t);
        };
    };
    reg("getInvitationStatus", handle_get_invitation_status);
    reg("createInvitationCode", handle_create_invitation_code);
    reg("staffListInvitationCodes", handle_staff_list_invitation_codes);
    reg("staffCreateInvitationCode", handle_staff_create_invitation_code);
    reg("staffRevokeInvitationCode", handle_staff_revoke_invitation_code);
}
