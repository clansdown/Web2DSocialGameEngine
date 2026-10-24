#include "AvatarKey.hpp"

std::string make_avatar_key(int character_id,
                            std::string_view sha256_hex,
                            std::string_view extension) {
    return std::to_string(character_id) + "/" + std::string(sha256_hex) + "." +
           std::string(extension);
}
