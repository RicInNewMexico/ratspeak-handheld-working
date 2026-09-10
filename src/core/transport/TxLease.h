#pragma once

#include <cstddef>
#include <cstdint>

namespace handheld {
struct TxReceipt {
    uint32_t generation = 0;
    uint8_t slot = UINT8_MAX;
    bool valid() const { return generation && slot != UINT8_MAX; }
};
enum class TxReceiptEvent : uint8_t { Validate, Started, Dropped };
// Validate is a read-only eligibility check. Terminal events may retire/reuse
// the receipt; drivers must finish releasing their queue entry before calling.
using TxReceiptHook = bool (*)(void*, TxReceipt, TxReceiptEvent);
enum class TxOffer : uint8_t { Rejected, Blocked, Queued, Started };

// Original Rust lifetime plus the host interface attachment/session generation.
// Process-local metadata: never serialize into a packet or a persisted record.
struct TxLease {
    static constexpr size_t TokenBytes = 104;
    uint8_t token[TokenBytes] = {};
    uint32_t generation = 0;
    uint32_t receiptGeneration = 0;
    uint8_t interfaceId = UINT8_MAX;
    uint8_t receiptSlot = UINT8_MAX;
    TxReceipt receipt() const { return {receiptGeneration, receiptSlot}; }
    void setReceipt(TxReceipt value) { receiptGeneration = value.generation; receiptSlot = value.slot; }
};
static_assert(sizeof(TxLease) == 116, "Review retained packet metadata budget");
}
