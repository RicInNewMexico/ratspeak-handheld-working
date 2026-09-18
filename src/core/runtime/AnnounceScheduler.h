#pragma once

#include <cstdint>
#include "config/AnnounceInterval.h"

namespace handheld {

// Single-owner cadence policy. begin() runs only after committed onboarding and
// runtime readiness. Manual announces bypass this owner; protocol retries and
// immutable wire packets remain owned by Runtime/transport. now is monotonic
// uint32 millis modulo wrap, observed at least once per full wrap period.
class AnnounceScheduler {
public:
    enum class Startup : uint8_t { ImmediateOnce, DelayedThree };
    enum class Phase : uint8_t { Dormant, Startup, Periodic, Disabled, Stopped };
    enum class Action : uint8_t { None, Startup, Periodic };
    enum class Result : uint8_t { Sent, Deferred, Failed, Skipped };
    struct Event {
        Action action = Action::None;
        Result result = Result::Failed; // Meaningful only when action != None.
        uint8_t attempt = 0; // Startup ordinal 1..3; zero for periodic/none.
    };
    static constexpr uint32_t StartupDelayMs = 5000;
    static constexpr uint16_t MinimumMinutes = announce::MinimumMinutes;
    static constexpr uint16_t MaximumMinutes = announce::MaximumMinutes;

    AnnounceScheduler() = default;
    AnnounceScheduler(const AnnounceScheduler&) = delete;
    AnnounceScheduler& operator=(const AnnounceScheduler&) = delete;

    bool begin(uint32_t now, Startup policy) {
        if (_phase != Phase::Dormant ||
            (policy != Startup::ImmediateOnce && policy != Startup::DelayedThree)) return false;
        _anchor = now;
        _policy = policy;
        _phase = Phase::Startup;
        return true;
    }

    // Permanent for this owner instance. A callback already entered can finish,
    // but its return cannot reopen scheduled admission after maintenance.
    void stop() { _phase = Phase::Stopped; }
    Phase phase() const { return _phase; }
    bool startupPending() const { return _phase == Phase::Startup; }
    uint32_t anchorMs() const { return _anchor; }

    // One synchronous callback at most. The adapter supplies the existing LoRa
    // online/utilization threshold as a fact; only periodic work is throttled.
    // Saved interval changes affect this due check, without a new timer or a
    // send from settings callbacks. A late poll never catches up in a burst.
    template<class Attempt>
    Event poll(uint32_t now, uint16_t savedMinutes, bool airtimeBlocked, Attempt&& attempt) {
        if (_attempting || _phase == Phase::Dormant || _phase == Phase::Stopped) return {};
        if (savedMinutes == announce::Off) {
            _phase = Phase::Disabled;
            return {};
        }
        if (_phase == Phase::Disabled) {
            // Re-enabling starts a full interval, without a startup/catch-up burst.
            _phase = Phase::Periodic;
            _anchor = now;
            return {};
        }
        const bool startup = _phase == Phase::Startup;
        const uint32_t delay = startup
            ? (_policy == Startup::ImmediateOnce ? 0 : StartupDelayMs)
            : intervalMs(savedMinutes);
        if (uint32_t(now - _anchor) < delay) return {};

        // Claim cadence before any callback. Zero is a valid anchor, and a
        // repeated/reentrant poll cannot claim this same scheduling obligation.
        _anchor = now;
        Event event;
        event.action = startup ? Action::Startup : Action::Periodic;
        if (startup) {
            event.attempt = ++_attempts;
            const uint8_t maximum = _policy == Startup::ImmediateOnce ? 1 : 3;
            if (_attempts == maximum) _phase = Phase::Periodic;
        } else if (airtimeBlocked) {
            event.result = Result::Skipped;
            return event;
        }

        _attempting = true;
        struct AttemptGuard {
            bool& active;
            ~AttemptGuard() { active = false; }
        } guard{_attempting};
        event.result = attempt(event.action, event.attempt);
        if (_phase != Phase::Stopped && startup &&
            (event.result == Result::Sent || event.result == Result::Deferred))
            _phase = Phase::Periodic;
        return event;
    }

private:
    static uint32_t intervalMs(uint16_t minutes) {
        return uint32_t(announce::normalizeMinutes(minutes)) * 60000u;
    }
    uint32_t _anchor = 0;
    Phase _phase = Phase::Dormant;
    Startup _policy = Startup::DelayedThree;
    uint8_t _attempts = 0;
    bool _attempting = false;
};

static_assert(sizeof(AnnounceScheduler) <= 8, "Review announce scheduler fixed owner budget");

} // namespace handheld
