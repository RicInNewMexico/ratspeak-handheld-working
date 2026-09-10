#include "reticulum/LXMFManager.h"

#include <Arduino.h>

bool LXMFManager::beginStoreOnly(MessageStore* store) {
    _store = store;
    Serial.println("[LXMF] Manager started (store-only: reads via MessageStore, send via backend)");
    return true;
}

handheld::storage::Submission LXMFManager::requestHistoryPage(const std::string& peerHex,
        handheld::storage::HistoryEntry cursor, handheld::storage::HistoryDirection direction) {
    return _store ? _store->requestHistoryPage(peerHex, cursor, 48, direction) : handheld::storage::Submission{};
}

handheld::storage::Submission LXMFManager::requestRecord(const handheld::storage::RecordKey& key,
                                                       uint32_t offset, uint16_t capacity) {
    return _store ? _store->requestRecord(key, offset, capacity) : handheld::storage::Submission{};
}

handheld::storage::Submission LXMFManager::requestConversationPage(handheld::storage::ConversationCursor cursor,
        bool hasCursor, handheld::storage::ConversationOrder order,
        handheld::storage::ConversationDirection direction, uint8_t limit) {
    return _store ? _store->requestConversationPage(cursor, hasCursor, order, direction, limit) : handheld::storage::Submission{};
}

handheld::storage::Submission LXMFManager::requestConversation(const handheld::storage::ConversationSelector& selector) {
    return _store ? _store->requestConversation(selector) : handheld::storage::Submission{};
}

bool LXMFManager::readStoragePayload(handheld::storage::Ticket ticket, void* bytes, size_t length) const {
    return _store && _store->readPayload(ticket, bytes, length);
}

int LXMFManager::unreadCount() const {
    return _store ? _store->totalUnreadCount() : 0;
}

handheld::storage::Submission LXMFManager::requestMarkRead(const std::string& peerHex) {
    return _store ? _store->requestMarkRead(peerHex) : handheld::storage::Submission{};
}

handheld::storage::Submission LXMFManager::requestDelete(const std::string& peerHex) {
    return _store ? _store->requestDelete(peerHex) : handheld::storage::Submission{};
}

bool LXMFManager::pollStorageResult(handheld::storage::Ticket ticket,
                                  handheld::storage::Result& result) const {
    return _store && _store->peekResult(ticket, result);
}

bool LXMFManager::releaseStorageResult(handheld::storage::Ticket ticket) {
    return _store && _store->releaseResult(ticket);
}
