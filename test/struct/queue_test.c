// vexspoke/test/struct/queue_test.c — the Queue class _test (FIFO, volume, hostile).

#include "vex.h"
#include <stdio.h>

#include "struct/queue.h"
#include "type/re_type.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// Verifies FIFO insertion/removal, empty behavior, bounds, and safe cleanup.
i32 main() {
    Queue *q = Queue_1(ID_INT);

    CHECK(q != nullptr);

    CHECK(Queue_isEmpty(q));

    CHECK(Queue_elementClassId(q) == ID_INT);

    CHECK(Queue_stride(q) == 4);

    CHECK(Queue_dataBuffer(q) != nullptr);

    // FIFO order.
    Queue_push(q, 10);

    Queue_push(q, 20);

    Queue_push(q, 30);

    CHECK(Queue_size(q) == 3);
    // The Exhaustion Loudness Law accessor: a fresh container has no refusals.
    CHECK(Queue_exhaustionCount(q) == 0);

    CHECK(Queue_peek(q) == 10);

    CHECK(Queue_pop(q) == 10);

    CHECK(Queue_pop(q) == 20);

    CHECK(Queue_pop(q) == 30);

    CHECK(Queue_pop(q) == 0);   // empty

    CHECK(Queue_peek(q) == 0);

    // Volume: a million enqueued, then dequeued in order.
    for (u64 i = 0; i < 1000000; i++) 
        Queue_push(q, i);

    CHECK(Queue_size(q) == 1000000);

    CHECK(Queue_peek(q) == 0);

    CHECK(Queue_pop(q) == 0);

    CHECK(Queue_pop(q) == 1);

    CHECK(Queue_pop(q) == 2);

    // Hostile indices.
    CHECK(Queue_slot(q, Queue_size(q)) == nullptr);

    CHECK(Queue_slot(q, SIZE_MAX) == nullptr);

    // Null-safety.
    Queue_push(nullptr, 1);

    CHECK(Queue_pop(nullptr) == 0);

    CHECK(Queue_peek(nullptr) == 0);

    CHECK(Queue_slot(nullptr, 0) == nullptr);

    CHECK(Queue_size(nullptr) == 0);

    CHECK(Queue_head(nullptr) == 0);

    CHECK(Queue_isEmpty(nullptr));

    Queue_free(nullptr);

    Queue_free(q);

    if (g_failures == 0) {
        printf("queue_test: all assertions held\n");

        return 0;
    }

    printf("queue_test: %d FAILURES\n", g_failures);

    return 1;
}
