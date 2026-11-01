#pragma once

#include "auth/Argon2.hpp"

#include <string>

// Compatibility wrappers for the old password API. All callers now use
// Argon2id; the legacy SHA-512 format is not accepted.
inline std::string hashPassword(const std::string& password) {
    return auth::hash_password_argon2id(password);
}

inline bool verifyPassword(const std::string& password, const std::string& stored_hash) {
    return auth::verify_password_argon2id(password, stored_hash);
}
