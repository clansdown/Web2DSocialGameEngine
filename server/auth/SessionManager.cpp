#include "auth/SessionManager.hpp"

#include "Hash.hpp"
#include "auth/Argon2.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/rand.h>

namespace auth {

namespace {

constexpr std::size_t kTokenBytes = 32;
constexpr int kMaxSessionsPerAccount = 5;

int64_t now_seconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string base64url_encode(const unsigned char* data, std::size_t length) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((length + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < length) {
        const unsigned int value = (static_cast<unsigned int>(data[i]) << 16) |
                                   (static_cast<unsigned int>(data[i + 1]) << 8) |
                                   static_cast<unsigned int>(data[i + 2]);
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out.push_back(alphabet[(value >> 6) & 0x3F]);
        out.push_back(alphabet[value & 0x3F]);
        i += 3;
    }
    if (i + 1 == length) {
        const unsigned int value = static_cast<unsigned int>(data[i]) << 16;
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
    } else if (i + 2 == length) {
        const unsigned int value = (static_cast<unsigned int>(data[i]) << 16) |
                                   (static_cast<unsigned int>(data[i + 1]) << 8);
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out.push_back(alphabet[(value >> 6) & 0x3F]);
    }
    return out;
}

std::string random_token(const char* prefix) {
    std::array<unsigned char, kTokenBytes> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        throw std::runtime_error("session: RAND_bytes failed");
    }
    return std::string(prefix) + base64url_encode(bytes.data(), bytes.size());
}

std::string make_uuid_v4() {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        throw std::runtime_error("session: RAND_bytes failed");
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);
    char buffer[37];
    std::snprintf(buffer, sizeof(buffer),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                  bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
                  bytes[12], bytes[13], bytes[14], bytes[15]);
    return std::string(buffer);
}

std::string access_hash(const std::string& token) {
    return sha256_hex(token);
}

} // namespace

SessionManager& SessionManager::instance() {
    static SessionManager manager;
    return manager;
}

std::optional<issued_session> SessionManager::login(sqlite::database& game_db,
                                                     const std::string& username,
                                                     const std::string& password,
                                                     const std::string& ip,
                                                     const std::string& user_agent,
                                                     std::string& error) {
    int64_t account_id = 0;
    bool adult = false;
    std::string language = "en";
    std::string password_hash;
    bool found = false;
    try {
        game_db << "SELECT id, password_hash, adult, COALESCE(language, 'en') "
                   "FROM users WHERE LOWER(username) = LOWER(?)"
                << username
                >> [&](int64_t id, std::string hash, bool is_adult, std::string lang) {
                       account_id = id;
                       password_hash = std::move(hash);
                       adult = is_adult;
                       language = std::move(lang);
                       found = true;
                   };
    } catch (const std::exception& exception) {
        error = std::string("login failed: ") + exception.what();
        return std::nullopt;
    }

    if (!found) {
        auth::verify_dummy_password(password);
        error = "invalid_credentials";
        return std::nullopt;
    }
    if (!auth::verify_password_argon2id(password, password_hash)) {
        error = "invalid_credentials";
        return std::nullopt;
    }
    return create_session(game_db, account_id, username, adult, language, ip, user_agent, error);
}

std::optional<issued_session> SessionManager::create_session(sqlite::database& game_db,
                                                              int64_t account_id,
                                                              const std::string& username,
                                                              bool adult,
                                                              const std::string& language,
                                                              const std::string& ip,
                                                              const std::string& user_agent,
                                                              std::string& error) {
    const int64_t now = now_seconds();
    if (!enforce_session_cap(game_db, account_id, now, error)) {
        return std::nullopt;
    }

    issued_session issued;
    issued.session_id = make_uuid_v4();
    issued.access_token = random_token("rvs_at_");
    issued.refresh_token = random_token("rvs_rt_");

    try {
        game_db << "INSERT INTO user_sessions "
                   "(id, account_id, refresh_token_hash, created_at, last_used_at, user_agent, ip) "
                   "VALUES (?, ?, ?, ?, ?, ?, ?)"
                << issued.session_id << account_id << access_hash(issued.refresh_token)
                << now << now << user_agent << ip;
    } catch (const std::exception& exception) {
        error = std::string("could not create session: ") + exception.what();
        return std::nullopt;
    }

    issued.snapshot.session_id = issued.session_id;
    issued.snapshot.account_id = account_id;
    issued.snapshot.username = username;
    issued.snapshot.adult = adult;
    issued.snapshot.language = language;
    issued.snapshot.created_at = now;
    issued.snapshot.last_used_at = now;
    try {
        game_db << "SELECT id FROM characters WHERE user_id = ? ORDER BY id" << account_id
                >> [&](int64_t character_id) { issued.snapshot.character_ids.push_back(character_id); };
    } catch (const std::exception& exception) {
        error = std::string("could not load account characters: ") + exception.what();
        return std::nullopt;
    }

    remember(issued);
    return issued;
}

