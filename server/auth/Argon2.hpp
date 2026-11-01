#pragma once

#include <string>

namespace auth {

// Argon2id password hashing. Returns a PHC-format string.
std::string hash_password_argon2id(const std::string& password);

// Verifies a PHC-format Argon2id string. Returns false for malformed hashes.
bool verify_password_argon2id(const std::string& password, const std::string& encoded);

// Runs a real Argon2id verification against a dummy hash. Used for unknown
// usernames so login latency does not reveal account existence.
bool verify_dummy_password(const std::string& password);

} // namespace auth
