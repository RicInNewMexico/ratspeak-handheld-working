#include "runtime/TaskOwner.h"
#include "TCPClientInterface.h"
#include "config/Config.h"

#include <WiFi.h>
#include <algorithm>
#include <lwip/sockets.h>
#include <cerrno>

uint8_t TCPClientInterface::_txBuffer[TX_BUFFER_SIZE] = {};
uint8_t TCPClientInterface::_wrapBuffer[WRAP_BUFFER_SIZE] = {};

TCPClientInterface::TCPClientInterface(const char* host, uint16_t port, const char* name)
    : _port(port)
{
    const size_t length = host ? strnlen(host, sizeof(_host)) : 0;
    _valid = length > 0 && length < sizeof(_host) && port != 0;
    if (_valid) memcpy(_host, host, length + 1);
    snprintf(_name, sizeof(_name), "%s", name ? name : "TCPClient");
}

TCPClientInterface::~TCPClientInterface() {
    stop();
    if (!reapConnectTask()) {
        Serial.printf("[TCP] Waiting for connect task before destroy: %s:%d\n",
                      _host, _port);
        waitForConnectTask();
    }
    if (_client.connected()) _client.stop();

}

bool TCPClientInterface::start() {
    handheld::assertDeviceOwner();
    if (!_valid) return false;
    _online = true;
    tryConnect();
    return true;
}

void TCPClientInterface::stop() {
    handheld::assertDeviceOwner();
    _online = false;
    if (!reapConnectTask()) {
        // The retiring owner polls canDestroy(); never block its timers while
        // the SDK connect helper owns the socket.
        Serial.printf("[TCP] Stop deferred while connect task exits for %s:%d\n",
                      _host, _port);
        return;
    }
    if (_client.connected()) {
        _client.stop();
        Serial.printf("[TCP] Disconnected from %s:%d\n", _host, _port);
    }
}

