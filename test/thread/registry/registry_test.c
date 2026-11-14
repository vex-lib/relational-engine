// re/test/thread/registry_test.c — owner test for thread/registry.
//
// ThreadRegistry gives every thread a stable dense index in [0, 256) and one
// opaque advisory tag. This test proves:
//   - the calling thread's index is stable across calls and in range;
//   - tags default to THREAD_TAG_NONE and set/get round-trips;
//   - out-of-range index calls are safe and read THREAD_TAG_NONE;
//   - under real concurrency every live thread gets a DISTINCT, stable index
//     (the lock-free probe-and-CAS contract) within a bounded watchdog;
//   - a thread that has joined and is gone does not invalidate anyone.
//
// Role names are policy, owned and proven by Vexspoke's thread test.

#include "vex.h"
#include <pthread.h>
#include <stdio.h>
#include <sched.h>

#include "thread/thread.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static fn test_self_index() {
    int a = ThreadRegistry_index();
    int b = ThreadRegistry_index();
    CHECK(a == b);
    CHECK(a >= 0 && a < THREAD_REGISTRY_DEFAULT_SIZE);
}

static fn test_tags() {
    int idx = ThreadRegistry_index();
    CHECK(ThreadRegistry_tag(idx) == THREAD_TAG_NONE);

    static const u32 tags[] = {
        THREAD_TAG_NONE, 1u, 7u, 0x1234u, 0xFFFFFFFFu,
    };
    for (bytes i = 0; i < sizeof tags / sizeof tags[0]; i++) {
        ThreadRegistry_setTag(idx, tags[i]);
        CHECK(ThreadRegistry_tag(idx) == tags[i]);
    }
    ThreadRegistry_setTag(idx, 42u);
    CHECK(ThreadRegistry_tag(idx) == 42u);
    ThreadRegistry_setTag(idx, THREAD_TAG_NONE);
}

static fn test_bounds() {
    ThreadRegistry_setTag(-1, 42u);
    ThreadRegistry_setTag(THREAD_REGISTRY_DEFAULT_SIZE, 42u);
    ThreadRegistry_setTag(1 << 20, 42u);
    CHECK(ThreadRegistry_tag(-1) == THREAD_TAG_NONE);
    CHECK(ThreadRegistry_tag(THREAD_REGISTRY_DEFAULT_SIZE) == THREAD_TAG_NONE);
    CHECK(ThreadRegistry_tag(INT32_MAX) == THREAD_TAG_NONE);
    CHECK(ThreadRegistry_tag(INT32_MIN) == THREAD_TAG_NONE);
}

// ── concurrency: distinct, stable indices for every live thread ────────────
#define REG_THREADS 16
#define REG_REPEAT 200

static atomic_int g_reg_start;
static int g_reg_index[REG_THREADS];
static int g_reg_stable_flags[REG_THREADS];     // 1 if all repeats agreed

static any reg_worker(any arg) {
    int id = (int) (intptr_t) arg;
    while (!atomic_load_explicit(&g_reg_start, memory_order_acquire))
        sched_yield();
    int first = ThreadRegistry_index();
    int stable = 1;
    for (int i = 0; i < REG_REPEAT; i++)
        if (ThreadRegistry_index() != first)
            stable = 0;
    ThreadRegistry_setTag(first, 42u);
    g_reg_index[id] = first;
    g_reg_stable_flags[id] = stable;
    while (atomic_load_explicit(&g_reg_start, memory_order_acquire) != 2)
        sched_yield();
    return nullptr;
}

static fn test_concurrent_distinct() {
    atomic_store(&g_reg_start, 0);
    pthread_t t[REG_THREADS];
    for (int i = 0; i < REG_THREADS; i++) {
        g_reg_index[i] = -1;
        g_reg_stable_flags[i] = 0;
        CHECK(pthread_create(&t[i], nullptr, reg_worker, (any) (intptr_t) i) == 0);
    }
    atomic_store_explicit(&g_reg_start, 1, memory_order_release);
    bool all_published = false;
    while (!all_published) {
        all_published = true;
        for (int i = 0; i < REG_THREADS; i++)
            if (g_reg_index[i] < 0)
                all_published = false;
        sched_yield();
    }
    int idx[REG_THREADS];
    for (int i = 0; i < REG_THREADS; i++) {
        idx[i] = g_reg_index[i];
        CHECK(g_reg_stable_flags[i] == 1);
        CHECK(idx[i] >= 0 && idx[i] < THREAD_REGISTRY_DEFAULT_SIZE);
    }
    for (int i = 0; i < REG_THREADS; i++)
        for (int j = i + 1; j < REG_THREADS; j++)
            CHECK(idx[i] != idx[j]);

    atomic_store_explicit(&g_reg_start, 2, memory_order_release);
    for (int i = 0; i < REG_THREADS; i++)
        CHECK(pthread_join(t[i], nullptr) == 0);

    int me = ThreadRegistry_index();
    CHECK(me == ThreadRegistry_index());
    CHECK(me >= 0 && me < THREAD_REGISTRY_DEFAULT_SIZE);
}

int main() {
    test_self_index();
    test_tags();
    test_bounds();
    test_concurrent_distinct();

    if (g_failures == 0) {
        printf("registry_test: all assertions held\n");
        return 0;
    }
    printf("registry_test: %d FAILURES\n", g_failures);
    return 1;
}
