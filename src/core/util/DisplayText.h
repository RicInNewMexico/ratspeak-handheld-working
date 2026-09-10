#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace handheld::display {
inline bool continuation(uint8_t byte) { return (byte & 0xc0) == 0x80; }

// Return a whole valid UTF-8 code point, zero for an incomplete suffix, or one
// escaped byte for invalid input. Invalid/control bytes remain visibly present.
inline size_t codepoint(const uint8_t* bytes, size_t length, bool final, bool& escape) {
    const uint8_t first = bytes[0];
    if (first < 0x80) {
        escape = (first < 0x20 && first != '\n' && first != '\t') || first == 0x7f;
        return 1;
    }
    const size_t count = first >= 0xc2 && first <= 0xdf ? 2 :
        first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!count) { escape = true; return 1; }
    for (size_t i = 1; i < std::min(length, count); ++i) {
        if (!continuation(bytes[i]) || (i == 1 &&
            ((first == 0xe0 && bytes[i] < 0xa0) || (first == 0xed && bytes[i] >= 0xa0) ||
             (first == 0xf0 && bytes[i] < 0x90) || (first == 0xf4 && bytes[i] >= 0x90)))) {
            escape = true; return 1;
        }
    }
    if (length < count) { escape = final; return final ? 1 : 0; }
    return count;
}
} // namespace handheld::display
