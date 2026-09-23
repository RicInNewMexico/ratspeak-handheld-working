#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace handheld::diagnostics {

enum class RemoteUiAction : uint8_t { View, Key, Character, Hold };
enum class RemoteUiKey : uint8_t { Up, Down, Left, Right, Enter, Backspace, Escape, Tab };
struct RemoteUiRequest {
    uint32_t id = 0;
    RemoteUiAction action = RemoteUiAction::View;
    RemoteUiKey key = RemoteUiKey::Enter;
    uint16_t offset = 0;
    char character = 0;
    bool ctrl = false;
};

// Deliberately narrower than the legacy radio diagnostics grammar. USB UI
// requests are printable ASCII tokens; malformed suffixes never become input.
inline bool parseRemoteUi(const char* line, RemoteUiRequest& request) {
    request = {};
    if (!line || *line++ != 'U' || *line++ != ' ') return false;
    const auto spaces = [&line]() { while (*line == ' ' || *line == '\t') ++line; };
    const auto number = [&line, &spaces](uint32_t& out, uint32_t limit) {
        spaces();
        if (*line < '0' || *line > '9') return false;
        out = 0;
        do {
            const auto digit = static_cast<uint32_t>(*line++ - '0');
            if (out > (limit - digit) / 10) return false;
            out = out * 10 + digit;
        } while (*line >= '0' && *line <= '9');
        return *line == '\0' || *line == ' ' || *line == '\t';
    };
    const auto token = [&line, &spaces](const char* word) {
        spaces();
        const size_t n = std::strlen(word);
        if (std::strncmp(line, word, n) || (line[n] && line[n] != ' ' && line[n] != '\t')) return false;
        line += n;
        return true;
    };
    if (!number(request.id, INT32_MAX) || !request.id) { request.id = 0; return false; }
    if (token("view")) {
        spaces();
        if (*line) {
            uint32_t offset = 0;
            if (!number(offset, 511)) return false;
            request.offset = static_cast<uint16_t>(offset);
        }
    } else if (token("hold")) {
        request.action = RemoteUiAction::Hold;
    } else if (token("key")) {
        request.action = RemoteUiAction::Key;
        static constexpr const char* names[] = {"up", "down", "left", "right", "enter", "backspace", "escape", "tab"};
        bool found = false;
        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            if (!token(names[i])) continue;
            request.key = static_cast<RemoteUiKey>(i);
            found = true;
            break;
        }
        if (!found) return false;
    } else if (token("char")) {
        request.action = RemoteUiAction::Character;
        uint32_t character = 0;
        if (!number(character, 126) || character < 32) return false;
        request.character = static_cast<char>(character);
        spaces();
        if (*line) {
            if (!token("ctrl")) return false;
            request.ctrl = true;
        }
    } else return false;
    spaces();
    return *line == '\0';
}

// No dynamic allocation, including escaping. A failed append leaves a valid
// prefix so callers can reserve a fixed tail before adding variable records.
class RemoteUiJson {
public:
    RemoteUiJson(char* data, size_t capacity) : _data(data), _capacity(capacity) {
        if (_capacity) _data[0] = '\0';
    }
    bool append(const char* text) {
        const size_t n = std::strlen(text);
        if (n >= remaining()) return false;
        std::memcpy(_data + _length, text, n + 1);
        _length += n;
        return true;
    }
    bool number(int32_t value) {
        char digits[16];
        std::snprintf(digits, sizeof(digits), "%ld", static_cast<long>(value));
        return append(digits);
    }
    // Bounds UTF-8 bytes without cutting a multi-byte character. JSON controls
    // and quotes are escaped; valid UTF-8 is retained for UI names and symbols.
    bool string(const char* text, size_t limit = 96) {
        if (!text) text = "";
        size_t bytes = 0;
        while (bytes < limit && text[bytes]) ++bytes;
        const bool truncated = text[bytes] != '\0';
        if (truncated) while (bytes && (static_cast<unsigned char>(text[bytes]) & 0xc0) == 0x80) --bytes;
        append("\"");
        for (size_t i = 0; i < bytes; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            char escaped[7] = {};
            if (c == '"' || c == '\\') { escaped[0] = '\\'; escaped[1] = static_cast<char>(c); }
            else if (c < 32) std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
            else escaped[0] = static_cast<char>(c);
            append(escaped);
        }
        append("\"");
        return truncated;
    }
    size_t size() const { return _length; }
    size_t remaining() const { return _capacity - _length; }
private:
    char* _data;
    size_t _capacity;
    size_t _length = 0;
};

