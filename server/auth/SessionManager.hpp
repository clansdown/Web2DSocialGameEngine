#pragma once

#include "db/postgres_sqlite_compat.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace auth {

struct session_snapshot {
    std::string session_id;
    int64_t account_id = 0;
    std::string username;
    std::vector<int64_t> character_ids;
    bool adult = false;
    std::string language = "en";
    int64_t created_at = 0;
    int64_t last_used_at = 0;
};

struct issued_session {
    std::string session_id;
    std::string access_token;
    std::string refresh_token;
    session_snapshot snapshot;
};

struct session_list_entry {
    std::string session_id;
    int64_t created_at = 0;
    int64_t last_used_at = 0;
    std::string user_agent;
    std::string ip;
    bool current = false;
};

class SessionManager {
public:
    static SessionManager& instance();

    // Verifies the password, enforces the 5-session cap (LRU eviction), and
    // issues access + refresh tokens.
    std::optional<issued_session> login(sqlite::database& game_db,
                                        const std::string& username,
                                        const std::string& password,
                                        const std::string& ip,
                                        const std::string& user_agent,
                                        std::string& error);

    // Issues a session for an already-authenticated account (used by
    // createAccount).
    std::optional<issued_session> create_session(sqlite::database& game_db,
                                                 int64_t account_id,
                                                 const std::string& username,
                                                 bool adult,
                                                 const std::string& language,
                                                 const std::string& ip,
                                                 const std::string& user_agent,
                                                 std::string& error);

    // Validates an in-memory access token. No database call.
    std::optional<session_snapshot> validate_access_token(const std::string& token) const;
    std::optional<session_snapshot> validate_access_token_for(const std::string& username,
                                                              const std::string& token) const;

    // Validates a refresh token against user_sessions and issues a fresh
    // access token (refresh tokens do not rotate).
    std::optional<issued_session> refresh(sqlite::database& game_db,
                                          const std::string& refresh_token,
                                          std::string& error);

    bool revoke_session(sqlite::database& game_db, const std::string& session_id,
                        const std::string& reason);
    bool revoke_by_refresh(sqlite::database& game_db, const std::string& refresh_token,
                           const std::string& reason);
    bool revoke_all(sqlite::database& game_db, int64_t account_id, const std::string& reason);
    bool revoke_all_except(sqlite::database& game_db, int64_t account_id,
                           const std::string& keep_session_id, const std::string& reason);

    std::vector<session_list_entry> list_sessions(sqlite::database& game_db,
                                                  int64_t account_id,
                                                  const std::string& current_session_id) const;

    void update_character_ids(int64_t account_id, std::vector<int64_t> character_ids);
    void update_language(int64_t account_id, const std::string& language);
    void update_adult(int64_t account_id, bool adult);

    static constexpr int64_t kAccessTokenTtlSeconds = 15 * 60;

private:
    SessionManager() = default;

    struct access_entry {
        std::string session_id;
        int64_t expires_at = 0;
    };

    std::optional<session_snapshot> load_snapshot(sqlite::database& game_db,
                                                  const std::string& session_id,
                                                  int64_t account_id,
                                                  std::string& error) const;

    bool enforce_session_cap(sqlite::database& game_db, int64_t account_id,
                             int64_t now, std::string& error);

    void remember(const issued_session& issued);
    void forget_session(const std::string& session_id);

    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, access_entry> access_tokens_;
    mutable std::unordered_map<std::string, session_snapshot> sessions_;
};

} // namespace auth
