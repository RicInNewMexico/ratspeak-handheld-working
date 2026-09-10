#pragma once
#ifdef PROTOCOL_PACKET_TRACE
#include "SerialCommand.h"

namespace handheld::diagnostics {
struct TraceCommand {
    char address[96] = {};
    char password[64] = {};
    uint16_t port = 0;
    bool wifi = false;

    bool parse(const char* line) {
        if (!line || (line[0] != 'W' && line[0] != 'C')) return false;
        const char* split = std::strchr(line + 1, '|');
        if (!split) return false;
        wifi = line[0] == 'W';
        const size_t length = split - (line + 1);
        if (!length || length > (wifi ? 32u : sizeof(address) - 1)) return false;
        if (wifi) {
            const size_t passLength = std::strlen(split + 1);
            if (passLength >= sizeof(password)) return false;
            std::memcpy(password, split + 1, passLength + 1);
        } else {
            int32_t parsed = 0;
            if (!parseInteger(split + 1, parsed) || parsed < 1 || parsed > UINT16_MAX) return false;
            port = static_cast<uint16_t>(parsed);
        }
        std::memcpy(address, line + 1, length);
        address[length] = '\0';
        return true;
    }
};
}
#endif
