#include "AvatarValidation.hpp"

#include <algorithm>
#include <cctype>

namespace {

std::string lower_ascii(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool is_lower_hex(std::string_view value) {
    for (char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool is_digits(std::string_view value) {
    if (value.empty()) return false;
    for (char c : value) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

} // namespace

std::optional<avatar_format> parse_avatar_format(std::string_view value) {
    const std::string lowered = lower_ascii(value);
    if (lowered == "png") return avatar_format::png;
    if (lowered == "jpg" || lowered == "jpeg") return avatar_format::jpeg;
    if (lowered == "webp") return avatar_format::webp;
    if (lowered == "avif") return avatar_format::avif;
    return std::nullopt;
}

std::string avatar_extension(avatar_format format) {
    switch (format) {
        case avatar_format::png: return "png";
        case avatar_format::jpeg: return "jpg";
        case avatar_format::webp: return "webp";
        case avatar_format::avif: return "avif";
    }
    return "bin";
}

std::string avatar_content_type(avatar_format format) {
    switch (format) {
        case avatar_format::png: return "image/png";
        case avatar_format::jpeg: return "image/jpeg";
        case avatar_format::webp: return "image/webp";
        case avatar_format::avif: return "image/avif";
    }
    return "application/octet-stream";
}

bool bytes_match_format(avatar_format format, std::string_view bytes) {
    switch (format) {
        case avatar_format::png:
            return bytes.size() >= 8 &&
                   static_cast<unsigned char>(bytes[0]) == 0x89 &&
                   bytes.compare(0, 4, "\x89PNG") == 0;
        case avatar_format::jpeg:
            return bytes.size() >= 3 &&
                   static_cast<unsigned char>(bytes[0]) == 0xFF &&
                   static_cast<unsigned char>(bytes[1]) == 0xD8 &&
                   static_cast<unsigned char>(bytes[2]) == 0xFF;
        case avatar_format::webp:
            return bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 &&
                   bytes.compare(8, 4, "WEBP") == 0;
        case avatar_format::avif:
            return bytes.size() >= 12 && bytes.compare(4, 4, "ftyp") == 0;
    }
    return false;
}

bool avatar_key_is_valid(std::string_view key) {
    if (key.empty() || key.size() > 160) return false;
    const std::size_t slash = key.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= key.size()) {
        return false;
    }
    if (key.find('/', slash + 1) != std::string_view::npos) return false;
    if (!is_digits(key.substr(0, slash))) return false;

    const std::string_view filename = key.substr(slash + 1);
    const std::size_t dot = filename.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= filename.size()) {
        return false;
    }
    const std::string_view hash = filename.substr(0, dot);
    if (hash.size() != 64 || !is_lower_hex(hash)) return false;
    const std::string_view extension = filename.substr(dot + 1);
    return extension == "png" || extension == "jpg" || extension == "jpeg" ||
           extension == "webp" || extension == "avif";
}
