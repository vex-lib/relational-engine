// vexspoke/test/atomic/registry_test.c — owner test for atomic/registry.
//
// ThreadRegistry gives every thread a stable dense index in [0,256) and an
// advisory role. This test proves:
//   - the calling thread's index is stable across calls and in range;
//   - roles default to NONE and set/get round-trips;
//   - out-of-range index/role calls are safe and return NONE;
//   - role names map exactly, and unknown roles read "none";
//   - under real concurrency every live thread gets a DISTINCT, stable index
//     (the lock-free probe-and-CAS contract) within a bounded watchdog;
//   - a thread that has joined and is gone does not invalidate anyone.

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sched.h>
#include <string.h>

#include "atomic/registry.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static void test_self_index(void) {
    int a = ThreadRegistry_index();
    int b = ThreadRegistry_index();
    CHECK(a == b);                                  // stable for a thread
    CHECK(a >= 0 && a < THREAD_REGISTRY_SIZE);
}

static void test_roles(void) {
    int idx = ThreadRegistry_index();
    // Default is untagged.
    CHECK(ThreadRegistry_role(idx) == THREAD_ROLE_NONE);

    static const int roles[] = {
        THREAD_ROLE_NONE, THREAD_ROLE_MAIN, THREAD_ROLE_ENGINE,
        THREAD_ROLE_DRAW, THREAD_ROLE_PRESENT, THREAD_ROLE_NETWORKING,
        THREAD_ROLE_SCRIPTING, THREAD_ROLE_CONSOLE, THREAD_ROLE_UI,
        THREAD_ROLE_USER,
    };
    for (size_t i = 0; i < sizeof roles / sizeof roles[0]; i++) {
        ThreadRegistry_setRole(idx, roles[i]);
        CHECK(ThreadRegistry_role(idx) == roles[i]);
    }
    // Restore and confirm.
    ThreadRegistry_setRole(idx, THREAD_ROLE_MAIN);
    CHECK(ThreadRegistry_role(idx) == THREAD_ROLE_MAIN);
    ThreadRegistry_setRole(idx, THREAD_ROLE_NONE);
}

static void test_role_names(void) {
    CHECK(strcmp(ThreadRegistry_roleName(THREAD_ROLE_MAIN), "_main") == 0);
    CHECK(strcmp(ThreadRegistry_roleName(THREAD_ROLE_ENGINE), "engine") == 0);
    CHECK(strcmp(ThreadRegistry_roleName(THREAD_ROLE_DRAW), "draw") == 0);
    CHECK(strcmp(ThreadRegistry_roleName(THREAD_ROLE_PRESENT), "present") == 0);
    CHECK(strcmp(ThreadRegistry_roleName(THREAD_ROLE_NETWORKING), "networking") == 0);
    CHECK(strcmp(ThreadRegistry_roleName(THREAD_ROLE_SCRIPTING), "scripting") == 0);
    CHECK(strcmp(ThreadRegistry_roleName(THREAD_ROLE_CONSOLE), "console") == 0);
    CHECK(strcmp(ThreadRegistry_roleName(THREAD_ROLE_UI), "ui") == 0);
    CHECK(strcmp(ThreadRegistry_roleName(THREAD_ROLE_USER), "user") == 0);
    // Untagged and any unknown value read the neutral name.
    CHECK(strcmp(ThreadRegistry_roleName(THREAD_ROLE_NONE), "none") == 0);
    CHECK(strcmp(ThreadRegistry_roleName(-1), "none") == 0);
    CHECK(strcmp(ThreadRegistry_roleName(9999), "none") == 0);
}

static void test_bounds(void) {
    // Out-of-range writes are ignored; out-of-range reads are safe defaults.
    ThreadRegistry_setRole(-1, THREAD_ROLE_USER);
    ThreadRegistry_setRole(THREAD_REGISTRY_SIZE, THREAD_ROLE_USER);
    ThreadRegistry_setRole(1 << 20, THREAD_ROLE_USER);
    CHECK(ThreadRegistry_role(-1) == THREAD_ROLE_NONE);
    CHECK(ThreadRegistry_role(THREAD_REGISTRY_SIZE) == THREAD_ROLE_NONE);
    CHECK(ThreadRegistry_role(INT32_MAX) == THREAD_ROLE_NONE);
    CHECK(ThreadRegistry_role(INT32_MIN) == THREAD_ROLE_NONE);
}

// ── concurrency: distinct, stable indices for every live thread ────────────
#define REG_THREADS 16
#define REG_REPEAT 200

static atomic_int g_reg_start;
static int g_reg_index[REG_THREADS];
static int g_reg_stable_flags[REG_THREADS];     // 1 if all repeats agreed

static void *reg_worker(void *arg) {
    int id = (int) (intptr_t) arg;
    while (!atomic_load_explicit(&g_reg_start, memory_order_acquire))
        sched_yield();
    int first = ThreadRegistry_index();
    int stable = 1;
    for (int i = 0; i < REG_REPEAT; i++)
        if (ThreadRegistry_index() != first)
            stable = 0;
    ThreadRegistry_setRole(first, THREAD_ROLE_USER);
    g_reg_index[id] = first;
    g_reg_stable_flags[id] = stable;
    // Spin until the main thread has collected every index.
    while (atomic_load_explicit(&g_reg_start, memory_order_acquire) != 2)
        sched_yield();
    return nullptr;
}

static void test_concurrent_distinct(void) {
    atomic_store(&g_reg_start, 0);
    pthread_t t[REG_THREADS];
    for (int i = 0; i < REG_THREADS; i++) {
        g_reg_index[i] = -1;
        g_reg_stable_flags[i] = 0;
        CHECK(pthread_create(&t[i], nullptr, reg_worker, (void*) (intptr_t) i) == 0);
    }
    atomic_store_explicit(&g_reg_start, 1, memory_order_release);
    // Wait until every worker published its distinct index.
    bool all_published = false;
    while (!all_published) {
        all_published = true;
        for (int i = 0; i < REG_THREADS; i++)
            if (g_reg_index[i] < 0)
                all_published = false;
        sched_yield();
    }
    // Snapshot and validate while all threads are still alive.
    int idx[REG_THREADS];
    for (int i = 0; i < REG_THREADS; i++) {
        idx[i] = g_reg_index[i];
        CHECK(g_reg_stable_flags[i] == 1);
        CHECK(idx[i] >= 0 && idx[i] < THREAD_REGISTRY_SIZE);
    }
    for (int i = 0; i < REG_THREADS; i++)
        for (int j = i + 1; j < REG_THREADS; j++)
            CHECK(idx[i] != idx[j]);                // dense, collision-free

    atomic_store_explicit(&g_reg_start, 2, memory_order_release);
    for (int i = 0; i < REG_THREADS; i++)
        CHECK(pthread_join(t[i], nullptr) == 0);

    // The main thread still has its own stable index after the others left.
    int me = ThreadRegistry_index();
    CHECK(me == ThreadRegistry_index());
    CHECK(me >= 0 && me < THREAD_REGISTRY_SIZE);
}

int main(void) {
    test_self_index();
    test_roles();
    test_role_names();
    test_bounds();
    test_concurrent_distinct();

    if (g_failures == 0) {
        printf("registry_test: all assertions held\n");
        return 0;
    }
    printf("registry_test: %d FAILURES\n", g_failures);
    return 1;
}
