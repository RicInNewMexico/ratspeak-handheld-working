#pragma once

#include <functional>
#include <string>
#include <vector>

#include "LXMFMessage.h"
#include "storage/MessageStore.h"

// Store-backed LXMF read surface. Wire
// protocol lives in the Rust FFI; send, queueing, links, and delivery proofs
// live in the backend engines behind the ProtocolBackend facade.
class LXMFManager {
public:
    struct CommittedMessage {
        handheld::storage::RecordKey key;
        uint32_t revision = 0;
    };
    using MessageCallback = std::function<void(const CommittedMessage&)>;
    using StatusCallback = std::function<void(const std::string& peerHex, double timestamp,
                                              uint32_t savedCounter, LXMFStatus status)>;

    bool beginStoreOnly(MessageStore* store);

    // Engine-owned queue state stays behind the ProtocolBackend facade.
    int queuedCount() const { return 0; }

    uint32_t storeRevision() const { return _store ? _store->revision() : 0; }
    uint32_t historyRevision() const { return _store ? _store->historyRevision() : 0; }
    handheld::storage::Submission requestHistoryPage(const std::string& peerHex,
        handheld::storage::HistoryEntry cursor, handheld::storage::HistoryDirection direction);
    handheld::storage::Submission requestRecord(const handheld::storage::RecordKey&, uint32_t offset, uint16_t capacity);
    handheld::storage::Submission requestConversationPage(handheld::storage::ConversationCursor, bool hasCursor,
        handheld::storage::ConversationOrder, handheld::storage::ConversationDirection, uint8_t limit);
    handheld::storage::Submission requestConversation(const handheld::storage::ConversationSelector&);
    bool readStoragePayload(handheld::storage::Ticket, void* bytes, size_t length) const;
    int unreadCount() const;
    handheld::storage::Submission requestMarkRead(const std::string& peerHex);
    handheld::storage::Submission requestDelete(const std::string& peerHex);
    bool pollStorageResult(handheld::storage::Ticket, handheld::storage::Result&) const;
    bool releaseStorageResult(handheld::storage::Ticket);

private:
    MessageStore* _store = nullptr;
};
