#include "registration/InvitationService.hpp"

#include <openssl/crypto.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>

#include "Database.hpp"

namespace {

std::int64_t now_seconds_utc() {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::int64_t utc_day_start(std::int64_t now) {
    constexpr std::int64_t kSecondsPerDay = 24 * 60 * 60;
    return (now / kSecondsPerDay) * kSecondsPerDay;
}

std::string trim_copy(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

bool constant_time_equals_ignore_case(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const unsigned char ca = static_cast<unsigned char>(std::tolower(
            static_cast<unsigned char>(a[i])));
        const unsigned char cb = static_cast<unsigned char>(std::tolower(
            static_cast<unsigned char>(b[i])));
        diff = static_cast<unsigned char>(diff | (ca ^ cb));
    }
    return CRYPTO_memcmp(&diff, "\0", 1) == 0;
}

bool usable(const invitation_code_record& record) {
    if (record.used_at > 0 || record.revoked_at > 0) return false;
    return !InvitationStore::instance().is_redeemed_in_game(record.id);
}

}  // namespace

bool registration_code_required(const registration_settings& settings) {
    return settings.mode != registration_mode::off;
}

registration_code_resolution resolve_registration_code(
    const registration_settings& settings, const std::string& submitted_code) {
    registration_code_resolution result;
    if (settings.mode == registration_mode::off) {
        result.ok = true;
        result.kind = registration_code_kind::none;
        return result;
    }

    const std::string submitted = trim_copy(submitted_code);
    if (submitted.empty()) {
        result.error = "invitation_code_required";
        return result;
    }

    if (settings.mode == registration_mode::global_code) {
        const std::string global = trim_copy(settings.global_code);
        if (!global.empty() && constant_time_equals_ignore_case(submitted, global)) {
            result.ok = true;
            result.kind = registration_code_kind::global_code;
            return result;
        }
        // global_code mode also accepts still-valid individual codes.
    }

    const std::string canonical = canonicalize_invitation_code(submitted);
    if (canonical.empty()) {
        result.error = "invalid_invitation_code";
        return result;
    }

    const auto record = InvitationStore::instance().find_by_code(canonical);
    if (!record || !usable(*record)) {
        result.error = "invalid_invitation_code";
        return result;
    }

    result.ok = true;
    result.kind = registration_code_kind::individual_code;
    result.invitation_code_id = record->id;
    return result;
}

invitation_allowance get_invitation_allowance(std::int64_t account_id,
                                              const registration_settings& settings) {
    invitation_allowance allowance;
    allowance.daily_limit = settings.player_daily_limit;
    allowance.lifetime_limit = settings.player_lifetime_limit;

    if (settings.mode != registration_mode::invite_only ||
        !settings.players_can_generate || settings.player_daily_limit <= 0 ||
        settings.player_lifetime_limit <= 0 || account_id <= 0) {
        return allowance;
    }

    allowance.enabled = true;
    const std::int64_t now = now_seconds_utc();
    InvitationStore::instance().count_created_for_account(
        account_id, utc_day_start(now), allowance.generated_today,
        allowance.generated_total);

    allowance.daily_remaining = std::max<std::int64_t>(
        0, static_cast<std::int64_t>(allowance.daily_limit) - allowance.generated_today);
    allowance.lifetime_remaining = std::max<std::int64_t>(
        0,
        static_cast<std::int64_t>(allowance.lifetime_limit) - allowance.generated_total);
    allowance.can_generate = allowance.daily_limit > 0 &&
                             allowance.lifetime_limit > 0 &&
                             allowance.daily_remaining > 0 &&
                             allowance.lifetime_remaining > 0;
    return allowance;
}

invitation_status get_invitation_status(std::int64_t account_id,
                                        const registration_settings& settings) {
    invitation_status status;
    status.mode = settings.mode;
    status.invite_required = registration_code_required(settings);
    status.players_can_generate = settings.mode == registration_mode::invite_only &&
                                  settings.players_can_generate &&
                                  settings.player_daily_limit > 0 &&
                                  settings.player_lifetime_limit > 0;
    status.allowance = get_invitation_allowance(account_id, settings);

    if (settings.mode == registration_mode::invite_only && account_id > 0) {
        status.codes = InvitationStore::instance().list_for_account(account_id);
    }
    return status;
}

std::optional<invitation_code_record> generate_for_player(
    std::int64_t account_id, const registration_settings& settings, std::string& error) {
    if (settings.mode != registration_mode::invite_only ||
        !settings.players_can_generate) {
        error = "invite_generation_disabled";
        return std::nullopt;
    }
    if (settings.player_daily_limit <= 0 || settings.player_lifetime_limit <= 0) {
        error = "invite_generation_disabled";
        return std::nullopt;
    }
    if (account_id <= 0) {
        error = "account_not_found";
        return std::nullopt;
    }

    auto& db = Database::getInstance().invitesDB();
    auto& store = InvitationStore::instance();
    store.ensure_ready();

    const std::int64_t now = now_seconds_utc();
    const std::int64_t day_start = utc_day_start(now);

    try {
        db << "BEGIN IMMEDIATE;";
        std::int64_t today = 0;
        std::int64_t total = 0;
        store.count_created_for_account(account_id, day_start, today, total);
        if (today >= settings.player_daily_limit) {
            db << "ROLLBACK;";
            error = "invite_daily_limit_reached";
            return std::nullopt;
        }
        if (total >= settings.player_lifetime_limit) {
            db << "ROLLBACK;";
            error = "invite_lifetime_limit_reached";
            return std::nullopt;
        }

        for (int attempt = 0; attempt < 10; ++attempt) {
            const invitation_code_draft draft = store.make_code_draft();
            if (!store.insert_code(draft, account_id, "player", now, error)) {
                if (error.find("UNIQUE") != std::string::npos) {
                    continue;
                }
                db << "ROLLBACK;";
                return std::nullopt;
            }
            db << "COMMIT;";
            return store.find_by_id(draft.id);
        }
        db << "ROLLBACK;";
        error = "invitation_code_generation_failed";
        return std::nullopt;
    } catch (const std::exception& e) {
        try {
            db << "ROLLBACK;";
        } catch (const std::exception&) {
        }
        error = e.what();
        return std::nullopt;
    }
}
