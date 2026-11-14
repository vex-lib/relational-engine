#include "lang/annotation.h"

#include <stdio.h>
#include <string.h>
#include "vex.h"

#include "nio/mem.h"
#include "type/re_type.h"
#include "substrate/variable/variable_mini_map.h"
#include "substrate/variable/variable_slot.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: VariableMiniMapTest (variable_mini_map_test/test.c)
 * LEVEL: L3 — Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the scoped mini map: the 39 single-level buckets,
 * lowercase folding + dotted names, upsert, rejection, forEach, null-safety,
 * string projections.
 *
 * STRUCT FIELDS: none — procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(cond)) {                                                    \
            g_fail++;                                                     \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);              \
        }                                                                 \
    } while (0)

// Checks map initialization, empty/missing lookup, basic insertion, null safety, and shutdown.
static fn testLifecycle() {
    printf("[1] init / empty / count / null-safety\n");

    VariableMiniMap map;
    CHECK(VariableMiniMap_init(&map) == true);
    CHECK(VariableMiniMap_count(&map) == 0u);
    CHECK(VariableMiniMap_isEmpty(&map) == true);

    CHECK(VariableMiniMap_get(&map, "x") == 0u);
    CHECK(VariableMiniMap_contains(&map, "x") == false);
    CHECK(VariableMiniMap_add(&map, "x", 1u) == true);
    CHECK(VariableMiniMap_count(&map) == 1u);
    CHECK(VariableMiniMap_isEmpty(&map) == false);
    CHECK(VariableMiniMap_get(&map, "X") == 1u);

    CHECK(VariableMiniMap_init(nullptr) == false);
    CHECK(VariableMiniMap_count(nullptr) == 0u);
    CHECK(VariableMiniMap_isEmpty(nullptr) == true);
    CHECK(VariableMiniMap_add(nullptr, "x", 1u) == false);
    CHECK(VariableMiniMap_get(nullptr, "x") == 0u);
    CHECK(VariableMiniMap_contains(nullptr, "x") == false);

    VariableMiniMap_shutdown(&map);
    CHECK(VariableMiniMap_count(&map) == 0u);
    VariableMiniMap_shutdown(&map);
    CHECK(true);
}

// Covers all buckets, case-folded dotted keys, and in-place upsert behavior.
static fn testBucketsAndFold() {
    printf("[2] 39 buckets + folding + dotted names + upsert\n");

    VariableMiniMap map;
    VariableMiniMap_init(&map);

    const char *firsts = "abcdefghijklmnopqrstuvwxyz0123456789_$-";
    bytes n = strlen(firsts);
    CHECK(n == 39u);
    for (bytes i = 0u; i < n; i++) {
        char name[4] = { firsts[i], '1', '\0' };
        CHECK(VariableMiniMap_add(&map, name, 0x100u + (pointer) i) == true);
    }
    CHECK(VariableMiniMap_count(&map) == 39u);
    for (bytes i = 0u; i < n; i++) {
        char name[4] = { firsts[i], '1', '\0' };
        CHECK(VariableMiniMap_get(&map, name) == 0x100u + (pointer) i);
    }

    CHECK(VariableMiniMap_add(&map, "Character.Position.X", 0xAAu) == true);
    CHECK(VariableMiniMap_get(&map, "character.position.x") == 0xAAu);

    u32 before = VariableMiniMap_count(&map);
    CHECK(VariableMiniMap_add(&map, "character.position.x", 0xBBu) == true);
    CHECK(VariableMiniMap_count(&map) == before);
    CHECK(VariableMiniMap_get(&map, "CHARACTER.POSITION.X") == 0xBBu);

    VariableMiniMap_shutdown(&map);
}

// Checks malformed and overlong key rejection without changing the entry count.
static fn testRejection() {
    printf("[3] illegal names rejected\n");

    VariableMiniMap map;
    VariableMiniMap_init(&map);
    CHECK(VariableMiniMap_add(&map, nullptr, 1u) == false);
    CHECK(VariableMiniMap_add(&map, "", 1u) == false);
    CHECK(VariableMiniMap_add(&map, "abcdefghijklmnopqrstuvwx", 1u) == false);
    CHECK(VariableMiniMap_add(&map, ".a", 1u) == false);
    CHECK(VariableMiniMap_add(&map, "a.", 1u) == false);
    CHECK(VariableMiniMap_add(&map, "a..b.sh", 1u) == false);
    CHECK(VariableMiniMap_add(&map, "a b.sh", 1u) == false);
    CHECK(VariableMiniMap_count(&map) == 0u);
    VariableMiniMap_shutdown(&map);
}

// Checks constructor/free forms, bounded projections, and safe no-op iteration inputs.
static fn testForEachAndStrings() {
    printf("[4] forEach + string projections + arity ctor\n");

    VariableMiniMap *map = VariableMiniMap();
    CHECK(map != nullptr);
    if (!map)
        return;
    CHECK(Memory_type(map) == TYPE_VARIABLE_MINI_MAP);
    VariableMiniMap_add(map, "position.x", 1u);
    VariableMiniMap_add(map, "position.y", 2u);
    VariableMiniMap_add(map, "health", 3u);

    char buf[128];
    bool truncated = true;
    VariableMiniMap_toString(map, buf, sizeof(buf), &truncated);
    CHECK(truncated == false && strcmp(buf, "VariableMiniMap(count=3)") == 0);
    VariableMiniMap_toStringStruct(map, buf, sizeof(buf), &truncated);
    CHECK(truncated == false && strcmp(buf, "VariableMiniMap { active=true, count=3, buckets=<array[39]> }") == 0);

    char tiny[4];
    VariableMiniMap_toString(map, tiny, sizeof(tiny), &truncated);
    CHECK(truncated == true);
    truncated = true;
    VariableMiniMap_toString(nullptr, buf, sizeof(buf), &truncated);
    CHECK(truncated == false && strcmp(buf, "nullptr") == 0);

    VariableMiniMap_forEach(map, nullptr, nullptr);
    VariableMiniMap_forEach(nullptr, nullptr, nullptr);
    CHECK(true);
    VariableMiniMap_free(map);
    VariableMiniMap_free(nullptr);
}

// Runs map lifecycle, key policy, projection, and iteration cases.
int main() {
    printf("=== VariableMiniMap (scoped) Test Suite ===\n\n");

    testLifecycle();
    testBucketsAndFold();
    testRejection();
    testForEachAndStrings();

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0)
        printf("=== VariableMiniMap Test Suite: ALL PASS ===\n");
    return g_fail == 0 ? 0 : 1;
}
