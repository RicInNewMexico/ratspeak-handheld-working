#pragma once

#include "LXMFMessage.h"

// Shared phase wording; these labels describe the existing owner status facts.
inline const char* messageStatusLabel(LXMFStatus status) {
    switch (status) {
        case LXMFStatus::QUEUED: return "queued";
        case LXMFStatus::SENDING: return "sending";
        case LXMFStatus::SENT: return "sent";
        case LXMFStatus::DELIVERED: return "delivered";
        case LXMFStatus::FAILED: return "failed";
        case LXMFStatus::UNCONFIRMED: return "unconfirmed";
        default: return "draft";
    }
}

// Shared transient caption; persisted/wire status values remain unchanged.
inline const char* messageStatusDetail(LXMFStatus status, bool pending,
        handheld::storage::Error error, bool suppressed) {
    if (pending)
        return error == handheld::storage::Error::None ? "saving status" : "save retry";
    if (suppressed && status != LXMFStatus::DELIVERED) return "not sending";
    if (error != handheld::storage::Error::None) return "storage error";
    return nullptr;
}
inline const char* messageStatusDetail(const LXMFMessage& message) {
    return messageStatusDetail(message.status, message.statusPending, message.statusError, message.txSuppressed);
}
