#include "annotation/annotation.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "algo/segment_index.h"
#include "nio/mem.h"
#include "oop/type.h"
#include "relational/variable_mini_map.h"
#include "relational/variable_slot.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: VariableMiniMapTest (variable_mini_map_test/test.c)
 * LEVEL: L3 — Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the scoped mini map: the 39 single-level buckets,
 * lowercase folding + dotted names, upsert, rejection, forEach, null-safety,
 * string projections, and the SCOPED search (build a SegmentIndex from a
 * class's mini map and query it).
 *
 * STRUCT FIELDS: none — procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main(void)
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

// Resolves a match's entry index to its stored name, using empty text if absent.
static const char *matchName(const SegmentIndex *index, const SegmentMatch *m) {
    const SegmentEntry *e = (const SegmentEntry*) ChunkedList_slot((*index).entries, (*m).entry);
    return e ? (*e).name : "";
}

// Checks map initialization, empty/missing lookup, basic insertion, null safety, and shutdown.
static void testLifecycle(void) {
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
    VariableMiniMap_shutdown(&map); // idempotent
    CHECK(true);
}

// Covers all buckets, case-folded dotted keys, and in-place upsert behavior.
static void testBucketsAndFold(void) {
    printf("[2] 39 buckets + folding + dotted names + upsert\n");

    VariableMiniMap map;
    VariableMiniMap_init(&map);

    const char *firsts = "abcdefghijklmnopqrstuvwxyz0123456789_$-";
    size_t n = strlen(firsts);
    CHECK(n == 39u);
    for (size_t i = 0u; i < n; i++) {
        char name[4] = { firsts[i], '1', '\0' };
        CHECK(VariableMiniMap_add(&map, name, 0x100u + (uintptr_t) i) == true);
    }
    CHECK(VariableMiniMap_count(&map) == 39u);
    for (size_t i = 0u; i < n; i++) {
        char name[4] = { firsts[i], '1', '\0' };
        CHECK(VariableMiniMap_get(&map, name) == 0x100u + (uintptr_t) i);
    }

    // Folding + dotted names.
    CHECK(VariableMiniMap_add(&map, "Character.Position.X", 0xAAu) == true);
    CHECK(VariableMiniMap_get(&map, "character.position.x") == 0xAAu);

    // Upsert: same folded key updates, never duplicates.
    uint32_t before = VariableMiniMap_count(&map);
    CHECK(VariableMiniMap_add(&map, "character.position.x", 0xBBu) == true);
    CHECK(VariableMiniMap_count(&map) == before);
    CHECK(VariableMiniMap_get(&map, "CHARACTER.POSITION.X") == 0xBBu);

    VariableMiniMap_shutdown(&map);
}

// Checks malformed and overlong key rejection without changing the entry count.
static void testRejection(void) {
    printf("[3] illegal names rejected\n");

    VariableMiniMap map;
    VariableMiniMap_init(&map);
    CHECK(VariableMiniMap_add(&map, nullptr, 1u) == false);
    CHECK(VariableMiniMap_add(&map, "", 1u) == false);
    CHECK(VariableMiniMap_add(&map, "abcdefghijklmnopqrstuvwx", 1u) == false); // 24
    CHECK(VariableMiniMap_add(&map, ".a", 1u) == false);
    CHECK(VariableMiniMap_add(&map, "a.", 1u) == false);
    CHECK(VariableMiniMap_add(&map, "a..b.sh", 1u) == false);
    CHECK(VariableMiniMap_add(&map, "a b.sh", 1u) == false);
    CHECK(VariableMiniMap_count(&map) == 0u);
    VariableMiniMap_shutdown(&map);
}

// Checks constructor/free forms, bounded projections, and safe no-op iteration inputs.
static void testForEachAndStrings(void) {
    printf("[4] forEach + string projections + arity ctor\n");

    VariableMiniMap *map = VariableMiniMap();
    CHECK(map != nullptr);
    if (!map)
        return;
    CHECK(Memory_type(map) == TYPE_VARIABLE_MINI_MAP);
    VariableMiniMap_add(map, "position.x", 1u);
    VariableMiniMap_add(map, "position.y", 2u);
    VariableMiniMap_add(map, "health", 3u);

    // Count via a probe by walking the buckets twice is not needed; use count.
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

    VariableMiniMap_forEach(map, nullptr, nullptr); // no-op
    VariableMiniMap_forEach(nullptr, nullptr, nullptr);
    CHECK(true);
    VariableMiniMap_free(map);
    VariableMiniMap_free(nullptr);
}

// Builds a SegmentIndex from map fields and checks scoped ranking and payload preservation.
static void testScopedSearch(void) {
    printf("[5] scoped search: SegmentIndex over a class's field mini map\n");

    VariableMiniMap *fields = VariableMiniMap();
    SegmentIndex *scoped = SegmentIndex_0();
    CHECK(fields && scoped);
    if (!fields || !scoped) {
        VariableMiniMap_free(fields);
        SegmentIndex_free(scoped);
        return;
    }

    VariableMiniMap_add(fields, "character.position.x", 1u);
    VariableMiniMap_add(fields, "position.x", 2u);
    VariableMiniMap_add(fields, "vector.x", 3u);
    VariableMiniMap_add(fields, "vector.y", 4u);
    VariableMiniMap_add(fields, "health", 5u);

    CHECK(SegmentIndex_buildFromMini(scoped, fields) == 5u);

    SegmentMatch out[8];
    bool truncated = true;

    // Scoped exact: "vector.x" is unique to this class.
    size_t n = SegmentIndex_query(scoped, "vector.x", out, 8u, &truncated);
    CHECK(n == 1u && strcmp(matchName(scoped, &out[0]), "vector.x") == 0);

    // Broad "x": suffix hits ordered by start then length.
    n = SegmentIndex_query(scoped, "x", out, 8u, &truncated);
    CHECK(n == 3u);
    CHECK(strcmp(matchName(scoped, &out[0]), "vector.x") == 0);
    CHECK(strcmp(matchName(scoped, &out[1]), "position.x") == 0);
    CHECK(strcmp(matchName(scoped, &out[2]), "character.position.x") == 0);

    // The payload is the field's VariableSlot* (pointer preserved).
    VariableSlot *slot = (VariableSlot*) out[0].payload;
    CHECK(VariableSlot_getPointer(slot) == 3u);

    SegmentIndex_free(scoped);
    VariableMiniMap_free(fields);
}

// Runs map lifecycle, key policy, projection, iteration, and scoped-search cases.
int main(void) {
    printf("=== VariableMiniMap (scoped) Test Suite ===\n\n");

    testLifecycle();
    testBucketsAndFold();
    testRejection();
    testForEachAndStrings();
    testScopedSearch();

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0)
        printf("=== VariableMiniMap Test Suite: ALL PASS ===\n");
    return g_fail == 0 ? 0 : 1;
}
