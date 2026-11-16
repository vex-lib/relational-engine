// vexspoke/test/struct/stack_test.c — the Stack class _test (LIFO, volume, hostile).

#include "vex.h"
#include <stdio.h>

#include "struct/stack.h"
#include "type/re_type.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// Verifies LIFO push/pop behavior, empty defaults, and stack cleanup.
i32 main() {
    Stack *s = Stack_1(ID_INT);

    CHECK(Stack_exhaustionCount(s) == 0);   // fresh: no refused grows yet

    CHECK(s != nullptr);

    CHECK(Stack_isEmpty(s));

    CHECK(Stack_size(s) == 0);

    CHECK(Stack_elementClassId(s) == ID_INT);

    CHECK(Stack_stride(s) == 4);

    CHECK(Stack_dataBuffer(s) != nullptr);

    // LIFO order.
    Stack_push(s, 10);

    Stack_push(s, 20);

    CHECK(Stack_size(s) == 2);

    CHECK(Stack_peek(s) == 20);

    CHECK(Stack_pop(s) == 20);

    CHECK(Stack_pop(s) == 10);

    CHECK(Stack_pop(s) == 0);   // empty

    CHECK(Stack_peek(s) == 0);

    // Volume: a million, then unwind in reverse.
    for (u64 i = 0; i < 1000000; i++) 
        Stack_push(s, i);

    CHECK(Stack_size(s) == 1000000);

    CHECK(Stack_peek(s) == 999999);

    CHECK(Stack_pop(s) == 999999);

    CHECK(Stack_pop(s) == 999998);

    CHECK(Stack_size(s) == 999998);

    // Hostile indices.
    CHECK(Stack_slot(s, Stack_size(s)) == nullptr);

    CHECK(Stack_slot(s, SIZE_MAX) == nullptr);

    // Explicit-fault sibling: Stack_pushTry names the reason. Stack_push stays
    // fn for family consistency but is no longer silent.
    TryValue okPush = Stack_pushTry(s, 7);

    CHECK(TryValue_isOk(&okPush));

    CHECK(TryValue_getValue(&okPush) == 7);

    CHECK(Stack_size(s) == 999999);

    CHECK(Stack_pop(s) == 7);

    TryValue nullPush = Stack_pushTry(nullptr, 7);

    CHECK(TryValue_getCode(&nullPush) == TRY_NULL_ARG);

    // Null-safety.
    Stack_push(nullptr, 1);

    CHECK(Stack_pop(nullptr) == 0);

    CHECK(Stack_peek(nullptr) == 0);

    CHECK(Stack_slot(nullptr, 0) == nullptr);

    CHECK(Stack_size(nullptr) == 0);

    CHECK(Stack_isEmpty(nullptr));

    Stack_free(nullptr);

    Stack_free(s);

    if (g_failures == 0) {
        printf("stack_test: all assertions held\n");

        return 0;
    }

    printf("stack_test: %d FAILURES\n", g_failures);

    return 1;
}
