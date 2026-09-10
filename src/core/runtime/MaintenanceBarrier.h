#pragma once

#include <cstdint>
#include "storage/StorageContract.h"

namespace handheld {

// Owner-thread policy only. Adapters retain every actual command, storage
// ticket and driver. A reporting deadline never authorizes destroying them.
class MaintenanceBarrier {
public:
    static constexpr uint32_t DeadlineMs = 120000;
    enum class Phase : uint8_t { Running, Quiescing, Settling, Performing, Stopped, Failed };
    enum class Failure : uint8_t { None, Deadline, Owner, Operation };
    enum Action : uint8_t { None = 0, BeginHelpers = 1, StopStorage = 2, Perform = 4, ReportFailure = 8 };
    enum Owner : uint8_t { Normal = 1, Application = 2, Helpers = 4, Storage = 8 };
    struct Token {
        uint32_t id = 0;
        uint32_t generation = 0;
    };
    struct Snapshot {
        bool normalPending = false;
        bool applicationPending = false;
        // Includes an already-started radio burst as well as helper retirement.
        bool helpersPending = true;
        // Successful MessageStore::finishStop: exited worker AND all credits
        // consumed, never merely the worker's stopped flag.
        bool storageStopped = false;
        storage::Error error = storage::Error::None;
        bool unrecoverable = false; // Explicit owner declaration, never inferred from a storage error.
    };

    // The timestamp is captured at admission, before waiting for the owner.
    // Successful admission itself requires the adapter to close producers.
    // The adapter owns monotonically allocated IDs. This controller never resets.
    bool begin(Token token, uint32_t admittedAt) {
        if (_phase != Phase::Running || !token.id || !token.generation) return false;
        _token = token;
        _admittedAt = admittedAt;
        _phase = Phase::Quiescing;
        return true;
    }
    uint8_t step(uint32_t now, const Snapshot& snapshot) {
        if (_phase == Phase::Running || _phase == Phase::Performing || _phase == Phase::Stopped)
            return None;
        _pending = (snapshot.normalPending ? Normal : 0) |
            (snapshot.applicationPending ? Application : 0) |
            (snapshot.helpersPending ? Helpers : 0) |
            (!snapshot.storageStopped ? Storage : 0);
        if (snapshot.error != storage::Error::None) _lastError = snapshot.error;
        uint8_t actions = None;
        if (_phase != Phase::Failed &&
            (snapshot.unrecoverable || uint32_t(now - _admittedAt) >= DeadlineMs)) {
            _phase = Phase::Failed;
            _failure = snapshot.unrecoverable ? Failure::Owner : Failure::Deadline;
            _failedPending = _pending;
            actions |= ReportFailure;
        }
        // Safe settlement continues after terminal failure. No future snapshot
        // may revive the failed operation or grant its destructive action.
        if (snapshot.normalPending) return actions;
        if (!_helpersStarted) {
            _helpersStarted = true;
            if (_phase != Phase::Failed) _phase = Phase::Settling;
            return actions | BeginHelpers;
        }
        if (snapshot.applicationPending || snapshot.helpersPending) return actions;
        if (!_storageStopStarted) {
            _storageStopStarted = true;
            return actions | StopStorage;
        }
        if (snapshot.storageStopped && _phase != Phase::Failed) {
            // Claim before returning: callback reentry cannot perform twice.
            _phase = Phase::Performing;
            actions |= Perform;
        }
        return actions;
    }
    bool complete(Token token, bool success) {
        if (_phase != Phase::Performing || !matches(token)) return false;
        _phase = success ? Phase::Stopped : Phase::Failed;
        if (!success) _failure = Failure::Operation;
        return true;
    }
    bool claimOperation(Token token) {
        if (_phase != Phase::Performing || !matches(token) || _operationClaimed) return false;
        _operationClaimed = true;
        return true;
    }
    bool matches(Token token) const { return token.id == _token.id && token.generation == _token.generation; }
    bool accepting() const { return _phase == Phase::Running; }
    bool helpersStarted() const { return _helpersStarted; }
    bool storageStopStarted() const { return _storageStopStarted; }
    Phase phase() const { return _phase; }
    Failure failure() const { return _failure; }
    uint8_t pending() const { return _pending; }
    uint8_t failedPending() const { return _failedPending; }
    storage::Error lastError() const { return _lastError; }
    Token token() const { return _token; }

private:
    Token _token;
    uint32_t _admittedAt = 0;
    Phase _phase = Phase::Running;
    Failure _failure = Failure::None;
    storage::Error _lastError = storage::Error::None;
    uint8_t _pending = 0, _failedPending = 0;
    bool _helpersStarted = false, _storageStopStarted = false;
    bool _operationClaimed = false;
};

static_assert(sizeof(MaintenanceBarrier) <= 128, "Maintenance policy exceeds its fixed budget");

} // namespace handheld
