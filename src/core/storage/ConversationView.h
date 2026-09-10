#pragma once

#include "StorageContract.h"
#include <cmath>
#include <cstring>

namespace handheld::storage {

// Ordering is applied across persisted peers before selecting a bounded page.
// Recent preserves fractional timestamps and uses the peer as its tie-breaker.

struct ConversationCursor {
    double timestamp = 0;
    uint8_t peer[16] = {};
};

// A selector binds the preview to the particular latest record observed by the
// page query. Later arrivals may change counts, but cannot silently substitute
// a different message body/time while retaining this selector's sort position.
struct ConversationSelector {
    ConversationCursor cursor;
    uint32_t counter = 0;
    uint8_t incoming = 0;
    Error error = Error::None;
    uint16_t reserved = 0;
};

// One bounded presentation row; no names, avatars or whole message strings.
// Two banks of these rows plus their controls fit the adopted 160 bytes/peer.
// Status and durableStatus are distinct so an unsaved proof remains truthful.
struct ConversationView {
    double timestamp = 0;
    uint8_t peer[16] = {};
    char preview[23] = {};
    uint8_t status = 0;
    uint32_t unreadCount = 0, totalCount = 0, lastOutgoingCounter = 0;
    uint32_t outgoingRevision = 0;
    uint16_t pendingCount = 0, failedCount = 0;
    uint8_t durableStatus = 0, flags = 0, previewLength = 0;
    Error error = Error::None;

    enum Flag : uint8_t {
        LastIncoming = 1, HasOutgoing = 2, PreviewTruncated = 4, Unavailable = 8,
        StatusPending = 16, TxSuppressed = 32, StatusUnavailable = 64,
        StatusFresh = 128
    };
};

inline bool conversationLess(const ConversationCursor& a, const ConversationCursor& b,
                             ConversationOrder order) {
    if (order == ConversationOrder::Recent && a.timestamp != b.timestamp)
        return a.timestamp > b.timestamp;
    return std::memcmp(a.peer, b.peer, sizeof(a.peer)) < 0;
}
inline bool conversationEqual(const ConversationCursor& a, const ConversationCursor& b,
                              ConversationOrder order) {
    return (order == ConversationOrder::Peer || a.timestamp == b.timestamp) &&
        !std::memcmp(a.peer, b.peer, sizeof(a.peer));
}
inline bool validConversationOrder(ConversationOrder order) {
    return order == ConversationOrder::Recent || order == ConversationOrder::Peer;
}
inline bool validConversationDirection(ConversationDirection direction) {
    return direction == ConversationDirection::Before || direction == ConversationDirection::After;
}
inline bool validConversationSelector(const ConversationSelector& selector) {
    return std::isfinite(selector.cursor.timestamp) && selector.incoming <= 1 &&
        selector.error <= Error::Internal && !selector.reserved &&
        (selector.counter || selector.error != Error::None);
}

static_assert(sizeof(ConversationCursor) == 24, "Conversation cursor must remain bounded");
static_assert(sizeof(ConversationSelector) == 32, "Sixteen selectors must fit a 512-byte storage credit");
static_assert(sizeof(ConversationView) == 72, "Two summary banks must fit the adopted owner budget");
static_assert(std::is_trivially_copyable<ConversationSelector>::value &&
              std::is_trivially_copyable<ConversationView>::value, "Conversation IPC uses copied values");

} // namespace handheld::storage
