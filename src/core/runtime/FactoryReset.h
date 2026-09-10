#pragma once

#include <cstdint>

class FlashStore;
class SDStore;

namespace handheld {

enum class ResetStep : uint8_t {
    Complete, Intent, Scope, SDUnavailable, SD, NvsErase, NvsInit, NvsVerify, Internal, Finish
};
struct ResetResult {
    ResetStep step = ResetStep::Intent;
    bool ok() const { return step == ResetStep::Complete; }
    const char* detail() const;
};

// Only after MaintenanceBarrier's one-shot claim, or a fresh boot-recovery
// confirmation before any asynchronous/protocol owners have started.
ResetResult performFactoryReset(FlashStore& flash, SDStore& sd,
                               bool replaceInvalidScope = false);

} // namespace handheld
