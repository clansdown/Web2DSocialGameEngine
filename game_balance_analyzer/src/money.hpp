#pragma once

#include "fmt.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

// Shared medieval-currency helpers for the analyzer. Mirrors the server's
// Money.hpp conventions and the "currency" block in economy.json, so money
// conversion and display live in exactly one place — no per-file
// re-derivations of gold/silver/pence formatting.
//
// Currency: 1 gold piece (£) = 20 silver shillings = 240 silver pence;
// 1 shilling = 12 pence. Gold and silver_pence are automatically fungible at
// this rate in the game, so any gold-equivalent value can be formatted as a
// single medieval monetary unit.

namespace money {

constexpr double pence_per_shilling = 12.0;
constexpr double shillings_per_gold = 20.0;
constexpr double pence_per_gold = pence_per_shilling * shillings_per_gold;

// Gold value of a money object {gold, shillings, pence} (all optional).
inline double money_object_to_gold(const nlohmann::json& obj) {
    return obj.value("gold", 0.0) + obj.value("shillings", 0.0) / shillings_per_gold
           + obj.value("pence", 0.0) / pence_per_gold;
}

// Pence value of a money object (gold*240 + shillings*12 + pence).
// Fractional pence (e.g. 0.5) are preserved so half-penny prices work.
// Returns 0 on malformed (non-object) input.
inline double money_price_to_pence(const nlohmann::json& obj) {
    if (!obj.is_object()) {
        return 0.0;
    }
    return obj.value("gold", 0.0) * pence_per_gold
           + obj.value("shillings", 0.0) * pence_per_shilling
           + obj.value("pence", 0.0);
}

// Formats a silver-pence value compactly as shillings and pence, e.g. 208 ->
// "17s 4d", 12 -> "1s", 6 -> "6d", 0 -> "0s". Negatives get a "-" prefix.
inline std::string format_pence(double pence) {
    if (pence == 0.0) {
        return "0s";
    }
    bool neg = pence < 0.0;
    long long p = std::llround(std::fabs(pence));
    long long sh = p / 12;
    long long d = p % 12;
    std::string out;
    if (neg) {
        out += "-";
    }
    if (sh > 0 && d > 0) {
        out += nfmt::format_int(sh) + "s " + nfmt::format_int(d) + "d";
    } else if (sh > 0) {
        out += nfmt::format_int(sh) + "s";
    } else {
        out += nfmt::format_int(d) + "d";
    }
    return out;
}

// Formats a gold-equivalent amount (plus optional extra silver pence) as a
// single medieval monetary unit: gold coins (pounds of silver), shillings, and
// pence. Compact — leading zero units are dropped ("4g 3s 2d", "14s 3d",
// "1g", "7d"), zero renders as "0s", negatives get a "-" prefix
// (e.g. -0.683g -> "-13s 8d").
inline std::string format_money(double gold, double pence = 0.0) {
    long long total = std::llround(gold * pence_per_gold + pence);
    if (total == 0) {
        return "0s";
    }
    bool neg = total < 0;
    long long a = std::llabs(total);
    long long g = a / static_cast<long long>(pence_per_gold);
    long long s = (a % static_cast<long long>(pence_per_gold)) / 12;
    long long d = a % 12;
    std::string out;
    if (neg) {
        out += "-";
    }
    if (g > 0) {
        out += nfmt::format_int(g) + "g";
        if (s > 0 || d > 0) {
            out += " ";
        }
    }
    if (s > 0) {
        out += nfmt::format_int(s) + "s";
        if (d > 0) {
            out += " ";
        }
    }
    if (d > 0 || (g == 0 && s == 0)) {
        out += nfmt::format_int(d) + "d";
    }
    return out;
}

}  // namespace money