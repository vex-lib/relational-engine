// vexspoke/test/struct/array_test.c — the Array class _test.
//
// Beyond the happy path, this is the HOSTILE-INDEX battery the containers
// demand:
//   - an in-range get/set round trip;
//   - OUT-OF-RANGE (== count, huge), NEGATIVE-AS-SIZE_T, and SIZE_MAX indices:
//     the hot get returns the safe default and the slot accessor returns
//     nullptr — never an out-of-bounds dereference (the Cold-Strict,
//     Hot-Minimal Validation Law);
//   - a refused write must NOT corrupt neighbouring state;
//   - the cold `getTry` seam returns the named reason (TRY_BOUNDS / TRY_NULL_ARG)
//     and reports it on stderr (the Failure Observability Law);
//   - the low-level bounded `Collection_readSlot` refuses an out-of-range index
//     with a safe default, not UB.

#include "vex.h"
#include <stdio.h>

#include "struct/array.h"
#include "struct/collection.h"
#include "exception/try_value.h"
#include "type/re_type.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// Tests Array introspection, round trips, hostile indices, observable rejection, and null safety.
i32 main() {
    Array *a = Array_2(ID_INT, 8);

    CHECK(a != nullptr);

    if (!a) 
        return 1;

    // Introspection.
    CHECK(Array_size(a) == 8);

    CHECK(Array_length(a) == 8);

    CHECK(Array_capacity(a) == 8);

    CHECK(Array_elementClassId(a) == ID_INT);

    CHECK(Array_stride(a) == 4);

    CHECK(!Array_isEmpty(a));

    CHECK(Array_dataBuffer(a) != nullptr);

    CHECK(Array_get(a, 0) == 0); // zero-initialized

    // In-range round trip.
    Array_set(a, 0, 111);

    Array_set(a, 7, 777);

    CHECK(Array_get(a, 0) == 111);

    CHECK(Array_get(a, 7) == 777);

    CHECK(Array_slot(a, 3) != nullptr);

    // Hostile indices: safe defaults, never a dereference.
    CHECK(Array_get(a, 8) == 0);          // == count

    CHECK(Array_get(a, 999) == 0);

    CHECK(Array_get(a, SIZE_MAX) == 0);   // a negative i32 wraps here

    CHECK(Array_get(a, (bytes) -1) == 0);

    CHECK(Array_slot(a, 8) == nullptr);

    CHECK(Array_slot(a, SIZE_MAX) == nullptr);

    Array_set(a, 8, 5);                   // refused: must not corrupt

    Array_set(a, SIZE_MAX, 5);

    CHECK(Array_get(a, 0) == 111);

    CHECK(Array_get(a, 7) == 777);

    // The cold seam names the reason (and prints it).
    TryValue ok = Array_getTry(a, 0);

    CHECK(TryValue_isOk(&ok));

    CHECK(TryValue_getValue(&ok) == 111);

    TryValue hi = Array_getTry(a, 8);

    CHECK(TryValue_getCode(&hi) == TRY_BOUNDS);

    TryValue max = Array_getTry(a, SIZE_MAX);

    CHECK(TryValue_getCode(&max) == TRY_BOUNDS);

    TryValue nul = Array_getTry(nullptr, 0);

    CHECK(TryValue_getCode(&nul) == TRY_NULL_ARG);

    // The bounded Collection accessor refuses out of range (also prints).
    Collection *c = (Collection*) a; // Collection is Array's first member

    CHECK(Collection_readSlot(c, 0) == 111);

    CHECK(Collection_readSlot(c, 7) == 777);

    CHECK(Collection_readSlot(c, 8) == 0);

    CHECK(Collection_readSlot(c, SIZE_MAX) == 0);

    Collection_writeSlot(c, 8, 5);

    CHECK(Collection_readSlot(c, 0) == 111);

    CHECK(Collection_readSlot(nullptr, 0) == 0);

    // Null-safety on every entry point.
    CHECK(Array_get(nullptr, 0) == 0);

    CHECK(Array_slot(nullptr, 0) == nullptr);

    Array_set(nullptr, 0, 1);

    CHECK(Array_size(nullptr) == 0);

    CHECK(Array_isEmpty(nullptr));

    Array_free(nullptr);

    Array_free(a);

    if (g_failures == 0) {
        printf("array_test: all assertions held\n");

        return 0;
    }

    printf("array_test: %d FAILURES\n", g_failures);

    return 1;
}
