#include "CosmeticsDB.hpp"
#include <algorithm>

namespace cosmetics_db {

nlohmann::json find_item(const nlohmann::json& catalog, const std::string& item_id) {
    if (!catalog.is_object() || !catalog.contains("items") || !catalog["items"].is_array()) {
        return nlohmann::json();
    }
    for (const auto& item : catalog["items"]) {
        if (item.is_object() && item.value("id", "") == item_id) return item;
    }
    return nlohmann::json();
}

nlohmann::json find_bundle(const nlohmann::json& catalog, const std::string& bundle_id) {
    if (!catalog.is_object() || !catalog.contains("bundles") || !catalog["bundles"].is_array()) {
        return nlohmann::json();
    }
    for (const auto& bundle : catalog["bundles"]) {
        if (bundle.is_object() && bundle.value("id", "") == bundle_id) return bundle;
    }
    return nlohmann::json();
}

nlohmann::json find_product(const nlohmann::json& catalog, const std::string& product_id) {
    if (!catalog.is_object() || !catalog.contains("products") || !catalog["products"].is_array()) {
        return nlohmann::json();
    }
    for (const auto& product : catalog["products"]) {
        if (product.is_object() && product.value("id", "") == product_id) return product;
    }
    return nlohmann::json();
}

int64_t get_platinum(sqlite::database& db, int64_t character_id) {
    int64_t balance = 0;
    bool found = false;
    db << "SELECT balance FROM platinum_ledger WHERE character_id = ?;"
       << character_id >> [&](int64_t b) { balance = b; found = true; };
    return found ? balance : 0;
}

void add_platinum(sqlite::database& db, int64_t character_id, int64_t delta) {
    db << "INSERT INTO platinum_ledger (character_id, balance) VALUES (?, ?) "
          "ON CONFLICT(character_id) DO UPDATE SET balance = balance + excluded.balance;"
       << character_id << delta;
}

bool transaction_exists(sqlite::database& db, const std::string& idempotency_key) {
    bool found = false;
    db << "SELECT 1 FROM platinum_transactions WHERE idempotency_key = ? LIMIT 1;"
       << idempotency_key >> [&](int) { found = true; };
    return found;
}

int64_t record_transaction(sqlite::database& db, int64_t character_id,
                           const std::string& product_id, int64_t platinum,
                           const std::string& status, const std::string& idempotency_key,
                           int64_t now) {
    db << "INSERT OR IGNORE INTO platinum_transactions "
          "(character_id, product_id, platinum, status, idempotency_key, created_at) "
          "VALUES (?, ?, ?, ?, ?, ?);"
       << character_id << product_id << platinum << status << idempotency_key << now;
    return db.last_insert_rowid();
}

std::pair<int64_t, int64_t> settle_transaction(sqlite::database& db,
                                               const std::string& idempotency_key,
                                               int64_t expected_character_id, int64_t now) {
    int64_t character_id = 0;
    int64_t platinum = 0;
    bool found = false;
    db << "SELECT character_id, platinum FROM platinum_transactions "
          "WHERE idempotency_key = ? AND status = 'pending';"
       << idempotency_key
       >> [&](int64_t cid, int64_t plat) { character_id = cid; platinum = plat; found = true; };
    if (!found || character_id != expected_character_id) return {0, 0};
    db << "UPDATE platinum_transactions SET status = 'settled', created_at = ? "
          "WHERE idempotency_key = ?;" << now << idempotency_key;
    if (platinum > 0) add_platinum(db, character_id, platinum);
    return {character_id, platinum};
}

bool owns_cosmetic(sqlite::database& db, int64_t character_id, const std::string& cosmetic_id) {
    bool owned = false;
    db << "SELECT 1 FROM character_cosmetics "
          "WHERE character_id = ? AND cosmetic_id = ? AND revoked = 0 LIMIT 1;"
       << character_id << cosmetic_id >> [&](int) { owned = true; };
    return owned;
}

bool grant_cosmetic(sqlite::database& db, int64_t character_id, const std::string& cosmetic_id,
                    const std::string& source, int64_t now) {
    int64_t existing_id = 0;
    int revoked = 0;
    bool found = false;
    db << "SELECT id, revoked FROM character_cosmetics "
          "WHERE character_id = ? AND cosmetic_id = ? LIMIT 1;"
       << character_id << cosmetic_id
       >> [&](int64_t id, int r) { existing_id = id; revoked = r; found = true; };
    if (!found) {
        db << "INSERT INTO character_cosmetics "
              "(character_id, cosmetic_id, source, acquired_at, revoked) VALUES (?, ?, ?, ?, 0);"
           << character_id << cosmetic_id << source << now;
        return true;
    }
    if (revoked != 0) {
        db << "UPDATE character_cosmetics SET revoked = 0, source = ?, acquired_at = ? WHERE id = ?;"
           << source << now << existing_id;
        return true;
    }
    return false;   // already owned
}

std::vector<std::string> owned_cosmetics(sqlite::database& db, int64_t character_id) {
    std::vector<std::string> owned;
    db << "SELECT cosmetic_id FROM character_cosmetics "
          "WHERE character_id = ? AND revoked = 0 ORDER BY acquired_at, id;"
       << character_id >> [&](std::string cid) { owned.push_back(std::move(cid)); };
    return owned;
}

void revoke_cosmetic(sqlite::database& db, int64_t character_id, const std::string& cosmetic_id) {
    db << "UPDATE character_cosmetics SET revoked = 1 WHERE character_id = ? AND cosmetic_id = ?;"
       << character_id << cosmetic_id;
}

bool refund_cosmetic(sqlite::database& db, int64_t character_id, const std::string& cosmetic_id) {
    if (!owns_cosmetic(db, character_id, cosmetic_id)) return false;
    revoke_cosmetic(db, character_id, cosmetic_id);
    // Clear any loadout slot still pointing at the revoked item.
    nlohmann::json loadout = get_loadout(db, character_id);
    bool changed = false;
    if (loadout.is_object()) {
        for (auto it = loadout.begin(); it != loadout.end();) {
            if (it.value().is_string() && it.value().get<std::string>() == cosmetic_id) {
                it = loadout.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    if (changed) set_loadout(db, character_id, loadout);
    return true;
}

nlohmann::json get_loadout(sqlite::database& db, int64_t character_id) {
    std::string raw;
    bool found = false;
    db << "SELECT loadout FROM character_loadout WHERE character_id = ?;"
       << character_id >> [&](std::string l) { raw = std::move(l); found = true; };
    if (!found || raw.empty()) return nlohmann::json::object();
    try {
        nlohmann::json parsed = nlohmann::json::parse(raw);
        return parsed.is_object() ? parsed : nlohmann::json::object();
    } catch (const std::exception&) {
        return nlohmann::json::object();
    }
}

void set_loadout(sqlite::database& db, int64_t character_id, const nlohmann::json& loadout) {
    db << "INSERT OR REPLACE INTO character_loadout (character_id, loadout) VALUES (?, ?);"
       << character_id << loadout.dump();
}

std::string purchase_cosmetic(sqlite::database& db, int64_t character_id,
                              const std::string& cosmetic_id, int64_t price,
                              const std::string& idempotency_key, int64_t now) {
    if (transaction_exists(db, idempotency_key)) return "duplicate";
    if (owns_cosmetic(db, character_id, cosmetic_id)) return "already_owned";
    if (get_platinum(db, character_id) < price) return "insufficient";
    if (price > 0) add_platinum(db, character_id, -price);
    grant_cosmetic(db, character_id, cosmetic_id, "store", now);
    record_transaction(db, character_id, cosmetic_id, -price, "settled", idempotency_key, now);
    return "ok";
}

std::string purchase_bundle(sqlite::database& db, int64_t character_id,
                            const std::string& bundle_id,
                            const std::vector<std::string>& item_ids, int64_t price,
                            const std::string& idempotency_key, int64_t now) {
    if (transaction_exists(db, idempotency_key)) return "duplicate";
    bool all_owned = !item_ids.empty();
    for (const auto& id : item_ids) {
        if (!owns_cosmetic(db, character_id, id)) {
            all_owned = false;
            break;
        }
    }
    if (all_owned) return "already_owned";
    if (get_platinum(db, character_id) < price) return "insufficient";
    if (price > 0) add_platinum(db, character_id, -price);
    for (const auto& id : item_ids) {
        grant_cosmetic(db, character_id, id, "store", now);
    }
    record_transaction(db, character_id, bundle_id, -price, "settled", idempotency_key, now);
    return "ok";
}

} // namespace cosmetics_db
