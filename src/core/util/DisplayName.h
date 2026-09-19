#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace handheld {
// UI fallback only: an unnamed identity stays unnamed in storage and announces.
// Share the same address-derived home label across both handheld renderers.
inline const char* deviceDisplayName(const char* name, const char* destination,
                                    const char* device, char (&fallback)[16]) {
    if (name && *name) return name;
    if (destination && strlen(destination) == 32) {
        bool hexadecimal = true;
        for (size_t i = 0; i < 32; ++i) {
            const char c = destination[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) hexadecimal = false;
        }
        if (hexadecimal) {
            memcpy(fallback, "Ratspeak.org-", 12);
            memcpy(fallback + 12, destination, 3);
            fallback[15] = '\0';
            return fallback;
        }
    }
    return device;
}

// Complete UTF-8 scalar values, with no overlong, surrogate or out-of-range
// encodings. Used for new input validation and the existing announce prefix.
inline size_t displayNameCodepoint(const char* text, size_t remaining) {
    if (!remaining) return 0;
    const auto* p = reinterpret_cast<const uint8_t*>(text);
    const uint8_t first = p[0];
    if (first < 0x80) return first >= 0x20 && first != 0x7f ? 1 : 0;
    const size_t n = first >= 0xc2 && first <= 0xdf ? 2 :
        first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!n || n > remaining) return 0;
    for (size_t i = 1; i < n; ++i) if ((p[i] & 0xc0) != 0x80) return 0;
    if ((first == 0xe0 && p[1] < 0xa0) || (first == 0xed && p[1] >= 0xa0) ||
        (first == 0xf0 && p[1] < 0x90) || (first == 0xf4 && p[1] >= 0x90)) return 0;
    return n;
}
inline bool validNewDisplayName(const char* text, size_t bytes) {
    size_t offset = 0, characters = 0;
    while (offset < bytes) {
        const size_t n = displayNameCodepoint(text + offset, bytes - offset);
        if (!n || ++characters > 16) return false;
        offset += n;
    }
    return true;
}
inline size_t displayNamePrefix(const char* text, size_t bytes, size_t maximum) {
    size_t offset = 0;
    while (offset < bytes) {
        const size_t n = displayNameCodepoint(text + offset, bytes - offset);
        if (!n || n > maximum - offset) break;
        offset += n;
    }
    return offset;
}
} // namespace handheld
