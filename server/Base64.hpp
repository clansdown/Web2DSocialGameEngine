#pragma once

#include <string>
#include <string_view>

// Decode a standard base64 payload (RFC 4648, '+/' alphabet). Whitespace is
// ignored and decoding stops at the first '='. Returns false when a non-
// whitespace character outside the alphabet and padding appears.
bool base64_decode(std::string_view encoded, std::string& decoded);