std::optional<session_snapshot> SessionManager::validate_access_token(const std::string& token) const {
    if (token.empty()) return std::nullopt;
    const std::string hash = access_hash(token);
    const int64_t now = now_seconds();
    std::lock_guard<std::mutex> lock(mutex_);
    auto token_it = access_tokens_.find(hash);
    if (token_it == access_tokens_.end()) return std::nullopt;
    if (token_it->second.expires_at <= now) {
        access_tokens_.erase(token_it);
        return std::nullopt;
    }
    auto session_it = sessions_.find(token_it->second.session_id);
    if (session_it == sessions_.end()) {
        access_tokens_.erase(token_it);
        return std::nullopt;
    }
    return session_it->second;
}

std::optional<session_snapshot> SessionManager::validate_access_token_for(
    const std::string& username, const std::string& token) const {
    auto snapshot = validate_access_token(token);
    if (!snapshot) return std::nullopt;
    if (snapshot->username != username) return std::nullopt;
    return snapshot;
}

std::optional<issued_session> SessionManager::refresh(sqlite::database& game_db,
                                                       const std::string& refresh_token,
                                                       std::string& error) {
    if (refresh_token.empty()) {
        error = "invalid_refresh_token";
        return std::nullopt;
    }
    std::string session_id;
    int64_t account_id = 0;
    bool found = false;
    try {
        game_db << "SELECT id, account_id FROM user_sessions "
                   "WHERE refresh_token_hash = ? AND revoked_at IS NULL"
                << access_hash(refresh_token)
                >> [&](std::string id, int64_t account) {
                       session_id = std::move(id);
                       account_id = account;
                       found = true;
                   };
    } catch (const std::exception& exception) {
        error = std::string("refresh failed: ") + exception.what();
        return std::nullopt;
    }
    if (!found) {
        error = "invalid_refresh_token";
        return std::nullopt;
    }

    const int64_t now = now_seconds();
    try {
        game_db << "UPDATE user_sessions SET last_used_at = ? WHERE id = ?"
                << now << session_id;
    } catch (const std::exception& exception) {
        error = std::string("refresh failed: ") + exception.what();
        return std::nullopt;
    }

    auto snapshot = load_snapshot(game_db, session_id, account_id, error);
    if (!snapshot) return std::nullopt;

    issued_session issued;
    issued.session_id = session_id;
    issued.access_token = random_token("rvs_at_");
    issued.refresh_token = refresh_token;
    issued.snapshot = *snapshot;
    issued.snapshot.last_used_at = now;
    remember(issued);
    return issued;
}

bool SessionManager::revoke_session(sqlite::database& game_db, const std::string& session_id,
                                    const std::string& reason) {
    if (session_id.empty()) return false;
    try {
        game_db << "UPDATE user_sessions SET revoked_at = ?, revoked_reason = ? "
                   "WHERE id = ? AND revoked_at IS NULL"
                << now_seconds() << reason << session_id;
    } catch (...) {
        return false;
    }
    forget_session(session_id);
    return true;
}

bool SessionManager::revoke_by_refresh(sqlite::database& game_db,
                                       const std::string& refresh_token,
                                       const std::string& reason) {
    if (refresh_token.empty()) return false;
    std::string session_id;
    try {
        game_db << "SELECT id FROM user_sessions "
                   "WHERE refresh_token_hash = ? AND revoked_at IS NULL"
                << access_hash(refresh_token)
                >> [&](std::string id) { session_id = std::move(id); };
    } catch (...) {
        return false;
    }
    if (session_id.empty()) return false;
    return revoke_session(game_db, session_id, reason);
}

bool SessionManager::revoke_all(sqlite::database& game_db, int64_t account_id,
                                const std::string& reason) {
    std::vector<std::string> ids;
    try {
        game_db << "SELECT id FROM user_sessions WHERE account_id = ? AND revoked_at IS NULL"
                << account_id
                >> [&](std::string id) { ids.push_back(std::move(id)); };
    } catch (...) {
        return false;
    }
    for (const auto& id : ids) {
        try {
            game_db << "UPDATE user_sessions SET revoked_at = ?, revoked_reason = ? WHERE id = ?"
                    << now_seconds() << reason << id;
        } catch (...) {
            return false;
        }
        forget_session(id);
    }
    return true;
}

bool SessionManager::revoke_all_except(sqlite::database& game_db, int64_t account_id,
                                       const std::string& keep_session_id,
                                       const std::string& reason) {
    std::vector<std::string> ids;
    try {
        game_db << "SELECT id FROM user_sessions WHERE account_id = ? AND revoked_at IS NULL"
                << account_id
                >> [&](std::string id) {
                       if (id != keep_session_id) ids.push_back(std::move(id));
                   };
    } catch (...) {
        return false;
    }
    for (const auto& id : ids) {
        try {
            game_db << "UPDATE user_sessions SET revoked_at = ?, revoked_reason = ? WHERE id = ?"
                    << now_seconds() << reason << id;
        } catch (...) {
            return false;
        }
        forget_session(id);
    }
    return true;
}