void TCPClientInterface::waitForConnectTask() {
    while (!reapConnectTask()) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool TCPClientInterface::reapConnectTask() {
    handheld::assertDeviceOwner();
    if (_connectState == CS_CONNECTING) return false;
    if (!_connectTask) return true;
    // IDF checks both running cores before reporting eSuspended. Only then,
    // on the task's affinity core, does vTaskDelete reclaim TLS/stack/TCB now.
    if (eTaskGetState(_connectTask) != eSuspended) return false;
    const BaseType_t affinity = xTaskGetAffinity(_connectTask);
    if (affinity != tskNO_AFFINITY && affinity != xPortGetCoreID()) return false;
    vTaskDelete(_connectTask);
    _connectTask = nullptr;
    // stop() may have raced the helper's last online check before publication.
    if (!_online) _client.stop();
    return true;
}

void TCPClientInterface::connectTaskFn(void* arg) {
    auto* self = static_cast<TCPClientInterface*>(arg);
    bool ok = self->_client.connect(self->_host, self->_port, TCP_CONNECT_TIMEOUT_MS);
    if (ok && !self->_online) {
        self->_client.stop();
        ok = false;
    }
    self->_connectState = ok ? CS_CONNECTED : CS_FAILED;
    // No self/owner access after terminal publication. The pinned owner keeps
    // this handle occupied until the kernel confirms suspension and reaps it.
    for (;;) vTaskSuspend(nullptr);
}

void TCPClientInterface::tryConnect() {
    if (!reapConnectTask()) return;
    if (_generation == UINT32_MAX) { _online = false; return; }
    ++_generation;
    _lastAttempt = millis();
    _connectState = CS_CONNECTING;
    Serial.printf("[TCP] Connecting to %s:%d (async)...\n", _host, _port);
    BaseType_t ok = xTaskCreatePinnedToCore(connectTaskFn, "tcpconn", 4096,
                                          this, 1, &_connectTask, xPortGetCoreID());
    if (ok != pdPASS) {
        _connectState = CS_IDLE;
        _connectTask = nullptr;
        _reconnectBackoff = std::min(_reconnectBackoff * 2, (unsigned long)300000);
        Serial.printf("[TCP] Failed to spawn connect task for %s:%d\n", _host, _port);
    }
}

void TCPClientInterface::loop(unsigned long budgetMs, size_t byteBudget) {
    handheld::assertDeviceOwner();
    if (!_online || budgetMs == 0 || byteBudget == 0) return;
    if (!reapConnectTask()) return;
    const unsigned long tcpStart = millis();

    if (_connectState == CS_CONNECTED) {
        _connectState = CS_IDLE;
        // Reset HDLC frame state and hub discovery for new connection
        _inFrame = false;
        _escaped = false;
        _rxPos = 0;
        _hubTransportIdKnown = false;
        _lastRxTime = millis();
        _reconnectBackoff = 1000;  // Reset backoff on success

        // RX uses available/read; TX uses one nonblocking socket send below.
        // This SDK's WiFiClient::setTimeout argument is seconds and its write()
        // has independent one-second retry waits, so neither bounds owner work.
        _client.setNoDelay(true);  // Disable Nagle — send immediately

        Serial.printf("[TCP] Connected to %s:%d\n", _host, _port);
    } else if (_connectState == CS_FAILED) {
        _connectState = CS_IDLE;
        // Exponential backoff: 1s → 2s → 4s → ... → 5min max, with jitter
        _reconnectBackoff = std::min(_reconnectBackoff * 2, (unsigned long)300000);
        _reconnectBackoff = std::min(300000UL,
            _reconnectBackoff + (unsigned long)random(_reconnectBackoff / 5));
        Serial.printf("[TCP] Failed to connect to %s:%d (next retry in %lus)\n",
                      _host, _port, _reconnectBackoff / 1000);
    }

    // WiFiClient is not safe to use from the loop while connectTaskFn owns it.
    if (_connectState == CS_CONNECTING) return;

    // Auto-reconnect with exponential backoff (only if WiFi is connected)
    if (!_client.connected()) {
        if (WiFi.status() != WL_CONNECTED) return;
        if (millis() - _lastAttempt >= _reconnectBackoff) {
            tryConnect();
        }
        return;
    }

    // Keepalive: if no RX for 5 minutes, force reconnect (NAT timeout detection)
    if (_lastRxTime > 0 && millis() - _lastRxTime >= TCP_KEEPALIVE_TIMEOUT_MS) {
        Serial.printf("[TCP] No RX for %lus, forcing reconnect to %s:%d\n",
                      (millis() - _lastRxTime) / 1000, _host, _port);
        _client.stop();
        _inFrame = false;
        _escaped = false;
        _rxPos = 0;
        return;  // Will reconnect on next loop iteration
    }

    // Drain incoming frames per loop (up to 15, time-boxed)
    for (int i = 0; i < 15 && byteBudget && _client.available() &&
         (millis() - tcpStart < budgetMs); i++) {
        unsigned long rxStart = millis();
        int len = readFrame(tcpStart, budgetMs, byteBudget);
        if (len > 0) {
            _lastRxTime = millis();
            _hubRxCount++;

            // Learn hub transport_id from incoming Header2 packets (once per connection)
            if (len >= 35) {
                uint8_t flags = _rxBuffer[0];
                uint8_t header_type = (flags >> 6) & 0x01;
                if (header_type == 1 && !_hubTransportIdKnown) {
                    memcpy(_hubTransportId, _rxBuffer + 2, 16);
                    _hubTransportIdKnown = true;
                    char hex[33];
                    for (int j = 0; j < 16; j++) snprintf(hex + j*2, sizeof(hex) - j*2, "%02x", _hubTransportId[j]);
                    Serial.printf("[TCP] Learned hub transport_id: %.8s\n", hex);
                }
            }

            Serial.printf("[TCP] RX %d bytes from %s:%d (%lums)\n",
                          len, _host, _port, millis() - rxStart);
            if (_rawSink) {
                _rawSink(_rxBuffer, (size_t)len);
            }
        } else {
            break;  // Incomplete frame, wait for more data
        }
    }
}

bool TCPClientInterface::send_outgoing(const uint8_t* rawData, size_t rawLen) {
    handheld::assertDeviceOwner();
    struct View { const uint8_t* p; size_t n; const uint8_t* data() const { return p; } size_t size() const { return n; } };
    View data{rawData, rawLen};
    if (!rawData || rawLen == 0 || rawLen > RX_BUFFER_SIZE) return false;
    if (!_online) {
        Serial.printf("[TCP] TX BLOCKED (offline) %d bytes to %s:%d\n", (int)data.size(), _host, _port);
        return false;
    }
    if (_connectState == CS_CONNECTING) {
        Serial.printf("[TCP] TX BLOCKED (connecting) %d bytes to %s:%d\n", (int)data.size(), _host, _port);
        return false;
    }
    if (!_client.connected()) {
        Serial.printf("[TCP] TX BLOCKED (disconnected) %d bytes to %s:%d\n", (int)data.size(), _host, _port);
        return false;
    }

    // Wrap Header1 non-announce packets as Header2 for TCP transport
    // (mirrors Rust actor.rs:653-678 — hub drops raw Header1 data packets).
    // Link packets are the exception: LRPROOF/LRRTT/link DATA are addressed
    // by link_id and must stay Header1 so the hub's link_table can route them
    // back along the link request path.
    if (_hubTransportIdKnown && data.size() >= 19) {
        uint8_t flags = data.data()[0];
        uint8_t header_type = (flags >> 6) & 0x01;
        uint8_t destination_type = (flags >> 2) & 0x03;
        uint8_t packet_type = flags & 0x03;
        bool link_packet = destination_type == 0x03;

        // Diagnostic: identify packet types going through TCP
        static const char* pt_names[] = {"DATA", "ANNOUNCE", "LINKREQ", "PROOF"};
        Serial.printf("[TCP-DIAG] send: %d bytes ht=%d dt=%d pt=%s(%d) to %s:%d\n",
            (int)data.size(), header_type, destination_type,
            (packet_type < 4) ? pt_names[packet_type] : "?", packet_type,
            _host, _port);
        if (packet_type == 0x03) {
            Serial.printf("[TCP-DIAG] *** PROOF packet being sent via TCP! ***\n");
        }

        if (packet_type != 0x01 && !link_packet) {  // Not ANNOUNCE or LINK traffic
            if (header_type == 0) {
                // Header1 → wrap as Header2 (handles hops==1, hops==0, unknown path)
                uint8_t new_flags = flags | 0x50;  // Set Header2 (bit 6) + Transport (bit 4)

                // Build Header2 packet: flags(1) + hops(1) + transport_id(16) + original[2:]
                size_t new_len = data.size() + 16;
                if (new_len > WRAP_BUFFER_SIZE) {
                    Serial.printf("[TCP] H1->H2 wrap too large (%d bytes), dropping\n", (int)new_len);
                    _txDropCount++;
                    return false;
                }
                _wrapBuffer[0] = new_flags;
                _wrapBuffer[1] = data.data()[1];  // hops
                memcpy(_wrapBuffer + 2, _hubTransportId, 16);  // transport_id
                memcpy(_wrapBuffer + 18, data.data() + 2, data.size() - 2);  // dest_hash + context + payload

                Serial.printf("[TCP] TX %d->%d bytes (H1->H2 wrap) to %s:%d\n",
                              (int)data.size(), (int)new_len, _host, _port);
                return sendFrame(_wrapBuffer, new_len);
            }
            else if (data.size() >= 35 && memcmp(data.data() + 2, _hubTransportId, 16) != 0) {
                // Header2 with wrong transport_id → fix it
                // Transport::outbound() may have used _received_from=destination_hash
                memcpy(_wrapBuffer, data.data(), data.size());
                memcpy(_wrapBuffer + 2, _hubTransportId, 16);

                Serial.printf("[TCP] TX %d bytes (H2 transport_id fixed) to %s:%d\n",
                              (int)data.size(), _host, _port);
                return sendFrame(_wrapBuffer, data.size());
            }
        }
    }

    if (!_hubTransportIdKnown) {
        bool accepted = sendFrame(data.data(), data.size());
        Serial.printf("[TCP] TX %d bytes (hub ID pending) to %s:%d\n",
                      (int)data.size(), _host, _port);
        return accepted;
    } else {
        // Passthrough: announces, correct Header2
        bool accepted = sendFrame(data.data(), data.size());
        Serial.printf("[TCP] TX %d bytes (passthrough) to %s:%d\n", (int)data.size(), _host, _port);
        return accepted;
    }
}

// HDLC-like framing: [0x7E] [escaped data] [0x7E]
// Buffered write — single syscall instead of per-byte writes
bool TCPClientInterface::sendFrame(const uint8_t* data, size_t len) {
    if (!data || len == 0 || len > WRAP_BUFFER_SIZE) return false;
    // Worst case: every byte escapes (2x) + 2 delimiters
    size_t maxFrameLen = len * 2 + 2;
    if (maxFrameLen > TX_BUFFER_SIZE) {
        Serial.printf("[TCP] TX frame too large (%d bytes), dropping\n", (int)len);
        _txDropCount++;
        return false;
    }
    size_t pos = 0;
    _txBuffer[pos++] = FRAME_START;
    for (size_t i = 0; i < len && pos < TX_BUFFER_SIZE - 2; i++) {
        if (data[i] == FRAME_START || data[i] == FRAME_ESC) {
            _txBuffer[pos++] = FRAME_ESC;
            _txBuffer[pos++] = data[i] ^ FRAME_XOR;
        } else {
            _txBuffer[pos++] = data[i];
        }
    }
    _txBuffer[pos++] = FRAME_START;
    const int written = ::send(_client.fd(), _txBuffer, pos, MSG_DONTWAIT);
    if (written != static_cast<int>(pos)) {
        _txDropCount++;
        // Backpressure with no accepted bytes leaves the socket usable. Any
        // partial frame must end this connection before another frame can be
        // appended; its missing delimiter prevents a truncated packet delivery.
        if (written >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
            _client.stop();
        Serial.printf("[TCP] TX short write (%d/%d bytes)\n", (int)written, (int)pos);
        return false;
    }
    // No flush() — TCP_NODELAY sends immediately without Nagle delay
    return true;
}

int TCPClientInterface::readFrame(unsigned long start, unsigned long budgetMs, size_t& bytesLeft) {
    if (!_client.available()) return 0;

    // Uses persistent member state: _inFrame, _escaped, _rxPos
    // This allows frames split across TCP segments to be reassembled correctly
    while (bytesLeft && millis() - start < budgetMs && _client.available()) {
        const int value = _client.read();
        if (value < 0) break;
        const uint8_t b = (uint8_t)value;
        --bytesLeft;

        if (b == FRAME_START) {
            if (_inFrame && _rxPos > 0) {
                // End of frame — return length, caller reads from _rxBuffer
                size_t frameLen = _rxPos;
                _inFrame = false;
                _escaped = false;
                _rxPos = 0;
                return frameLen;
            }
            _inFrame = true;
            _rxPos = 0;
            _escaped = false;
            continue;
        }

        if (!_inFrame) continue;

        if (b == FRAME_ESC) {
            _escaped = true;
            continue;
        }

        if (_rxPos == RX_BUFFER_SIZE) {
            // Discard the entire oversized frame, resynchronizing at a delimiter.
            _inFrame = false;
            _escaped = false;
            _rxPos = 0;
            continue;
        }
        if (_escaped) {
            _rxBuffer[_rxPos++] = b ^ FRAME_XOR;
            _escaped = false;
        } else {
            _rxBuffer[_rxPos++] = b;
        }
    }

    return 0;  // Incomplete frame — state preserved for next call
}
