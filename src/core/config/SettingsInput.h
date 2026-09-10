#pragma once

#include <cstdint>
#include <string_view>

namespace handheld::settings {
// Accept one complete decimal integer, without truncating a prefix or wrapping
// before range validation. Failure leaves the caller's output unchanged.
inline bool parseInteger(std::string_view text, int32_t minimum, int32_t maximum, int32_t& output) {
    if (text.empty() || minimum > maximum) return false;
    const bool negative = text.front() == '-';
    const size_t first = negative || text.front() == '+' ? 1 : 0;
    if (first == text.size()) return false;
    const uint32_t limit = negative ? 2147483648u : 2147483647u;
    uint32_t magnitude = 0;
    for (size_t i = first; i < text.size(); ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') return false;
        const uint32_t digit = uint32_t(c - '0');
        if (magnitude > (limit - digit) / 10) return false;
        magnitude = magnitude * 10 + digit;
    }
    const int64_t value = negative ? -int64_t(magnitude) : int64_t(magnitude);
    if (value < minimum || value > maximum) return false;
    output = int32_t(value);
    return true;
}
} // namespace handheld::settings
