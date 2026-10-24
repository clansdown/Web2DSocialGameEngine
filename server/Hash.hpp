#pragma once

#include <string>
#include <string_view>

// Small OpenSSL-backed digest helpers shared by the avatar storage layer.
// They return lowercase hexadecimal strings unless the name says "raw".
std::string sha256_hex(std::string_view bytes);
std::string sha256_raw(std::string_view bytes);
std::string hmac_sha256_raw(std::string_view key, std::string_view data);
std::string hmac_sha256_hex(std::string_view key, std::string_view data);
std::string hex_encode(const unsigned char* data, std::size_t length);
