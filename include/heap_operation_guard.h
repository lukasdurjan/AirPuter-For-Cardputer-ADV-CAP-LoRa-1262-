#pragma once

namespace HeapOperationGuard {

// Serializes operations which need a large contiguous internal-RAM block.
// Network/TLS work may block; UI rendering must always use a non-blocking lock.
void begin();

class Lock {
public:
    explicit Lock(bool wait = true);
    ~Lock();
    explicit operator bool() const { return acquired_; }

    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    bool acquired_ = false;
};

}
