#pragma once

#include "config/Config.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>
#include <atomic>
#include <cstddef>

class TCPClientInterface {
public:
    TCPClientInterface(const char* host, uint16_t port, const char* name);
    ~TCPClientInterface();

    bool start();
    void stop();
    // Caller supplies the remaining round allowance. Byte and frame caps also
    // bound work when the clock does not advance. Partial RX is connection-owned.
    void loop(unsigned long budgetMs = 25, size_t byteBudget = 1024);

    bool isConnected() {
        if (!reapConnectTask()) return false;
        return _client.connected();
    }
    bool canDestroy() { return reapConnectTask(); }
    uint32_t generation() const { return _generation; }
    const char* host() const { return _host; }
    uint16_t port() const { return _port; }

    // Raw-frame seam for the backend pump: HDLC-deframed RX frames go to
    // the sink; TX keeps the Header2 hub-wrap + HDLC framing path unchanged.
    using RawSink = std::function<void(const uint8_t* data, size_t len)>;
    void setRawSink(RawSink sink) { _rawSink = sink; }
    bool sendRaw(const uint8_t* data, size_t len) { return send_outgoing(data, len); }

private:
    bool send_outgoing(const uint8_t* data, size_t len);
    void tryConnect();
    bool sendFrame(const uint8_t* data, size_t len);
    int readFrame(unsigned long start, unsigned long budgetMs, size_t& bytesLeft);
    static void connectTaskFn(void* arg);
    bool reapConnectTask();
    void waitForConnectTask();

    // WiFiClient::connect() blocks. Run it in a one-shot task and let the
    // owner reclaim the permanently suspended task before claiming its result.
    enum ConnectState : uint8_t {
        CS_IDLE       = 0,
        CS_CONNECTING = 1,
        CS_CONNECTED  = 2,
        CS_FAILED     = 3,
    };
    std::atomic<ConnectState> _connectState{CS_IDLE};
    TaskHandle_t _connectTask = nullptr;

    RawSink _rawSink;
    char _name[32] = {};
    bool _valid = false;
    std::atomic<bool> _online{false};
    uint32_t _generation = 0; // owner-only; advances before every socket attempt

    WiFiClient _client;
    char _host[254] = {};
    uint16_t _port;
    unsigned long _lastAttempt = 0;
    unsigned long _lastRxTime = 0;
    // The Rust pump accepts a 500-byte RNS frame. Each connection owns its
    // partial frame; only synchronous TX scratch is shared by the protocol owner.
    static constexpr size_t RX_BUFFER_SIZE = 500;
    static constexpr size_t WRAP_BUFFER_SIZE = RX_BUFFER_SIZE + 16;
    static constexpr size_t TX_BUFFER_SIZE = WRAP_BUFFER_SIZE * 2 + 2;
    uint8_t _rxBuffer[RX_BUFFER_SIZE] = {};
    static uint8_t _txBuffer[TX_BUFFER_SIZE];
    static uint8_t _wrapBuffer[WRAP_BUFFER_SIZE];

    // Exponential reconnect backoff: 1s → ×2 (+0-20% jitter) → 5min cap
    unsigned long _reconnectBackoff = 1000;

    // Hub transport_id for Header2 wrapping (learned from incoming Header2 packets)
    uint8_t _hubTransportId[16] = {};
    bool _hubTransportIdKnown = false;

    // Telemetry counters
    unsigned long _hubRxCount = 0;
    unsigned long _txDropCount = 0;

    // Persistent HDLC frame reassembly state (survives across loop() calls)
    bool _inFrame = false;
    bool _escaped = false;
    size_t _rxPos = 0;

    static constexpr uint8_t FRAME_START = 0x7E;
    static constexpr uint8_t FRAME_ESC   = 0x7D;
    static constexpr uint8_t FRAME_XOR   = 0x20;
    static constexpr unsigned long TCP_KEEPALIVE_TIMEOUT_MS = 300000; // 5 min

public:
    unsigned long lastRxTime() const { return _lastRxTime; }
    unsigned long hubRxCount() const { return _hubRxCount; }
};
