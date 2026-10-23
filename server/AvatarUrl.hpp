#pragma once

#include <cstdint>
#include <string>

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

// True when the account that owns `character_id` is flagged adult (18+).
bool account_is_adult(int64_t character_id);

// Resolves the portrait URL a viewer should see for `character_id`. Returns
// "/api/avatar/<id>" (the age-gated custom stream) only when the owner is an
// adult, the viewer is an adult, and a custom upload exists; otherwise the
// public safe-pool URL; otherwise "" when the character has neither.
std::string resolve_avatar_url(int64_t character_id, bool viewer_is_adult);
