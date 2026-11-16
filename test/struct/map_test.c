// vexspoke/test/struct/map_test.c — the Map class _test (key/value, volume).

#include "vex.h"
#include <stdio.h>

#include "struct/map.h"
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

// Checks map insertion, lookup, replacement, missing keys, and cleanup.
i32 main() {
    Map *m = Map_2(ID_INT, ID_INT);

    CHECK(m != nullptr);

    CHECK(Map_isEmpty(m));

    CHECK(Map_size(m) == 0);
    // The Exhaustion Loudness Law accessor: a fresh container has no refusals.
    CHECK(Map_exhaustionCount(m) == 0);

    CHECK(Map_keyClassId(m) == ID_INT);

    CHECK(Map_dataBuffer(m) != nullptr);

    Map_put(m, 1, 100);

    Map_put(m, 2, 200);

    Map_put(m, 2, 222); // overwrite

    CHECK(Map_size(m) == 2);

    CHECK(Map_containsKey(m, 1));

    CHECK(Map_get(m, 1) == 100);

    CHECK(Map_get(m, 2) == 222);

    CHECK(Map_get(m, 3) == 0); // missing

    CHECK(!Map_containsKey(m, 3));

    CHECK(Map_remove(m, 1) == 100);

    CHECK(!Map_containsKey(m, 1));

    CHECK(Map_size(m) == 1);

    Array *keys = Map_keys(m);

    CHECK(keys != nullptr);

    CHECK(Array_size(keys) == 1);

    Array_free(keys);

    // Volume: 100,000 keys.
    for (u64 i = 0; i < 100000; i++) 
        Map_put(m, 1000 + i, i);

    CHECK(Map_size(m) == 100001);

    CHECK(Map_get(m, 1000) == 0);

    CHECK(Map_get(m, 100999) == 99999);

    // Null-safety.
    Map_put(nullptr, 1, 1);

    CHECK(Map_get(nullptr, 1) == 0);

    CHECK(!Map_containsKey(nullptr, 1));

    CHECK(Map_remove(nullptr, 1) == 0);

    CHECK(Map_size(nullptr) == 0);

    CHECK(Map_isEmpty(nullptr));

    CHECK(Map_keys(nullptr) == nullptr);

    Map_free(nullptr);

    Map_free(m);

    if (g_failures == 0) {
        printf("map_test: all assertions held\n");

        return 0;
    }

    printf("map_test: %d FAILURES\n", g_failures);

    return 1;
}
