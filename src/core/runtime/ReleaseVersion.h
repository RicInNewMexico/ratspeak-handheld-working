#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace handheld {
namespace release_version {
struct Version {
    uint32_t part[3] = {};
    const char* prerelease = nullptr;
    size_t length = 0;
};
inline bool digit(char c) { return c >= '0' && c <= '9'; }
inline bool identifier(char c) {
    return digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
}
inline bool suffix(const char*& cursor, bool prerelease) {
    do {
        const char* start = cursor;
        bool numeric = true;
        while (identifier(*cursor)) { numeric = numeric && digit(*cursor); ++cursor; }
        if (cursor == start || (prerelease && numeric && *start == '0' && cursor - start > 1)) return false;
        if (*cursor != '.') break;
        ++cursor;
    } while (true);
    return true;
}
inline bool parse(const char* cursor, Version& value) {
    if (!cursor) return false;
    value = {};
    for (size_t i = 0; i < 3; ++i) {
        const char* start = cursor;
        if (!digit(*cursor)) return false;
        while (digit(*cursor)) {
            const unsigned next = static_cast<unsigned>(*cursor++ - '0');
            if (value.part[i] > (UINT32_MAX - next) / 10) return false;
            value.part[i] = value.part[i] * 10 + next;
        }
        if (*start == '0' && cursor - start > 1) return false;
        if (i < 2 && *cursor++ != '.') return false;
    }
    if (*cursor == '-') {
        value.prerelease = ++cursor;
        if (!suffix(cursor, true)) return false;
        value.length = static_cast<size_t>(cursor - value.prerelease);
    }
    if (*cursor == '+') { ++cursor; if (!suffix(cursor, false)) return false; }
    return *cursor == '\0';
}
inline int compare(const Version& a, const Version& b) {
    for (size_t i = 0; i < 3; ++i) {
        if (a.part[i] != b.part[i]) return a.part[i] > b.part[i] ? 1 : -1;
    }
    if (!a.length || !b.length) return !a.length ? (b.length ? 1 : 0) : -1;
    size_t ai = 0, bi = 0;
    while (ai < a.length && bi < b.length) {
        const size_t ab = ai, bb = bi;
        bool an = true, bn = true;
        while (ai < a.length && a.prerelease[ai] != '.') an = digit(a.prerelease[ai++]) && an;
        while (bi < b.length && b.prerelease[bi] != '.') bn = digit(b.prerelease[bi++]) && bn;
        const size_t al = ai - ab, bl = bi - bb;
        if (an != bn) return an ? -1 : 1;
        if (an && al != bl) return al > bl ? 1 : -1;
        const int order = memcmp(a.prerelease + ab, b.prerelease + bb, al < bl ? al : bl);
        if (order) return order > 0 ? 1 : -1;
        if (al != bl) return al > bl ? 1 : -1;
        if (ai < a.length) ++ai;
        if (bi < b.length) ++bi;
    }
    return ai < a.length ? 1 : (bi < b.length ? -1 : 0);
}
} // namespace release_version
} // namespace handheld
