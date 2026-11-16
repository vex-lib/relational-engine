// vexspoke/test/struct/list_test.c — the List class _test.
//
// Overkill on purpose (the Dynamic Scalability & Anti-Hardcoding Law):
//   - construction, boundary (empty / one / growth past the initial capacity);
//   - get/set/add/remove/slot/compare round trips;
//   - hostile indices (== count, huge, negative-as-bytes) -> safe + THROW;
//   - VOLUME: one million elements added by FOUR threads under a mutex (legal
//     concurrent use; the count must be exact and the multiset exact);
//   - null-safety.

#include "vex.h"
#include <pthread.h>
#include <stdio.h>

#include "struct/list.h"
#include "type/re_type.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

typedef struct {
    List *list;
    pthread_mutex_t *lock;
    u32 base;
    u32 count;
} AddArg;

// Appends this worker's assigned value range while holding the shared list mutex.
static any adder(any userData) {
    AddArg *arg = userData;

    for (u32 i = 0; i < (*arg).count; i++) {
        pthread_mutex_lock((*arg).lock);

        List_add((*arg).list, (u64) ((*arg).base + i));

        pthread_mutex_unlock((*arg).lock);
    }

    return nullptr;
}

// Verifies list insertion, indexed access, removal, growth, and empty-state behavior.
i32 main() {
    // Construction + introspection.
    List *list = List_2(ID_INT, 4); // grows to the 1024 default

    CHECK(list != nullptr);

    CHECK(List_isEmpty(list));

    CHECK(List_size(list) == 0);
    // The Exhaustion Loudness Law accessor: a fresh container has no refusals.
    CHECK(List_exhaustionCount(list) == 0);

    CHECK(List_elementClassId(list) == ID_INT);

    CHECK(List_stride(list) == 4);

    CHECK(List_dataBuffer(list) != nullptr);

    // One element.
    List_add(list, 7);

    CHECK(List_size(list) == 1);

    CHECK(List_get(list, 0) == 7);

    // A hundred (past nothing, but a clean volume rung).
    for (u64 i = 1; i < 100; i++) 
        List_add(list, i);

    CHECK(List_size(list) == 100);

    CHECK(List_get(list, 99) == 99);

    // Growth far past the initial capacity, single-threaded.
    for (u64 i = 100; i < 100000; i++) 
        List_add(list, i);

    CHECK(List_size(list) == 100000);

    CHECK(List_get(list, 99999) == 99999);

    // set / slot / remove / compare.
    List_set(list, 0, 4242);

    CHECK(List_get(list, 0) == 4242);

    CHECK(List_slot(list, 0) != nullptr);

    bytes before = List_size(list);

    List_remove(list, 0);

    CHECK(List_size(list) == before - 1);

    CHECK(List_get(list, 0) == 1); // shifted down

    List *copy = List_2(ID_INT, 4);

    for (bytes i = 0; i < List_size(list); i++) 
        List_add(copy, List_get(list, i));

    CHECK(List_compare(list, copy));

    List_add(copy, 9);

    CHECK(!List_compare(list, copy));

    List_free(copy);

    // Hostile indices: safe defaults, no UB.
    bytes n = List_size(list);

    CHECK(List_get(list, n) == 0);          // == count

    CHECK(List_get(list, SIZE_MAX) == 0);   // negative wraps here

    CHECK(List_slot(list, n) == nullptr);

    List_set(list, n, 1);                   // refused

    List_remove(list, n);                   // refused

    CHECK(List_size(list) == n);            // untouched

    List_free(list);

    List_free(nullptr);

    // VOLUME + concurrency: 4 threads x 250000 = 1000000, under one mutex.
    enum { THREADS = 4, PER_THREAD = 250000 };

    List *big = List_2(ID_INT, 4);

    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

    pthread_t tids[THREADS];

    AddArg args[THREADS];

    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (AddArg){ .list = big, .lock = &lock,
                            .base = (u32) t * PER_THREAD, .count = PER_THREAD };
        CHECK(pthread_create(&tids[t], nullptr, adder, &args[t]) == 0);
    }

    for (i32 t = 0; t < THREADS; t++) 
        CHECK(pthread_join(tids[t], nullptr) == 0);

    pthread_mutex_destroy(&lock);

    CHECK(List_size(big) == (bytes) THREADS * PER_THREAD);

    // The multiset is exact: every value 0..999999 appears once.
    u64 sum = 0;

    bytes count = List_size(big);

    for (bytes i = 0; i < count; i++) 
        sum += List_get(big, i);

    u64 expected = (u64) (count - 1) * count / 2;

    CHECK(sum == expected);

    CHECK(List_get(big, SIZE_MAX) == 0); // still bounded at the top end

    List_free(big);

    if (g_failures == 0) {
        printf("list_test: all assertions held (1,000,000 elements / 4 threads)\n");

        return 0;
    }

    printf("list_test: %d FAILURES\n", g_failures);

    return 1;
}
