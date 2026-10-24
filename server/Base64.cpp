#include "Base64.hpp"

#include <cctype>

bool base64_decode(std::string_view encoded, std::string& decoded) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int lookup[256];
    for (int i = 0; i < 256; ++i) lookup[i] = -1;
    for (int i = 0; i < 64; ++i) {
        lookup[static_cast<unsigned char>(alphabet[i])] = i;
    }

    decoded.clear();
    decoded.reserve(encoded.size() * 3 / 4);

    unsigned int value = 0;
    int bits = -8;
    for (char c : encoded) {
        if (c == '=') break;
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        const int digit = lookup[static_cast<unsigned char>(c)];
        if (digit < 0) return false;
        value = (value << 6) + static_cast<unsigned int>(digit);
        bits += 6;
        if (bits >= 0) {
            decoded.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}