std::vector<session_list_entry> SessionManager::list_sessions(
    sqlite::database& game_db, int64_t account_id, const std::string& current_session_id) const {
    std::vector<session_list_entry> entries;
    try {
        game_db << "SELECT id, created_at, last_used_at, COALESCE(user_agent, ''), COALESCE(ip, '') "
                   "FROM user_sessions WHERE account_id = ? AND revoked_at IS NULL "
                   "ORDER BY last_used_at DESC"
                << account_id
                >> [&](std::string id, int64_t created_at, int64_t last_used_at,
                       std::string user_agent, std::string ip) {
                       session_list_entry entry;
                       entry.session_id = std::move(id);
                       entry.created_at = created_at;
                       entry.last_used_at = last_used_at;
                       entry.user_agent = std::move(user_agent);
                       entry.ip = std::move(ip);
                       entry.current = !current_session_id.empty() &&
                                       entry.session_id == current_session_id;
                       entries.push_back(std::move(entry));
                   };
    } catch (...) {
        return {};
    }
    return entries;
}

void SessionManager::update_character_ids(int64_t account_id, std::vector<int64_t> character_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : sessions_) {
        if (entry.second.account_id == account_id) {
            entry.second.character_ids = character_ids;
        }
    }
}

void SessionManager::update_language(int64_t account_id, const std::string& language) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : sessions_) {
        if (entry.second.account_id == account_id) {
            entry.second.language = language;
        }
    }
}

void SessionManager::update_adult(int64_t account_id, bool adult) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : sessions_) {
        if (entry.second.account_id == account_id) {
            entry.second.adult = adult;
        }
    }
}

std::optional<session_snapshot> SessionManager::load_snapshot(sqlite::database& game_db,
                                                              const std::string& session_id,
                                                              int64_t account_id,
                                                              std::string& error) const {
    session_snapshot snapshot;
    bool found = false;
    try {
        game_db << "SELECT u.username, u.adult, COALESCE(u.language, 'en'), s.created_at, s.last_used_at "
                   "FROM user_sessions s JOIN users u ON u.id = s.account_id "
                   "WHERE s.id = ? AND s.account_id = ? AND s.revoked_at IS NULL"
                << session_id << account_id
                >> [&](std::string username, bool adult, std::string language,
                       int64_t created_at, int64_t last_used_at) {
                       snapshot.username = std::move(username);
                       snapshot.adult = adult;
                       snapshot.language = std::move(language);
                       snapshot.created_at = created_at;
                       snapshot.last_used_at = last_used_at;
                       found = true;
                   };
        if (!found) {
            error = "invalid_refresh_token";
            return std::nullopt;
        }
        game_db << "SELECT id FROM characters WHERE user_id = ? ORDER BY id" << account_id
                >> [&](int64_t character_id) { snapshot.character_ids.push_back(character_id); };
    } catch (const std::exception& exception) {
        error = std::string("could not load session: ") + exception.what();
        return std::nullopt;
    }
    snapshot.session_id = session_id;
    snapshot.account_id = account_id;
    return snapshot;
}

bool SessionManager::enforce_session_cap(sqlite::database& game_db, int64_t account_id,
                                         int64_t now, std::string& error) {
    int count = 0;
    try {
        game_db << "SELECT COUNT(*) FROM user_sessions WHERE account_id = ? AND revoked_at IS NULL"
                << account_id
                >> [&](int value) { count = value; };
    } catch (const std::exception& exception) {
        error = std::string("could not check session count: ") + exception.what();
        return false;
    }
    if (count < kMaxSessionsPerAccount) return true;

    const int to_revoke = count - (kMaxSessionsPerAccount - 1);
    std::vector<std::string> ids;
    try {
        game_db << "SELECT id FROM user_sessions WHERE account_id = ? AND revoked_at IS NULL "
                   "ORDER BY last_used_at ASC LIMIT ?"
                << account_id << to_revoke
                >> [&](std::string id) { ids.push_back(std::move(id)); };
    } catch (const std::exception& exception) {
        error = std::string("could not load old sessions: ") + exception.what();
        return false;
    }
    for (const auto& id : ids) {
        try {
            game_db << "UPDATE user_sessions SET revoked_at = ?, revoked_reason = 'session_limit' "
                       "WHERE id = ?"
                    << now << id;
        } catch (const std::exception& exception) {
            error = std::string("could not revoke old session: ") + exception.what();
            return false;
        }
        forget_session(id);
    }
    return true;
}

void SessionManager::remember(const issued_session& issued) {
    const int64_t expires_at = now_seconds() + kAccessTokenTtlSeconds;
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[issued.session_id] = issued.snapshot;
    access_tokens_[access_hash(issued.access_token)] = access_entry{issued.session_id, expires_at};
}

void SessionManager::forget_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session_id);
    for (auto it = access_tokens_.begin(); it != access_tokens_.end();) {
        if (it->second.session_id == session_id) {
            it = access_tokens_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace auth