// One producer (Service), one consumer (UI). The result buffer is written only
// while Executing and read only while Ready. Admission closes permanently for
// this boot during maintenance. Pending work is settled without UI execution.
class RemoteUiBridge {
public:
    static constexpr size_t Capacity = 2048;
    enum class Admission : uint8_t { Accepted, Busy, Unavailable };
    Admission submit(const RemoteUiRequest& request) {
        if (!_accepting.load(std::memory_order_acquire)) return Admission::Unavailable;
        if (_state.load(std::memory_order_acquire) != State::Empty) return Admission::Busy;
        _request = request;
        _state.store(State::Pending, std::memory_order_release);
        return Admission::Accepted;
    }
    bool take(RemoteUiRequest& request) {
        State pending = State::Pending;
        if (!_state.compare_exchange_strong(pending, State::Executing, std::memory_order_acq_rel)) return false;
        request = _request;
        return true;
    }
    bool accepting() const { return _accepting.load(std::memory_order_acquire); }
    void close() {
        _accepting.store(false, std::memory_order_release);
        State pending = State::Pending;
        if (_state.compare_exchange_strong(pending, State::Executing, std::memory_order_acq_rel))
            fail(_request.id, "unavailable");
    }
    char* buffer() { return _buffer; }
    void finish(size_t length) {
        _length = length;
        _state.store(State::Ready, std::memory_order_release);
    }
    void fail(uint32_t id, const char* error) {
        const int n = std::snprintf(_buffer, sizeof(_buffer),
            "[UICTRL] {\"id\":%lu,\"ok\":false,\"error\":\"%s\"}\n", static_cast<unsigned long>(id), error);
        finish(n > 0 && static_cast<size_t>(n) < sizeof(_buffer) ? static_cast<size_t>(n) : 0);
    }
    bool result(const char*& data, size_t& length) const {
        if (_state.load(std::memory_order_acquire) != State::Ready) return false;
        data = _buffer;
        length = _length;
        return true;
    }
    void consumed() { _state.store(State::Empty, std::memory_order_release); }
    bool drained() const { return _state.load(std::memory_order_acquire) == State::Empty; }
private:
    enum class State : uint8_t { Empty, Pending, Executing, Ready };
    std::atomic<State> _state{State::Empty};
    std::atomic<bool> _accepting{true};
    RemoteUiRequest _request;
    char _buffer[Capacity] = {};
    size_t _length = 0;
};

// Service-owner reply settlement. Native HWCDC's disconnected write fallback
// can discard bytes while reporting success, so connection and whole-frame
// queue capacity are prerequisites, not a reason to retry an input action.
class RemoteUiReplyDelivery {
public:
    static constexpr size_t TxCapacity = RemoteUiBridge::Capacity + 1024;
    static constexpr size_t LogHeadroom = 512;
    static constexpr uint32_t DeadlineMs = 1000;
    static constexpr uint32_t WriteTimeoutMs = 5;
    enum class Result { Idle, Waiting, Sent, Dropped };

    template<class Writer>
    Result poll(RemoteUiBridge& bridge, uint32_t now, bool connected,
                size_t available, Writer&& write) {
        const char* data = nullptr;
        size_t length = 0;
        if (!bridge.result(data, length)) {
            _waiting = false;
            return Result::Idle;
        }
        if (!_waiting) { _started = now; _waiting = true; }
        if (!length || length >= RemoteUiBridge::Capacity ||
            static_cast<uint32_t>(now - _started) >= DeadlineMs) {
            bridge.consumed();
            _waiting = false;
            return Result::Dropped;
        }
        if (!connected || available < length + LogHeadroom) return Result::Waiting;

        const size_t written = write(data, length);
        if (!written) return Result::Waiting;
        // A partial frame is ambiguous. Never append a suffix amid other logs,
        // repeat the frame, or repeat its input; the host must inspect again.
        bridge.consumed();
        _waiting = false;
        return written == length ? Result::Sent : Result::Dropped;
    }
private:
    uint32_t _started = 0;
    bool _waiting = false;
};

} // namespace handheld::diagnostics
