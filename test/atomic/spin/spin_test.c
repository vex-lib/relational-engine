// vexspoke/test/atomic/spin_test.c — owner test for atomic/spin (SpinLock).
//
// Proves the one-word spinlock's whole contract:
//   - zero-value / SPIN_LOCK_INIT starts free; nullptr is a safe no-op;
//   - acquire -> isLocked -> release round trip, and isLocked tracks state;
//   - tryLock fails while held and succeeds when free;
//   - tryLockTimeout returns false at a deadline instead of spinning forever;
//   - unlock is FAIL-CLOSED: a non-owner release is refused, the lock stays
//     held, and only the owner clears it (the pointer/identity safety law);
//   - mutual exclusion + release/acquire visibility under real contention
//     with an external bounded watchdog (the Concurrency and Bounded Progress
//     Law): N threads hammer one counter, final value is exact.

#include "vex.h"
#include <pthread.h>
#include <stdio.h>
#include <sched.h>

#include "atomic/atomic.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// ── nullptr safety is a first-class case ───────────────────────────────────
/** Verifies null lock operations fail safely without acquiring or releasing state. */
static fn test_null_safety() {
    SpinLock_lock(nullptr);
    CHECK(!SpinLock_tryLock(nullptr));
    CHECK(!SpinLock_tryLockTimeout(nullptr, 0));
    SpinLock_unlock(nullptr);
    CHECK(!SpinLock_isLocked(nullptr));
}

// ── basic acquire/release + isLocked round trip ────────────────────────────
/** Checks zero initialization, acquisition, try-acquisition, lock state, and release. */
static fn test_basic_round_trip() {
    SpinLock lock = SPIN_LOCK_INIT;
    CHECK(!SpinLock_isLocked(&lock));
    CHECK(SpinLock_tryLock(&lock));
    CHECK(SpinLock_isLocked(&lock));
    CHECK(!SpinLock_tryLock(&lock));
    SpinLock_unlock(&lock);
    CHECK(!SpinLock_isLocked(&lock));
    SpinLock_unlock(&lock);
    CHECK(!SpinLock_isLocked(&lock));

    SpinLock_lock(&lock);
    CHECK(SpinLock_isLocked(&lock));
    SpinLock_unlock(&lock);
    CHECK(!SpinLock_isLocked(&lock));
}

// ── timeout path: a held lock is reported at the deadline, never hangs ─────
/** Confirms timed acquisition returns at its deadline and succeeds after release. */
static fn test_timeout() {
    SpinLock lock = SPIN_LOCK_INIT;
    CHECK(SpinLock_tryLock(&lock));
    CHECK(!SpinLock_tryLockTimeout(&lock, 200000));
    CHECK(!SpinLock_tryLockTimeout(&lock, 0));
    SpinLock_unlock(&lock);
    CHECK(SpinLock_tryLockTimeout(&lock, 1000000));
    SpinLock_unlock(&lock);
}

// ── fail-closed foreign unlock: a non-owner must never open the lock ───────
static SpinLock g_foreign;
static atomic_int g_foreign_ready;
static atomic_int g_foreign_go;

/** Holds the shared lock until the test permits its owning thread to release it. */
static any foreign_owner(any arg) {
    (fn) arg;
    SpinLock_lock(&g_foreign);
    atomic_store_explicit(&g_foreign_ready, 1, memory_order_release);
    while (!atomic_load_explicit(&g_foreign_go, memory_order_acquire))
        sched_yield();
    SpinLock_unlock(&g_foreign);
    return nullptr;
}

/** Checks a non-owner cannot unlock another thread's lock and that the owner can release it. */
static fn test_foreign_unlock_refused() {
    g_foreign = SPIN_LOCK_INIT;
    atomic_store(&g_foreign_ready, 0);
    atomic_store(&g_foreign_go, 0);
    pthread_t t;
    CHECK(pthread_create(&t, nullptr, foreign_owner, nullptr) == 0);
    while (!atomic_load_explicit(&g_foreign_ready, memory_order_acquire))
        sched_yield();
    CHECK(SpinLock_isLocked(&g_foreign));

    SpinLock_unlock(&g_foreign);
    CHECK(SpinLock_isLocked(&g_foreign));

    atomic_store_explicit(&g_foreign_go, 1, memory_order_release);
    CHECK(pthread_join(t, nullptr) == 0);
    CHECK(!SpinLock_isLocked(&g_foreign));
}

// ── contention: mutual exclusion and release/acquire visibility ────────────
#define SPIN_THREADS 8
#define SPIN_ITER 50000

static SpinLock g_counter_lock;
static int64_t g_plain_counter;               // guarded ONLY by g_counter_lock
static int64_t g_observed_min;
static int64_t g_observed_max;
static atomic_long g_watchdog_ticks;
static atomic_int g_spin_start;

/** Contends on the shared lock while updating the protected counter and watchdog progress count. */
static any spin_worker(any arg) {
    (fn) arg;
    while (!atomic_load_explicit(&g_spin_start, memory_order_acquire))
        sched_yield();
    for (int i = 0; i < SPIN_ITER; i++) {
        SpinLock_lock(&g_counter_lock);
        int64_t before = g_plain_counter;
        g_plain_counter = before + 1;
        if (before < g_observed_min)
            g_observed_min = before;
        if (before > g_observed_max)
            g_observed_max = before;
        SpinLock_unlock(&g_counter_lock);
        atomic_fetch_add_explicit(&g_watchdog_ticks, 1, memory_order_relaxed);
    }
    return nullptr;
}

/** Verifies concurrent increments are neither lost nor duplicated under lock contention. */
static fn test_contention() {
    g_counter_lock = SPIN_LOCK_INIT;
    g_plain_counter = 0;
    g_observed_min = INT64_MAX;
    g_observed_max = 0;
    atomic_store(&g_watchdog_ticks, 0);
    atomic_store(&g_spin_start, 0);

    pthread_t t[SPIN_THREADS];
    for (int i = 0; i < SPIN_THREADS; i++)
        CHECK(pthread_create(&t[i], nullptr, spin_worker, nullptr) == 0);
    atomic_store_explicit(&g_spin_start, 1, memory_order_release);
    for (int i = 0; i < SPIN_THREADS; i++)
        CHECK(pthread_join(t[i], nullptr) == 0);

    CHECK(g_plain_counter == (int64_t) SPIN_THREADS * SPIN_ITER);
    CHECK(g_observed_min == 0);
    CHECK(g_observed_max == (int64_t) SPIN_THREADS * SPIN_ITER - 1);
    CHECK(atomic_load(&g_watchdog_ticks) == (int64_t) SPIN_THREADS * SPIN_ITER);
    CHECK(!SpinLock_isLocked(&g_counter_lock));
}

/** Runs the SpinLock owner scenarios and returns failure if any assertion was violated. */
int main() {
    test_null_safety();
    test_basic_round_trip();
    test_timeout();
    test_foreign_unlock_refused();
    test_contention();

    if (g_failures == 0) {
        printf("spin_test: all assertions held\n");
        return 0;
    }
    printf("spin_test: %d FAILURES\n", g_failures);
    return 1;
}
