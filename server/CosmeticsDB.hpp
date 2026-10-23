#pragma once
#include <nlohmann/json.hpp>
#include <sqlite_modern_cpp.h>
#include <cstdint>
#include <string>
#include <vector>

// Persistence for the premium-variety surface (rts_design.md §15): the
// character-bound Platinum ledger, idempotent purchase records, cosmetic
// ownership, and the equipped loadout.
//
// Platinum here is deliberately isolated from the manor economy: nothing in
// this module reads or writes `gold`/`silver_pence`, and no economy code path
// reads Platinum. That separation is the non-fungibility guarantee.
namespace cosmetics_db {

// ── Catalog helpers (config-driven; no DB) ──────────────────────────────────
// Look up a cosmetic item by id in the cosmetics.json catalog. Returns null if
// absent. Only appearance/meta keys are read — never a gameplay key.
nlohmann::json find_item(const nlohmann::json& catalog, const std::string& item_id);
nlohmann::json find_bundle(const nlohmann::json& catalog, const std::string& bundle_id);
nlohmann::json find_product(const nlohmann::json& catalog, const std::string& product_id);

// ── Platinum ledger ─────────────────────────────────────────────────────────
int64_t get_platinum(sqlite::database& db, int64_t character_id);
void add_platinum(sqlite::database& db, int64_t character_id, int64_t delta);

// ── Idempotent purchase records ─────────────────────────────────────────────
bool transaction_exists(sqlite::database& db, const std::string& idempotency_key);
int64_t record_transaction(sqlite::database& db, int64_t character_id,
                           const std::string& product_id, int64_t platinum,
                           const std::string& status, const std::string& idempotency_key,
                           int64_t now);
// Marks a pending transaction settled; returns its character id + platinum grant
// (or {0,0} if missing / already settled / not owned by `expected_character_id`).
// Idempotent.
std::pair<int64_t, int64_t> settle_transaction(sqlite::database& db,
                                               const std::string& idempotency_key,
                                               int64_t expected_character_id, int64_t now);

// ── Cosmetic ownership ──────────────────────────────────────────────────────
bool owns_cosmetic(sqlite::database& db, int64_t character_id, const std::string& cosmetic_id);
// Grants ownership (idempotent). Returns true if newly granted.
bool grant_cosmetic(sqlite::database& db, int64_t character_id, const std::string& cosmetic_id,
                    const std::string& source, int64_t now);
std::vector<std::string> owned_cosmetics(sqlite::database& db, int64_t character_id);
void revoke_cosmetic(sqlite::database& db, int64_t character_id, const std::string& cosmetic_id);

// Refunds (revokes) a cosmetic: sets the revoke flag and clears any loadout
// slot currently using it (rts_design.md §15.4/§15.8). Returns true if the
// character currently owned it. No Platinum is credited (refunds revoke only).
bool refund_cosmetic(sqlite::database& db, int64_t character_id, const std::string& cosmetic_id);

// ── Equipped loadout (slot -> cosmetic_id JSON) ─────────────────────────────
nlohmann::json get_loadout(sqlite::database& db, int64_t character_id);
void set_loadout(sqlite::database& db, int64_t character_id, const nlohmann::json& loadout);

// ── Composite purchase (idempotent + atomic-ish) ────────────────────────────
// Outcome strings: "ok" | "already_owned" | "duplicate" | "insufficient".
// On "ok" the Platinum is debited, ownership granted, and a settled transaction
// recorded under `idempotency_key`.
std::string purchase_cosmetic(sqlite::database& db, int64_t character_id,
                              const std::string& cosmetic_id, int64_t price,
                              const std::string& idempotency_key, int64_t now);

// Bundle purchase: one debit for `price`, grants every `item_ids` entry, one
// settled transaction under `idempotency_key`. Same outcome strings as above.
std::string purchase_bundle(sqlite::database& db, int64_t character_id,
                            const std::string& bundle_id,
                            const std::vector<std::string>& item_ids, int64_t price,
                            const std::string& idempotency_key, int64_t now);

} // namespace cosmetics_db
