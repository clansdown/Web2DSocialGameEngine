#pragma once
#include <string>
#include <random>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/err.h>

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
    std::string salt = generateRandomSalt(16);
    std::string input = salt + password;

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    if (EVP_Digest(input.data(), input.size(), hash, &hash_len, EVP_sha512(), nullptr) != 1) {
        throw std::runtime_error("Password hashing failed");
    }

    std::string result = "$6$" + salt + "$";
    const char hex_chars[] = "0123456789abcdef";
    for (unsigned int i = 0; i < hash_len; i++) {
        result += hex_chars[(hash[i] >> 4) & 0xF];
        result += hex_chars[hash[i] & 0xF];
    }
    return result;
}

inline bool verifyPassword(const std::string& password, const std::string& stored_hash) {
    std::string computed = hashPassword(password);

    size_t salt_end = stored_hash.find('$', 3);
    if (salt_end == std::string::npos) {
        return false;
    }
    std::string stored_salt = stored_hash.substr(3, salt_end - 3);

    std::string input = stored_salt + password;
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    if (EVP_Digest(input.data(), input.size(), hash, &hash_len, EVP_sha512(), nullptr) != 1) {
        return false;
    }

    std::string computed_hash = "$6$" + stored_salt + "$";
    const char hex_chars[] = "0123456789abcdef";
    for (unsigned int i = 0; i < hash_len; i++) {
        computed_hash += hex_chars[(hash[i] >> 4) & 0xF];
        computed_hash += hex_chars[hash[i] & 0xF];
    }

    return computed_hash == stored_hash;
}
