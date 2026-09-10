#pragma once

#include "Screen.h"
#include "widgets/TextInput.h"
#include "reticulum/LXMFManager.h"
#include "protocol/OutgoingContract.h"
#include "history/HistoryWindow.h"
#include <string>

class AnnounceManager;
class ProtocolBackend;

class MessageView : public Screen {
public:
    void render(M5Canvas& canvas) override;
    bool handleKey(const KeyEvent& event) override;
    const char* title() const override { return "Chat"; }
    void onEnter() override;
    void onExit() override { _visible = false; _readRequested = false; _history.close(); }

    void setLXMFManager(LXMFManager* lxmf) { _lxmf = lxmf; }
    void setBackend(ProtocolBackend* backend) { _backend = backend; }
    void setAnnounceManager(AnnounceManager* am) { _am = am; }
    void setPeerHex(const std::string& peerHex) { _peerHex = peerHex; }
    void notifyNewMessage(const handheld::storage::RecordKey&);
    // App-owned polling continues while this screen is hidden or powered off.
    bool pollSubmission();
    bool pollReadMarker();
    bool pollHistory(bool allowAdmission = true);

    // Status callback — update chat line color when send completes
    void notifyStatusChange(const std::string& peerHex, uint32_t counter, LXMFStatus status);

    // Callback to return to messages list
    using BackCallback = std::function<void()>;
    void setBackCallback(BackCallback cb) { _backCb = cb; }

    // Callback to update unread badge after markRead
    using UnreadUpdateCb = std::function<void()>;
    void setUnreadUpdateCallback(UnreadUpdateCb cb) { _unreadCb = cb; }

private:
    void refreshMessages();
    void sendCurrentInput();
    bool prepareDraft(String& identity);
    static constexpr size_t DraftLength = 400;

    LXMFManager* _lxmf = nullptr;
    ProtocolBackend* _backend = nullptr;
    AnnounceManager* _am = nullptr;
    std::string _peerHex;
    handheld::history::HistoryWindow _history;
    TextInput _input;
    BackCallback _backCb;
    UnreadUpdateCb _unreadCb;
    bool _needsRefresh = false;

    handheld::outgoing::Ticket _sendTicket;
    uint64_t _submittedRevision = 0;
    bool _submissionEdited = false;
    bool _draftReady = false;
    std::string _retainedDraft, _retainedPeer, _retainedIdentity;
    const char* _sendNotice = nullptr;
    uint32_t _sendNoticeSince = 0;
    handheld::storage::Ticket _readTicket;
    char _readPeer[33] = {};
    uint32_t _readRetryAt = 0;
    bool _visible = false, _readRequested = false;
};
