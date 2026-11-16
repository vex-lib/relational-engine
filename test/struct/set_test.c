// vexspoke/test/struct/set_test.c — the Set class _test (membership, volume, dedup).

#include "vex.h"
#include <stdio.h>

#include "struct/set.h"
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

// Exercises set membership, duplicate handling, insertion, and removal.
i32 main() {
    Set *s = Set_1(ID_INT);

    CHECK(s != nullptr);

    CHECK(Set_isEmpty(s));

    CHECK(Set_size(s) == 0);
    // The Exhaustion Loudness Law accessor: a fresh container has no refusals.
    CHECK(Set_exhaustionCount(s) == 0);

    CHECK(Set_elementClassId(s) == ID_INT);

    CHECK(Set_dataBuffer(s) != nullptr);

    // Membership + dedup.
    CHECK(Set_add(s, 10) == 1);

    CHECK(Set_add(s, 10) == 0); // already present

    CHECK(Set_contains(s, 10));

    CHECK(Set_size(s) == 1);

    CHECK(Set_remove(s, 10) == 1);

    CHECK(!Set_contains(s, 10));

    CHECK(Set_remove(s, 10) == 0);

    // Volume: 100,000 distinct (grows past the initial capacity), then membership.
    for (u64 i = 0; i < 100000; i++) 
        Set_add(s, i);

    CHECK(Set_size(s) == 100000);

    CHECK(Set_contains(s, 0));

    CHECK(Set_contains(s, 99999));

    CHECK(!Set_contains(s, 100000));

    // Projections.
    List *asList = Set_toList(s);

    CHECK(asList != nullptr);

    CHECK(List_size(asList) == 100000);

    List_free(asList);

    List *sorted = Set_toSortedList(s);

    CHECK(sorted != nullptr);

    CHECK(List_size(sorted) == 100000);

    List_free(sorted);

    // Null-safety.
    CHECK(Set_add(nullptr, 1) == 0);

    CHECK(!Set_contains(nullptr, 1));

    CHECK(Set_remove(nullptr, 1) == 0);

    CHECK(Set_size(nullptr) == 0);

    CHECK(Set_isEmpty(nullptr));

    CHECK(Set_toList(nullptr) == nullptr);

    Set_free(nullptr);

    Set_free(s);

    if (g_failures == 0) {
        printf("set_test: all assertions held\n");

        return 0;
    }

    printf("set_test: %d FAILURES\n", g_failures);

    return 1;
}
