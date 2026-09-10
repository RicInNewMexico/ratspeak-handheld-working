#pragma once
#include <cstdint>

namespace launcher {

enum class Choice : uint8_t { Standalone = 0, RNode = 1 };
constexpr uint8_t choiceValue(Choice choice) { return choice == Choice::RNode ? 1 : 0; }
constexpr Choice choiceFromValue(uint8_t value) { return value == 1 ? Choice::RNode : Choice::Standalone; }

// Policy only: board HAL, drawing, Preferences and OTA operations stay outside.
// Delivered key/pointer activity cancels the seven-second countdown for this
// session. Silent modifier hardware cannot be inferred by this policy.
// Navigation never confirms; errors require another explicit action to retry.
class SelectionState {
public:
    static constexpr uint32_t AutoBootMs = 7000;
    enum class Phase : uint8_t { Choosing, Booting, Error };
    enum class Result : uint8_t { None, TargetError, ChoiceNotSaved, Ready };
    void begin(Choice saved, uint32_t now) {
        _selected = saved;
        _started = now;
        _phase = Phase::Choosing;
        _result = Result::None;
        _autoBoot = true;
    }
    Choice selected() const { return _selected; }
    bool booting() const { return _phase == Phase::Booting; }
    bool autoBootEnabled() const { return _autoBoot; }
    Phase phase() const { return _phase; }
    Result result() const { return _result; }
    bool activity() {
        if (booting()) return false;
        const bool changed = _autoBoot || _phase == Phase::Error;
        _autoBoot = false;
        _phase = Phase::Choosing;
        return changed;
    }
    bool choose(Choice choice) {
        if (booting() || _selected == choice) return false;
        _selected = choice;
        return true;
    }
    uint32_t remainingSeconds(uint32_t now) const {
        const uint32_t elapsed = now - _started;
        return elapsed >= AutoBootMs ? 0 : (AutoBootMs - elapsed + 999) / 1000;
    }
    bool autoBootDue(uint32_t now) const {
        return _autoBoot && _phase == Phase::Choosing && remainingSeconds(now) == 0;
    }
    bool requestBoot(Choice choice) {
        if (booting()) return false;
        _selected = choice;
        _autoBoot = false;
        _phase = Phase::Booting;
        return true;
    }
    bool completeBoot(bool targetReady, bool choiceSaved) {
        if (!targetReady) {
            _phase = Phase::Error;
            _result = Result::TargetError;
            return false;
        }
        _result = choiceSaved ? Result::Ready : Result::ChoiceNotSaved;
        return true; // The valid target can boot even if preference saving failed.
    }
private:
    uint32_t _started = 0;
    Choice _selected = Choice::Standalone;
    Phase _phase = Phase::Choosing;
    Result _result = Result::None;
    bool _autoBoot = true;
};
static_assert(sizeof(SelectionState) <= 4096, "Approved launcher shared state ceiling");

} // namespace launcher
