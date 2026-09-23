#pragma once

#include "ServiceMessages.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace handheld {

// One UI producer/consumer and one service consumer/producer. Payload ownership
// follows slot state: Free -> Ready -> Running -> Done -> Free. A terminal slot
// is reserved at admission, so completion never depends on a second queue.
class ServiceMailbox {
public:
    static constexpr size_t NormalSlots = 16;
    static constexpr size_t SlotCount = NormalSlots + 1;
    static constexpr size_t BlockSize = 1024;
    static constexpr size_t BlockCount = 32;
    static constexpr size_t ArenaSize = BlockSize * BlockCount;
    static constexpr size_t MaxPayload = 4 * BlockSize;
    static constexpr uint8_t NoSlot = 255;

    void begin(uint8_t* arena, size_t bytes) {
        _arena = bytes >= ArenaSize ? arena : nullptr;
        _accepting.store(_arena != nullptr, std::memory_order_release);
    }

    Admission submit(Request request, const void* data, size_t length,
                     size_t resultCapacity, uint32_t& id, uint32_t now = 0) {
        id = 0;
        if (!_arena) return Admission::NotReady;
        if (request.operation > Operation::ClearOldDataAndRestart) return Admission::Invalid;
        if (length > MaxPayload || resultCapacity > MaxPayload || (length && !data))
            return Admission::Invalid;
        if (!_accepting.load(std::memory_order_acquire) &&
            !(request.operation == Operation::Restart && _recovery.load(std::memory_order_acquire)))
            return Admission::NotReady;
        const bool lifecycle = lifecycleOperation(request.operation);
        const size_t first = lifecycle ? NormalSlots : 0;
        const size_t end = lifecycle ? SlotCount : NormalSlots;
        size_t index = first;
        while (index < end && _slots[index].state.load(std::memory_order_acquire) != State::Free)
            ++index;
        if (index == end) { ++_busy; return Admission::Busy; }
        auto& slot = _slots[index];
        const size_t capacity = length > resultCapacity ? length : resultCapacity;
        const uint8_t blocks = static_cast<uint8_t>((capacity + BlockSize - 1) / BlockSize);
        // Only the UI allocates/releases blocks. The worker accesses assigned
        // blocks exclusively until publishing Done.
        uint32_t selected = 0;
        uint8_t found = 0;
        for (uint8_t b = 0; b < BlockCount && found < blocks; ++b) {
            const uint32_t bit = uint32_t(1) << b;
            if (!(_usedBlocks & bit)) { selected |= bit; slot.blocks[found++] = b; }
        }
        if (found != blocks) { ++_busy; return Admission::Busy; }
        _usedBlocks |= selected;
        slot.blockMask = selected;
        slot.blockCount = blocks;
        slot.length = static_cast<uint16_t>(length);
        // A request ID is also the cancellation identity. Never recycle it.
        if (_nextId == UINT32_MAX) {
            _usedBlocks &= ~selected;
            return Admission::NotReady;
        }
        request.id = ++_nextId;
        request.admittedAt = now;
        slot.request = request;
        slot.result = {};
        copyIn(slot, 0, data, length);
        ++_outstanding;
        if (_outstanding > _peak) _peak = _outstanding;
        if (lifecycle) _accepting.store(false, std::memory_order_release);
        slot.state.store(State::Ready, std::memory_order_release);
        if (!lifecycle) {
            // Publication order, not a scan of reusable slots, defines FIFO.
            const auto tail = _readyTail.load(std::memory_order_relaxed);
            _readySlots[tail % NormalSlots] = static_cast<uint8_t>(index);
            _readyTail.store(tail + 1, std::memory_order_release);
        }
        id = request.id;
        return Admission::Admitted;
    }

    // Service owner: inspect the next immutable FIFO request without consuming
    // its credit. No later command may bypass one waiting for safe dispatch.
    const Request* nextRequest() const {
        const auto head = _readyHead.load(std::memory_order_relaxed);
        if (head == _readyTail.load(std::memory_order_acquire)) return nullptr;
        return &_slots[_readySlots[head % NormalSlots]].request;
    }

