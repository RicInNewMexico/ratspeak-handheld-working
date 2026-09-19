#pragma once
#include <stdint.h>

namespace handheld::network_timing {
// These waits run in asynchronous owners; they do not block UI polling.
constexpr uint32_t WifiConnectMs = 30000;
constexpr uint32_t WifiScanMs = 15000;
constexpr uint32_t ReconnectMaxMs = 60000;
constexpr uint32_t AutoInitialMs = 1500;
constexpr uint32_t AutoPollMs = 2000;
constexpr uint32_t AutoRetryMs = 5000;
constexpr uint32_t AutoRetryMaxMs = 30000;
constexpr uint32_t AutoNoticeMs = 10000;
// TCP probes are acknowledged by the peer's stack even on a quiet network.
constexpr int TcpKeepIdleSeconds = 60;
constexpr int TcpKeepIntervalSeconds = 15;
constexpr int TcpKeepCount = 4;
} // namespace handheld::network_timing
