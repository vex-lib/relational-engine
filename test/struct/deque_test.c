// vexspoke/test/struct/deque_test.c — the Deque class _test (both ends, volume, hostile).

#include "vex.h"
#include <stdio.h>

#include "struct/deque.h"
#include "type/re_type.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// Exercises deque construction, both-end operations, large growth, hostile indices, null safety, and cleanup.
i32 main() {
    Deque *d = Deque_1(ID_INT);

    CHECK(d != nullptr);

    CHECK(Deque_isEmpty(d));

    CHECK(Deque_elementClassId(d) == ID_INT);

    CHECK(Deque_stride(d) == 4);

    CHECK(Deque_dataBuffer(d) != nullptr);

    // Both ends: 0 1 2.
    Deque_addLast(d, 1);

    Deque_addLast(d, 2);

    Deque_addFirst(d, 0);

    CHECK(Deque_size(d) == 3);
    // The Exhaustion Loudness Law accessor: a fresh container has no refusals.
    CHECK(Deque_exhaustionCount(d) == 0);

    CHECK(Deque_peekFirst(d) == 0);

    CHECK(Deque_peekLast(d) == 2);

    CHECK(Deque_get(d, 0) == 0);

    CHECK(Deque_get(d, 1) == 1);

    CHECK(Deque_get(d, 2) == 2);

    CHECK(Deque_removeFirst(d) == 0);

    CHECK(Deque_removeLast(d) == 2);

    CHECK(Deque_removeFirst(d) == 1);

    CHECK(Deque_removeFirst(d) == 0);   // empty

    CHECK(Deque_peekFirst(d) == 0);

    // Volume from the last end, then drain from the front.
    for (u64 i = 0; i < 1000000; i++) 
        Deque_addLast(d, i);

    CHECK(Deque_size(d) == 1000000);

    CHECK(Deque_peekFirst(d) == 0);

    CHECK(Deque_peekLast(d) == 999999);

    CHECK(Deque_get(d, 12345) == 12345);

    // Hostile indices.
    CHECK(Deque_get(d, Deque_size(d)) == 0);

    CHECK(Deque_get(d, SIZE_MAX) == 0);

    CHECK(Deque_slot(d, Deque_size(d)) == nullptr);

    CHECK(Deque_slot(d, SIZE_MAX) == nullptr);

    // Null-safety.
    Deque_addFirst(nullptr, 1);

    Deque_addLast(nullptr, 1);

    CHECK(Deque_removeFirst(nullptr) == 0);

    CHECK(Deque_removeLast(nullptr) == 0);

    CHECK(Deque_peekFirst(nullptr) == 0);

    CHECK(Deque_peekLast(nullptr) == 0);

    CHECK(Deque_get(nullptr, 0) == 0);

    CHECK(Deque_slot(nullptr, 0) == nullptr);

    CHECK(Deque_size(nullptr) == 0);

    CHECK(Deque_isEmpty(nullptr));

    Deque_free(nullptr);

    Deque_free(d);

    if (g_failures == 0) {
        printf("deque_test: all assertions held\n");

        return 0;
    }

    printf("deque_test: %d FAILURES\n", g_failures);

    return 1;
}