    // Close normal admission as soon as a lifecycle request is observed, then
    // finish older work before dispatching it. UI result consumption is not a
    // prerequisite for settling a durable mutation.
    uint8_t take() {
        if (_slots[NormalSlots].state.load(std::memory_order_acquire) == State::Ready)
            _accepting.store(false, std::memory_order_release);
        uint8_t chosen = NoSlot;
        const auto head = _readyHead.load(std::memory_order_relaxed);
        if (head != _readyTail.load(std::memory_order_acquire)) {
            chosen = _readySlots[head % NormalSlots];
            _readyHead.store(head + 1, std::memory_order_release);
        } else if (_slots[NormalSlots].state.load(std::memory_order_acquire) == State::Ready) {
            // The lifecycle publication synchronizes all earlier normal
            // admissions; recheck the queue before considering teardown.
            if (head != _readyTail.load(std::memory_order_acquire)) return NoSlot;
            bool running = false;
            for (uint8_t i = 0; i < NormalSlots; ++i)
                running |= runningState(_slots[i].state.load(std::memory_order_acquire));
            if (!running) chosen = NormalSlots;
        }
        if (chosen != NoSlot) {
            auto state = _slots[chosen].state.load(std::memory_order_acquire);
            while (!_slots[chosen].state.compare_exchange_weak(state,
                    state == State::CancelReady ? State::CancelRunning : State::Running,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {}
        }
        return chosen;
    }

    // UI owner: cancellation is checked against the immutable request ID and
    // is supported only before completion claims the Send slot. Ready sends
    // never reach the engine; Running sends still retain their result credit.
    bool cancelSend(uint32_t id) {
        if (!id) return false;
        for (uint8_t index = 0; index < NormalSlots; ++index) {
            auto& slot = _slots[index];
            auto state = slot.state.load(std::memory_order_acquire);
            if (state == State::Free || slot.request.id != id ||
                slot.request.operation != Operation::Send) continue;
            for (;;) {
                if (state == State::CancelReady || state == State::CancelRunning) return true;
                if (state != State::Ready && state != State::Running) return false;
                const auto next = state == State::Ready ? State::CancelReady : State::CancelRunning;
                if (slot.state.compare_exchange_weak(state, next,
                        std::memory_order_acq_rel, std::memory_order_acquire)) return true;
            }
        }
        return false;
    }

    bool cancellationRequested(uint8_t slot) const {
        if (slot >= NormalSlots) return false;
        const auto state = _slots[slot].state.load(std::memory_order_acquire);
        return state == State::CancelReady || state == State::CancelRunning;
    }
    bool lifecyclePending() const {
        const auto state = _slots[NormalSlots].state.load(std::memory_order_acquire);
        return state == State::Ready || runningState(state);
    }

    // Service owner: claim the reserved control request before ordinary FIFO
    // work settles. Its controller, not this claim, grants teardown permission.
    // Normal slot states, FIFO entries and unread Done results remain owned.
    uint8_t takeLifecycle() {
        auto expected = State::Ready;
        if (!_slots[NormalSlots].state.compare_exchange_strong(expected, State::Running,
                std::memory_order_acq_rel, std::memory_order_acquire)) return NoSlot;
        return NormalSlots;
    }
    bool normalWorkPending() const {
        for (uint8_t i = 0; i < NormalSlots; ++i) {
            const auto state = _slots[i].state.load(std::memory_order_acquire);
            if (state == State::Ready || state == State::CancelReady || runningState(state)) return true;
        }
        return false;
    }

    const Request& request(uint8_t slot) const { return _slots[slot].request; }
    size_t length(uint8_t slot) const { return _slots[slot].length; }
    size_t capacity(uint8_t slot) const { return _slots[slot].blockCount * BlockSize; }

    bool read(uint8_t slot, void* out, size_t length, size_t offset = 0) const {
        if (slot >= SlotCount || offset > capacity(slot) || length > capacity(slot) - offset ||
            (length && !out)) return false;
        const auto& s = _slots[slot];
        auto* dest = static_cast<uint8_t*>(out);
        while (length) {
            const size_t chunk = std::min(length, BlockSize - offset % BlockSize);
            std::memcpy(dest, _arena + s.blocks[offset / BlockSize] * BlockSize + offset % BlockSize, chunk);
            dest += chunk; offset += chunk; length -= chunk;
        }
        return true;
    }

    bool write(uint8_t slot, const void* data, size_t length, size_t offset = 0) {
        if (slot >= SlotCount || offset > capacity(slot) || length > capacity(slot) - offset ||
            (length && !data)) return false;
        copyIn(_slots[slot], offset, data, length);
        return true;
    }

    // Service owner: claim prevents a late UI cancellation from racing the
    // terminal result. The Send owner applies any claimed cancellation before
    // acknowledging its engine ticket and publishing the mailbox result.
    bool claimCompletion(uint8_t slot, bool& cancelled) {
        cancelled = false;
        if (slot >= SlotCount) return false;
        auto state = _slots[slot].state.load(std::memory_order_acquire);
        while (state == State::Running || state == State::CancelRunning) {
            if (_slots[slot].state.compare_exchange_weak(state, State::Completing,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                cancelled = state == State::CancelRunning;
                return true;
            }
        }
        return false;
    }
    void publishCompletion(uint8_t slot, Result result) {
        if (slot >= SlotCount || _slots[slot].state.load(std::memory_order_acquire) != State::Completing)
            return;
        if (result.length > capacity(slot)) { result = {}; result.outcome = Outcome::Invalid; }
        _slots[slot].result = result;
        _slots[slot].state.store(State::Done, std::memory_order_release);
    }
    void complete(uint8_t slot, Result result) {
        bool cancelled;
        if (claimCompletion(slot, cancelled)) publishCompletion(slot, result);
    }

    uint8_t nextResult() const {
        for (uint8_t i = 0; i < SlotCount; ++i)
            if (_slots[i].state.load(std::memory_order_acquire) == State::Done) return i;
        return NoSlot;
    }
    const Result& result(uint8_t slot) const { return _slots[slot].result; }

    void release(uint8_t slot) {
        if (slot >= SlotCount || _slots[slot].state.load(std::memory_order_acquire) != State::Done) return;
        // Erase credentials/message payloads before returning their blocks.
        for (uint8_t b = 0; b < _slots[slot].blockCount; ++b)
            std::memset(_arena + _slots[slot].blocks[b] * BlockSize, 0, BlockSize);
        _usedBlocks &= ~_slots[slot].blockMask;
        --_outstanding;
        _slots[slot].state.store(State::Free, std::memory_order_release);
    }

    void setAccepting(bool value) { _accepting.store(value && _arena, std::memory_order_release); }
    void allowRecovery() { _recovery.store(true, std::memory_order_release); }
    bool accepting() const { return _accepting.load(std::memory_order_acquire); }
    uint32_t busyCount() const { return _busy; } // UI-owned metrics
    uint8_t peak() const { return _peak; }
    uint8_t outstanding() const { return _outstanding; }

    bool publishStatus(const Status& status) {
        if (_statusLock.test_and_set(std::memory_order_acquire)) return false;
        _status = status;
        _statusLock.clear(std::memory_order_release);
        return true;
    }
    bool readStatus(Status& status) {
        if (_statusLock.test_and_set(std::memory_order_acquire)) return false;
        status = _status;
        _statusLock.clear(std::memory_order_release);
        return true;
    }

private:
    enum class State : uint8_t { Free, Ready, CancelReady, Running, CancelRunning, Completing, Done };
    static bool runningState(State state) {
        return state == State::Running || state == State::CancelRunning || state == State::Completing;
    }
    struct Slot {
        std::atomic<State> state{State::Free};
        Request request;
        Result result;
        uint32_t blockMask = 0;
        uint16_t length = 0;
        uint8_t blockCount = 0;
        uint8_t blocks[4] = {};
    };
    void copyIn(const Slot& s, size_t offset, const void* data, size_t length) {
        auto* source = static_cast<const uint8_t*>(data);
        while (length) {
            const size_t chunk = std::min(length, BlockSize - offset % BlockSize);
            std::memcpy(_arena + s.blocks[offset / BlockSize] * BlockSize + offset % BlockSize, source, chunk);
            source += chunk; offset += chunk; length -= chunk;
        }
    }
    Slot _slots[SlotCount];
    uint8_t _readySlots[NormalSlots] = {};
    std::atomic<uint32_t> _readyHead{0}, _readyTail{0};
    uint8_t* _arena = nullptr;
    std::atomic<bool> _accepting{false}, _recovery{false};
    uint32_t _usedBlocks = 0;
    uint32_t _nextId = 0;
    uint32_t _busy = 0;
    uint8_t _outstanding = 0;
    uint8_t _peak = 0;
    std::atomic_flag _statusLock = ATOMIC_FLAG_INIT;
    Status _status;
};

static_assert(sizeof(ServiceMailbox) + ServiceMailbox::ArenaSize <= 64 * 1024,
              "Service bridge exceeds its memory budget");

} // namespace handheld
