#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

enum class avatar_format {
    png,
    jpeg,
    webp,
    avif,
};

constexpr std::size_t k_avatar_max_upload_bytes = 1024 * 1024;  // 1 MB

// Accepts png/jpg/jpeg/webp/avif (case-insensitive in callers).
std::optional<avatar_format> parse_avatar_format(std::string_view value);

// Storage extension / HTTP content type for one format.
std::string avatar_extension(avatar_format format);
std::string avatar_content_type(avatar_format format);

// Magic-byte identity check for the declared format.
bool bytes_match_format(avatar_format format, std::string_view bytes);

// Valid storage keys are "<character_id>/<64 lowercase hex>.<ext>".
bool avatar_key_is_valid(std::string_view key);
