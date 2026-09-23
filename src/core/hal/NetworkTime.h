#pragma once

#include <Arduino.h>
#include "hal/ClockConfidence.h"
#include <esp_sntp.h>

namespace handheld {

inline void configureNetworkTime(const char* timezone) {
    ClockConfidence::networkSyncStarted();
    sntp_set_time_sync_notification_cb([](struct timeval* value) {
        // The lwIP callback follows a successful clock update; it never calls
        // protocol, storage, or UI owners from the network task.
        if (value) ClockConfidence::markSynchronized(value->tv_sec);
    });
    configTzTime(timezone, "pool.ntp.org", "time.nist.gov");
}

} // namespace handheld
