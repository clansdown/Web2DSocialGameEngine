#include "Hash.hpp"

#include <array>
#include <vector>

#include <openssl/evp.h>

namespace {

constexpr std::size_t kSha256DigestBytes = 32;
constexpr std::size_t kSha256BlockBytes = 64;

std::string digest_sha256(std::string_view bytes) {
    std::array<unsigned char, kSha256DigestBytes> digest{};
    unsigned int digest_length = 0;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr) return {};
    if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(context, bytes.data(), bytes.size()) != 1 ||
        EVP_DigestFinal_ex(context, digest.data(), &digest_length) != 1) {
        EVP_MD_CTX_free(context);
        return {};
    }
    EVP_MD_CTX_free(context);
    return std::string(reinterpret_cast<const char*>(digest.data()), digest_length);
}

} // namespace

std::string hex_encode(const unsigned char* data, std::size_t length) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.resize(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out[i * 2] = digits[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    return out;
}

std::string sha256_raw(std::string_view bytes) {
    return digest_sha256(bytes);
}

std::string sha256_hex(std::string_view bytes) {
    const std::string raw = digest_sha256(bytes);
    return hex_encode(reinterpret_cast<const unsigned char*>(raw.data()), raw.size());
}

std::string hmac_sha256_raw(std::string_view key, std::string_view data) {
    std::array<unsigned char, kSha256BlockBytes> normalized_key{};
    if (key.size() > kSha256BlockBytes) {
        const std::string hashed_key = digest_sha256(key);
        for (std::size_t i = 0; i < hashed_key.size(); ++i) {
            normalized_key[i] = static_cast<unsigned char>(hashed_key[i]);
        }
    } else {
        for (std::size_t i = 0; i < key.size(); ++i) {
            normalized_key[i] = static_cast<unsigned char>(key[i]);
        }
    }

    std::string inner;
    inner.resize(kSha256BlockBytes + data.size());
    for (std::size_t i = 0; i < kSha256BlockBytes; ++i) {
        inner[i] = static_cast<char>(normalized_key[i] ^ 0x36);
    }
    for (std::size_t i = 0; i < data.size(); ++i) {
        inner[kSha256BlockBytes + i] = data[i];
    }
    const std::string inner_hash = digest_sha256(inner);

    std::string outer;
    outer.resize(kSha256BlockBytes + inner_hash.size());
    for (std::size_t i = 0; i < kSha256BlockBytes; ++i) {
        outer[i] = static_cast<char>(normalized_key[i] ^ 0x5C);
    }
    for (std::size_t i = 0; i < inner_hash.size(); ++i) {
        outer[kSha256BlockBytes + i] = inner_hash[i];
    }
    return digest_sha256(outer);
}

std::string hmac_sha256_hex(std::string_view key, std::string_view data) {
    const std::string raw = hmac_sha256_raw(key, data);
    return hex_encode(reinterpret_cast<const unsigned char*>(raw.data()), raw.size());
}
