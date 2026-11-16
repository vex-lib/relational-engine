// vexspoke/test/struct/collection_test.c — the Collection class _test.
//
// The shared container header/routing. Proves the metadata getters and, above
// all, that the BOUNDED slot pair really is bounded: an out-of-range or
// overflow index is refused with a safe default + a THROW (the THROW Law), never
// an out-of-bounds dereference. The deliberately-UNSAFE pair stays unchecked but
// null-guarded.

#include "vex.h"
#include <stdio.h>

#include "struct/collection.h"
#include "struct/array.h"
#include "type/re_type.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// Exercises the Collection slot interface, including valid access and safe bounds handling.
i32 main() {
    // Collection is Array's first member; build one and view it as its header.
    Array *a = Array_2(ID_INT, 4);

    CHECK(a != nullptr);

    Collection *c = (Collection*) a;

    // Metadata.
    CHECK(Collection_size(c) == 4);

    CHECK(Collection_length(c) == 4);

    CHECK(Collection_capacity(c) == 4);

    CHECK(Collection_elementClassId(c) == ID_INT);

    CHECK(Collection_keyClassId(c) == ID_INT);

    CHECK(Collection_stride(c) == 4);

    CHECK(Collection_valClassId(c) == 4);

    CHECK(Collection_head(c) == 0);

    CHECK(!Collection_isEmpty(c));

    CHECK(Collection_dataBuffer(c) != nullptr);

    CHECK(Collection_type(c) != 0);

    // In-range bounded round trip.
    Collection_writeSlot(c, 0, 123);

    Collection_writeSlot(c, 3, 456);

    CHECK(Collection_readSlot(c, 0) == 123);

    CHECK(Collection_readSlot(c, 3) == 456);

    // Out of range: safe default + a THROW, never UB.
    CHECK(Collection_readSlot(c, 4) == 0);        // == count

    CHECK(Collection_readSlot(c, 999) == 0);

    CHECK(Collection_readSlot(c, SIZE_MAX) == 0);

    Collection_writeSlot(c, 4, 1);                // refused

    Collection_writeSlot(c, SIZE_MAX, 1);

    CHECK(Collection_readSlot(c, 0) == 123);      // untouched

    // The deliberately-UNSAFE pair: unchecked index, but null-guarded.
    CHECK(Collection_readSlotUnsafe(c, 0) == 123);

    Collection_writeSlotUnsafe(c, 1, 7);

    CHECK(Collection_readSlotUnsafe(c, 1) == 7);

    CHECK(Collection_readSlotUnsafe(nullptr, 0) == 0);

    Collection_writeSlotUnsafe(nullptr, 0, 1);    // no crash

    CHECK(Collection_readSlot(nullptr, 0) == 0);

    Collection_writeSlot(nullptr, 0, 1);          // no crash

    // Null metadata is safe.
    CHECK(Collection_size(nullptr) == 0);

    CHECK(Collection_capacity(nullptr) == 0);

    CHECK(Collection_stride(nullptr) == 0);

    CHECK(Collection_elementClassId(nullptr) == 0);

    CHECK(Collection_head(nullptr) == 0);

    CHECK(Collection_isEmpty(nullptr));

    CHECK(Collection_dataBuffer(nullptr) == nullptr);

    CHECK(Collection_type(nullptr) == 0);

    // Exhaustion observability (the Exhaustion Loudness Law): the shared seam
    // counts every refusal and arms one diagnostic per epoch.
    Collection *ac = (Collection*) a;

    CHECK(Collection_exhaustionCount(ac) == 0);

    CHECK(!Collection_reportExhaustion(ac, 4096));       // always reports false

    CHECK(Collection_exhaustionCount(ac) == 1);

    CHECK(!Collection_reportExhaustion(ac, 8192));

    CHECK(Collection_exhaustionCount(ac) == 2);          // counted, not re-reported

    Collection_resetExhaustion(ac);

    CHECK(Collection_exhaustionCount(ac) == 0);          // fresh epoch

    CHECK(Collection_exhaustionCount(nullptr) == 0);

    CHECK(!Collection_reportExhaustion(nullptr, 16));    // null owner: still false

    Collection_resetExhaustion(nullptr);                 // no crash

    Array_free(a);

    if (g_failures == 0) {
        printf("collection_test: all assertions held\n");

        return 0;
    }

    printf("collection_test: %d FAILURES\n", g_failures);

    return 1;
}
