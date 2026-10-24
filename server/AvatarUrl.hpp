#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Player portrait (PFP) URL resolution — the single source of truth for the
// layered identity model:
//
//   avatar_safe_id   always present once chosen; shown to minors and used as
//                    the fallback for every viewer.
//   avatar_custom_id optional 18+ upload layered ON TOP for adult viewers.
//
// A custom avatar is therefore only visible when BOTH the owner and the viewer
// are adults; every other viewer gets the safe pick. This mirrors the
// safe-name / custom-name model. See docs/retinue_design.md §13.

// Process-wide public-asset configuration, set once at startup. Empty values
// keep the age-gated /api/avatar/<id> fallback for local development.
struct avatar_url_config {
    std::string public_base_url;
    std::string r2_prefix = "avatars/";
};

void configure_avatar_urls(const avatar_url_config& config);

// Builds the public R2 URL for one stored avatar key. Returns "" for local
// storage or when no public base is configured.
std::string build_avatar_public_url(std::string_view storage, std::string_view key);

// True when the account that owns `character_id` is flagged adult (18+).
bool account_is_adult(int64_t character_id);

// Resolves the portrait URL a viewer should see for `character_id`. Returns the
// public content-addressed R2 URL (or "/api/avatar/<id>" for local storage) only
// when the owner is an adult, the viewer is an adult, and a custom upload
// exists; otherwise the public safe-pool URL; otherwise "" when the character
// has neither.
std::string resolve_avatar_url(int64_t character_id, bool viewer_is_adult);
