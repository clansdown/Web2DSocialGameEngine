#pragma once
#include <string>
#include <random>
#include <stdexcept>
#include <crypt.h>

std::string generateRandomSalt(int length = 16) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    std::string salt;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

    for (int i = 0; i < length; i++) {
        salt += charset[dis(gen)];
    }
    return salt;
}

inline std::string hashPassword(const std::string& password) {
    std::string salt = "$y$" + generateRandomSalt(16) + "$";
    char* hashed = crypt(password.c_str(), salt.c_str());
    if (!hashed) {
        throw std::runtime_error("Password hashing failed");
    }
    return std::string(hashed);
}

inline bool verifyPassword(const std::string& password, const std::string& stored_hash) {
    char* computed = crypt(password.c_str(), stored_hash.c_str());
    if (!computed) {
        return false;
    }
    return std::string(computed) == stored_hash;
}