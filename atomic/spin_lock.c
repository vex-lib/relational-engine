// spin.c — SpinLock port (thread/SpinLock.java) on C23 atomics.
//
// One word: 0 = free, otherwise owner-encoded:
//     (threadId & 0x3FFFFFFF) << 1 | 1
// The low bit marks "held" so 0 is unambiguously free, and the owner id is
// embedded in the same word — one word means we can embed a lock directly in a
// RingBuffer header etc. without allocating a separate object.
//
// Ordering note (Lesson 20): on ARM the hardware reorders freely, so we use
// acquire on the successful CAS and release on the store in unlock. Relaxed
// would "work" on x86 and corrupt on Apple Silicon.

#include "atomic/atomic.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include "vex.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Spin
 * ============================================================================
 * SpinLock port (thread/SpinLock.java) on C23 atomics. The whole lock is one
 * word: 0 means free, any nonzero value is owner-encoded
 * ((threadId & 0x3FFFFFFF) << 1 | 1), so a lock embeds directly in a
 * RingBuffer header or any struct without a separate allocation. Acquire
 * uses a CAS with acquire ordering and release stores on unlock so
 * critical-section writes are visible to the next owner on ARM's relaxed
 * hardware. Unlock fails closed: only the owning thread may release.
 * tryLockTimeout bounds spinning to a caller deadline; isLocked reads the
 * word without taking the lock.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Spin (atomic/spin.c)
 * ============================================================================
 * spinlock API (Legacy: thread/SpinLock.java).
 *
 * STRUCT FIELDS (Mirroring atomic/atomic.h):
 * ----------------------------------------------------------------------------
 *   SpinLock {
 *     atomic_uint word; // atomic lock word
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - SpinLock_lock(lock)
 *   - SpinLock_tryLock(lock)
 *   - SpinLock_tryLockTimeout(lock, timeout_nanos)
 *   - SpinLock_unlock(lock)
 *
 * Getters:
 *   - SpinLock_isLocked(lock)
 * ============================================================================
 */


// x86 has `pause`; ARM64 has `yield`. Both tell the CPU "I'm spinning" so it
// can back off power/contention. This is the one place we're arch-specific.
#if defined(__aarch64__)
#define SPIN_PAUSE() asm volatile("yield" ::: "memory")
#else
#define SPIN_PAUSE() asm volatile("pause" ::: "memory")
#endif

// Encode a thread id into the lock word: low bit = held, id in the upper 31.
// Keeps the word 0 (free) unambiguous from any held value.
static u32 ticket(u64 thread_id) {
    return (u32)(((thread_id & 0x3FFFFFFFu) << 1) | 1u);
}

// Spin until acquired. The CAS publishes with acquire so our critical section
// sees everything the previous owner wrote before releasing.
fn SpinLock_lock(SpinLock *lock) {
    if (!lock)
        return;
    u32 expected = 0;
    u32 tk = ticket((u64) pthread_self());
    while (!atomic_compare_exchange_weak_explicit(&(*lock).word, &expected, tk,
                                                  memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
        for (int i = 0; i < 16; i++) {
            SPIN_PAUSE();
        }
    }
}

// One-shot acquire: returns immediately whether it worked or not.
bool SpinLock_tryLock(SpinLock *lock) {
    if (!lock)
        return false;
    u32 expected = 0;
    u32 tk = ticket((u64) pthread_self());
    return atomic_compare_exchange_strong_explicit(&(*lock).word, &expected, tk,
                                                   memory_order_acquire, memory_order_relaxed);
}

// Try until timeout_nanos elapses (or forever if negative). Never returns
// falsely-successful; the deadline is a hard stop.
bool SpinLock_tryLockTimeout(SpinLock *lock, int64_t timeout_nanos) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (!SpinLock_tryLock(lock)) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t elapsed = (now.tv_sec - start.tv_sec) * 1000000000LL
                        + (now.tv_nsec - start.tv_nsec);
        if (timeout_nanos >= 0 && elapsed >= timeout_nanos) return false;
        for (int i = 0; i < 16; i++) {
            SPIN_PAUSE();
        }
    }
    return true;
}

// Release with release semantics so our critical-section writes are visible
// to the next thread that acquires.
fn SpinLock_unlock(SpinLock *lock) {
    if (!lock)
        return;
    u32 cur = atomic_load_explicit(&(*lock).word, memory_order_acquire);
    if (cur == 0)
        return;
    if (cur != ticket((u64) pthread_self()))
        return;
    atomic_store_explicit(&(*lock).word, 0, memory_order_release);
}

// is_locked takes a const pointer, so cast away const for the atomic load.
bool SpinLock_isLocked(const SpinLock *lock) {
    if (!lock)
        return false;
    return atomic_load_explicit(&(*(SpinLock*) lock).word, memory_order_acquire) != 0;
}
