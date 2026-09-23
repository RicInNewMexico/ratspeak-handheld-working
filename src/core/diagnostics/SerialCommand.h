#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace handheld::diagnostics {

inline bool separator(char c) {
    return c == ' ' || c == '\t' || c == ':' || c == '=' || c == ',';
}
inline const char* skipSeparators(const char* p) {
    while (p && separator(*p)) ++p;
    return p;
}
inline bool hasArgument(const char* p) {
    p = skipSeparators(p);
    return p && *p;
}

// Parse the target's 32-bit integer range even on a 64-bit host. A suffix is
// legal only when the caller explicitly requests another argument.
inline bool parseInteger(const char* p, int32_t& value, const char** rest = nullptr,
                         int base = 10) {
    p = skipSeparators(p);
    if (!p || !*p) return false;
    if (static_cast<unsigned char>(*p) <= ' ') return false;
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(p, &end, base);
    if (end == p || errno == ERANGE || parsed < INT32_MIN || parsed > INT32_MAX)
        return false;
    if (*end && !separator(*end)) return false;
    if (!rest && hasArgument(end)) return false;
    value = static_cast<int32_t>(parsed);
    if (rest) *rest = end;
    return true;
}

inline int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline bool parseDestination(const char* p, uint8_t out[16]) {
    p = skipSeparators(p);
    if (!p) return false;
    uint8_t parsed[16] = {};
    unsigned digits = 0;
    for (; *p; ++p) {
        const int digit = hexDigit(*p);
        if (digit < 0) {
            if (separator(*p) || *p == '-') continue;
            return false;
        }
        if (digits == 32) return false;
        auto& byte = parsed[digits / 2];
        byte = static_cast<uint8_t>((byte << 4) | digit);
        ++digits;
    }
    if (digits != 32) return false;
    std::memcpy(out, parsed, sizeof(parsed));
    return true;
}

// One byte at a time; an overlong or binary command is discarded THROUGH its
// newline. Its suffix can never turn into radio/settings commands.
class SerialCommand {
public:
    static constexpr unsigned Capacity = 128;
    enum class Result { None, Character, Line, Rejected };
    Result feed(char c) {
        if (_active || _discard) {
            if (c == '\r' || c == '\n') {
                const bool rejected = _discard;
                _line[_length] = '\0';
                _length = 0;
                _active = _discard = false;
                return rejected ? Result::Rejected : Result::Line;
            }
            if (_discard) return Result::None;
            if (_length + 1 >= Capacity || (static_cast<unsigned char>(c) < 32 && c != '\t')) {
                _discard = true;
                return Result::None;
            }
            _line[_length++] = c;
            return Result::None;
        }
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') return Result::None;
        if (c && std::strchr("FPLHJKYWCU", c)) {
            _line[0] = c;
            _length = 1;
            _active = true;
            return Result::None;
        }
        return Result::Character;
    }
    const char* line() const { return _line; }
    // A completed line is borrowed only through dispatch. Clear the whole
    // buffer, including rejected prefixes and older, longer credentials.
    void consume() {
        volatile char* bytes = _line;
        for (unsigned i = 0; i < Capacity; ++i) bytes[i] = 0;
        _length = 0;
        _active = _discard = false;
    }
private:
    char _line[Capacity] = {};
    unsigned _length = 0;
    bool _active = false;
    bool _discard = false;
};

class SampleWindow {
public:
    void toggle(uint32_t now) {
        _active = !_active;
        _start = _last = now;
    }
    bool due(uint32_t now) {
        if (!_active) return false;
        if (static_cast<uint32_t>(now - _start) >= 5000) {
            _active = false;
            return false;
        }
        if (static_cast<uint32_t>(now - _last) < 100) return false;
        _last = now;
        return true;
    }
    bool active() const { return _active; }
private:
    uint32_t _start = 0, _last = 0;
    bool _active = false;
};
} // namespace handheld::diagnostics
