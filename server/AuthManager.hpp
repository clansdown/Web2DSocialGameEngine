#pragma once

#include "Database.hpp"
#include "auth/SessionManager.hpp"

#include <string>

// Legacy facade kept for the existing handler call sites. All state lives in
// auth::SessionManager; this class performs no token derivation.
class AuthManager {
public:
    std::string authenticateWithPassword(const std::string& username,
                                         const std::string& password,
                                         const std::string& ip_address) {
        std::string error;
        auto issued = auth::SessionManager::instance().login(
            Database::getInstance().gameDB(), username, password, ip_address, std::string(), error);
        return issued ? issued->access_token : std::string();
    }

    bool authenticateWithToken(const std::string& username,
                               const std::string& token) {
        return auth::SessionManager::instance()
            .validate_access_token_for(username, token)
            .has_value();
    }

    void cacheToken(const std::string&, const std::string&) {}

    static AuthManager& getInstance() {
        static AuthManager instance;
        return instance;
    }
};
