#pragma once

#include "LXMFMessage.h"

namespace handheld {

// Temporary values shared by both presentation owners; no retained status cache.
struct StatusFacts {
    uint32_t revision;
    uint8_t desired, durable;
    bool pending, suppressed;
};

// A failed fresh projection cannot erase an already observed delivery proof.
// A newer persisted header still wins for durable state: a write admitted before
// that proof may complete after it. Call only for the same identity/peer/record,
// and only before the new row has received an explicit fresh projection.
inline StatusFacts retainKnownStatus(StatusFacts current, StatusFacts known) {
    if (known.revision >= current.revision) {
        current.desired = known.desired;
        current.durable = known.durable;
        current.pending = known.pending;
        current.suppressed = known.suppressed;
    } else if (known.desired == uint8_t(LXMFStatus::DELIVERED) &&
               current.desired != uint8_t(LXMFStatus::DELIVERED)) {
        current.desired = uint8_t(LXMFStatus::DELIVERED);
        current.pending = true;
        current.suppressed |= known.suppressed;
    }
    return current;
}

} // namespace handheld
