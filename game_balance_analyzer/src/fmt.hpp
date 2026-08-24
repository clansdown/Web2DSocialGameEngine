#pragma once

#include <sstream>
#include <string>

// Shared number formatting for analyzer output. Avoids the per-file duplication
// of the old `fmt()` helpers and, crucially, never prints scientific notation:
// large values are rendered with the integer part grouped in 3-digit comma
// groups (e.g. 1260 -> "1,260", 13278.08 -> "13,278.08").

namespace nfmt {

// Groups the digit string (integer part only, may include a leading '-') into
// 3-digit comma groups from the right.
inline std::string comma_group(std::string digits) {
    std::string sign;
    if (!digits.empty() && digits[0] == '-') {
        sign = "-";
        digits.erase(0, 1);
    }
    std::string out;
    int count = 0;
    for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
        if (count > 0 && count % 3 == 0) {
            out.insert(out.begin(), ',');
        }
        out.insert(out.begin(), digits[static_cast<size_t>(i)]);
        ++count;
    }
    return sign + out;
}

// Formats a double in fixed-point (never scientific) with up to `decimals`
// fractional digits, stripping trailing zeros and a trailing '.'. The integer
// part is comma-grouped. Examples: 1260 -> "1,260", 0.5 -> "0.5", 7.09 ->
// "7.09", 13278.08 -> "13,278.08", 4 -> "4".
inline std::string format_number(double v, int decimals = 3) {
    std::ostringstream out;
    out.precision(decimals);
    out << std::fixed << v;
    std::string s = out.str();
    // Split off the fractional part for comma grouping of the integer part.
    size_t dot = s.find('.');
    std::string integer = (dot == std::string::npos) ? s : s.substr(0, dot);
    std::string frac = (dot == std::string::npos) ? std::string() : s.substr(dot + 1);
    // Strip trailing zeros (but keep at least one digit if nonzero).
    while (!frac.empty() && frac.back() == '0') {
        frac.pop_back();
    }
    std::string result = comma_group(integer);
    if (!frac.empty()) {
        result += "." + frac;
    }
    return result;
}

// Formats an integer with comma grouping (no decimals).
inline std::string format_int(long long v) {
    return comma_group(std::to_string(v));
}

}  // namespace nfmt
