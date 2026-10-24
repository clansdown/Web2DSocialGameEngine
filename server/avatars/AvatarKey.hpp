#pragma once

#include <string>
#include <string_view>

// Content-addressed storage key: "<character_id>/<sha256>.<extension>".
// The key is relative to either uploads/avatars/ or the configured R2 prefix.
std::string make_avatar_key(int character_id,
                            std::string_view sha256_hex,
                            std::string_view extension);
