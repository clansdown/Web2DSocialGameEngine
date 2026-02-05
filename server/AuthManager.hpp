#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <random>
#include <cstdio>
#include <openssl/sha.h>

class AuthManager {
private:
    std::vector<uint8_t> secret_salt;
    std::unordered_map<std::string, std::string> tokens;

    std::string hashTokenInput(const std::string& username,
                               const std::string& password,
                               const std::string& ip_address) const {
        std::string salted = std::string((char*)secret_salt.data(), secret_salt.size()) +
                            username + password + ip_address;
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256((unsigned char*)salted.c_str(), salted.size(), hash);
        std::string hex;
        hex.reserve(SHA256_DIGEST_LENGTH * 2);
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x", hash[i]);
            hex += buf;
        }
        return hex;
    }

public:
    AuthManager() {
        secret_salt.resize(32);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(0, 255);
        for (auto& byte : secret_salt) {
            byte = static_cast<uint8_t>(dis(gen));
        }
    }

    std::string authenticateWithPassword(const std::string& username,
                                        const std::string& password,
                                        const std::string& ip_address) {
        std::string token = hashTokenInput(username, password, ip_address);
        cacheToken(username, token);
        return token;
    }

    bool authenticateWithToken(const std::string& username,
                              const std::string& token) {
        auto it = tokens.find(username);
        if (it == tokens.end()) {
            return false;
        }
        return it->second == token;
    }

    void cacheToken(const std::string& username, const std::string& token) {
        tokens[username] = token;
    }

    static AuthManager& getInstance() {
        static AuthManager instance;
        return instance;
    }
};