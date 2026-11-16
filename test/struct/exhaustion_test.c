// vexspoke/test/struct/exhaustion_test.c — the shared loud-refusal seam _test.
//
// Proves exception-free counting and one-report-per-epoch behaviour for the
// owner-agnostic exhaustion seam that non-Collection owners (MinHeap, SparseSet,
// Octree) route their refusals through (the Exhaustion Loudness Law).

#include "vex.h"
#include <stdio.h>

#include "struct/exhaustion.h"
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

// Verifies counting, the single-diagnostic epoch, reset and null-safety.
i32 main() {
    u64 count = 0;

    bool reported = false;

    CHECK(Struct_exhaustionCount(&count) == 0);

    CHECK(!reported);

    // First refusal reports and counts.
    CHECK(!Struct_reportExhaustion(&count, &reported, "probe", 4096u, 2048u));

    CHECK(Struct_exhaustionCount(&count) == 1);

    CHECK(reported);

    // Later refusals in the same epoch only count.
    CHECK(!Struct_reportExhaustion(&count, &reported, "probe", 4096u, 2048u));

    CHECK(!Struct_reportExhaustion(&count, &reported, "probe", 8192u, 2048u));

    CHECK(Struct_exhaustionCount(&count) == 3);

    // A reset starts a fresh epoch.
    Struct_resetExhaustion(&count, &reported);

    CHECK(Struct_exhaustionCount(&count) == 0);

    CHECK(!reported);

    CHECK(!Struct_reportExhaustion(&count, &reported, "probe", 16u, 0u));

    CHECK(Struct_exhaustionCount(&count) == 1);

    // Null-safety: a null counter is reported, never dereferenced.
    CHECK(!Struct_reportExhaustion(nullptr, &reported, "probe", 8u, 0u));

    CHECK(!Struct_reportExhaustion(&count, nullptr, "probe", 8u, 0u));

    CHECK(Struct_exhaustionCount(nullptr) == 0);

    Struct_resetExhaustion(nullptr, nullptr);   // no crash

    // Construction refusals: process-wide (no owner instance exists yet),
    // counted always and reported once per epoch.
    CHECK(Struct_constructionExhaustionCount() == 0);

    CHECK(!Struct_reportConstructionExhaustion("probe", 128u));

    CHECK(Struct_constructionExhaustionCount() == 1);

    CHECK(!Struct_reportConstructionExhaustion("probe", 256u));

    CHECK(Struct_constructionExhaustionCount() == 2);

    Struct_resetConstructionExhaustion();

    CHECK(Struct_constructionExhaustionCount() == 0);

    // End-to-end wiring: a constructor whose backing cannot be allocated
    // reports the refusal and returns nullptr (a ~400 MB buffer exceeds any
    // arena the default 64 MB master arena can serve).
    List *tooBig = List_2(ID_INT, 100000000u);

    CHECK(tooBig == nullptr);

    CHECK(Struct_constructionExhaustionCount() == 1);

    // A construction that fits still succeeds and does not count.
    List *ok = List_1(ID_INT);

    CHECK(ok != nullptr);

    CHECK(Struct_constructionExhaustionCount() == 1);

    List_free(ok);

    if (g_failures == 0) {
        printf("exhaustion_test: all assertions held\n");

        return 0;
    }

    printf("exhaustion_test: %d FAILURES\n", g_failures);

    return 1;
}
