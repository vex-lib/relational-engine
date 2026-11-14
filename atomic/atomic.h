#ifndef ATOMIC_ATOMIC_H
#define ATOMIC_ATOMIC_H



// atomic/atomic.h — spinlock API (Legacy: thread/SpinLock.java).
//
// A single atomic word: 0 = free, else (threadId<<1)|1 (owner-encoded). One
// word means the lock can be embedded inside other structs (ring buffers,
// pool headers) with no separate allocation.
//
// Ordering: the successful CAS is memory_order_acquire, unlock is
// memory_order_release. On ARM the hardware reorders, so relaxed would be
// wrong (see Lesson 20).

#include "vex.h"

;;CLASSNAME("SpinLock")
typedef struct SpinLock {
    atomic_uint word;
} SpinLock;

// Zero-initialized lock usable as a field initializer / global.
#define SPIN_LOCK_INIT ((SpinLock){ .word = 0 })

fn SpinLock_lock(SpinLock *lock);

bool SpinLock_tryLock(SpinLock *lock);

bool SpinLock_tryLockTimeout(SpinLock *lock, int64_t timeout_nanos);

fn SpinLock_unlock(SpinLock *lock);

bool SpinLock_isLocked(const SpinLock *lock);

#endif
