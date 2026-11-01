#include "auth/Argon2.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>

#include <openssl/rand.h>

// libargon2 C API (runtime library is available even when the -dev headers are
// not installed in the sandbox).
extern "C" {
enum Argon2_type { Argon2_d = 0, Argon2_i = 1, Argon2_id = 2 };
int argon2id_hash_encoded(uint32_t t_cost, uint32_t m_cost, uint32_t parallelism,
                          const void* pwd, size_t pwdlen,
                          const void* salt, size_t saltlen,
                          size_t hashlen, char* encoded, size_t encodedlen);
int argon2id_verify(const char* encoded, const void* pwd, size_t pwdlen);
}

namespace auth {

namespace {

// OWASP 2024 baseline for Argon2id: 64 MiB, 3 iterations, 1 lane. The auth
// pool is small and explicit, so this never runs on the uWS loop.
constexpr uint32_t kTimeCost = 3;
constexpr uint32_t kMemoryCostKib = 65536;
constexpr uint32_t kParallelism = 1;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;
constexpr std::size_t kEncodedBytes = 256;

std::array<unsigned char, kSaltBytes> random_salt() {
    std::array<unsigned char, kSaltBytes> salt{};
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        throw std::runtime_error("argon2: RAND_bytes failed");
    }
    return salt;
}

const std::string& dummy_hash() {
    static const std::string value = [] {
        std::array<unsigned char, kSaltBytes> salt{};
        for (std::size_t i = 0; i < salt.size(); ++i) {
            salt[i] = static_cast<unsigned char>(i + 1);
        }
        std::array<char, kEncodedBytes> encoded{};
        if (argon2id_hash_encoded(kTimeCost, kMemoryCostKib, kParallelism,
                                  "ravenest-dummy-password", 23,
                                  salt.data(), salt.size(), kHashBytes,
                                  encoded.data(), encoded.size()) != 0) {
            throw std::runtime_error("argon2: could not build dummy hash");
        }
        return std::string(encoded.data());
    }();
    return value;
}

} // namespace

std::string hash_password_argon2id(const std::string& password) {
    const auto salt = random_salt();
    std::array<char, kEncodedBytes> encoded{};
    const int result = argon2id_hash_encoded(
        kTimeCost, kMemoryCostKib, kParallelism,
        password.data(), password.size(),
        salt.data(), salt.size(), kHashBytes,
        encoded.data(), encoded.size());
    if (result != 0) {
        throw std::runtime_error("argon2: password hashing failed");
    }
    return std::string(encoded.data());
}

bool verify_password_argon2id(const std::string& password, const std::string& encoded) {
    if (encoded.empty()) return false;
    return argon2id_verify(encoded.c_str(), password.data(), password.size()) == 0;
}

bool verify_dummy_password(const std::string& password) {
    (void)argon2id_verify(dummy_hash().c_str(), password.data(), password.size());
    return false;
}

} // namespace auth
