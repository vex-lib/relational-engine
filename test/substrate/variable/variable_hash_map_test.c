#include "lang/annotation.h"

#include <stdio.h>
#include <string.h>
#include "vex.h"

#include "nio/mem.h"
#include "type/type.h"
#include "substrate/variable/variable_hash_map.h"
#include "substrate/variable/variable_slot.h"
#include "vexspoke/util/hash.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: VariableHashMapTest (variable_hash_map_test/test.c)
 * LEVEL: L3 — Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the relational name => pointer hash map (hash system B):
 * lowercase folding, the 39 first-char buckets, the per-bucket 1024 dashcode
 * slots, forced collisions (chaining inside a slot), upsert semantics,
 * rejection of illegal names, both string projections, and null-safety.
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

// Mirror the map's slot index so the test can force a real collision.
static u32 slotOf(const char *name) {
    char folded[VARIABLE_SLOT_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return 0xFFFFFFFFu;
    const char *tail = folded + 1;
    u64 seed = Hash_fnv1a64((const u8*) tail, strlen(tail));
    return (u32)(Hash_murmur3Mix64(seed) & 1023u);
}

// Checks initialization, empty lookup defaults, null receiver behavior, and idempotent shutdown.
static void testLifecycle(void) {
    printf("[1] init / empty / count / null-safety\n");

    VariableHashMap map;
    CHECK(VariableHashMap_init(&map) == true);
    CHECK(VariableHashMap_count(&map) == 0u);
    CHECK(VariableHashMap_isEmpty(&map) == true);
    CHECK(VariableHashMap_contains(&map, "anything") == false);

    CHECK(VariableHashMap_get(&map, "anything") == 0u);

    CHECK(VariableHashMap_init(nullptr) == false);
    CHECK(VariableHashMap_count(nullptr) == 0u);
    CHECK(VariableHashMap_isEmpty(nullptr) == true);
    CHECK(VariableHashMap_contains(nullptr, "x") == false);
    CHECK(VariableHashMap_get(nullptr, "x") == 0u);
    CHECK(VariableHashMap_add(nullptr, "x", 1u) == false);
    VariableHashMap_shutdown(nullptr);

    VariableHashMap_shutdown(&map);
    CHECK(VariableHashMap_count(&map) == 0u);
    VariableHashMap_shutdown(&map);
    CHECK(true);
}

// Checks case folding, lookup, and same-key upsert without duplicate count growth.
static void testFoldAndBucket(void) {
    printf("[2] lowercase folding + upsert semantics\n");

    VariableHashMap map;
    VariableHashMap_init(&map);

    CHECK(VariableHashMap_add(&map, "helloWorld", 0xAAu) == true);
    CHECK(VariableHashMap_count(&map) == 1u);

    CHECK(VariableHashMap_get(&map, "helloworld") == 0xAAu);
    CHECK(VariableHashMap_get(&map, "HELLOWORLD") == 0xAAu);
    CHECK(VariableHashMap_get(&map, "helloWorld") == 0xAAu);
    CHECK(VariableHashMap_contains(&map, "HelloWorld") == true);

    CHECK(VariableHashMap_add(&map, "HEALTH", 1u) == true);
    CHECK(VariableHashMap_add(&map, "health", 2u) == true);
    CHECK(VariableHashMap_count(&map) == 2u);
    CHECK(VariableHashMap_get(&map, "health") == 2u);

    VariableHashMap_shutdown(&map);
}

// Admits and resolves a key in every supported first-character bucket.
static void testAllBuckets(void) {
    printf("[3] every first-char bucket is reachable (39 of them)\n");

    VariableHashMap map;
    VariableHashMap_init(&map);

    const char *firsts = "abcdefghijklmnopqrstuvwxyz0123456789_$-";
    bytes n = strlen(firsts);
    CHECK(n == 39u);

    for (bytes i = 0; i < n; i++) {
        char name[4] = { firsts[i], '1', '\0' };
        CHECK(VariableHashMap_add(&map, name, 0x100u + (uintptr_t) i) == true);
    }
    CHECK(VariableHashMap_count(&map) == 39u);

    for (bytes i = 0; i < n; i++) {
        char name[4] = { firsts[i], '1', '\0' };
        CHECK(VariableHashMap_get(&map, name) == 0x100u + (uintptr_t) i);
    }

    VariableHashMap_shutdown(&map);
}

// Checks invalid-name rejection and accepted dotted/name-length boundary forms.
static void testRejection(void) {
    printf("[4] illegal / malformed names rejected; dotted names accepted\n");

    VariableHashMap map;
    VariableHashMap_init(&map);

    CHECK(VariableHashMap_add(&map, nullptr, 1u) == false);
    CHECK(VariableHashMap_add(&map, "", 1u) == false);
    CHECK(VariableHashMap_add(&map, "abcdefghijklmnopqrstuvwx", 1u) == false);
    CHECK(VariableHashMap_add(&map, ".a", 1u) == false);
    CHECK(VariableHashMap_add(&map, "a.", 1u) == false);
    CHECK(VariableHashMap_add(&map, "a..b.sh", 1u) == false);
    CHECK(VariableHashMap_add(&map, "has space", 1u) == false);
    CHECK(VariableHashMap_add(&map, "character.position.x", 1u) == true);
    CHECK(VariableHashMap_add(&map, "23charsokabcdefghijklmn", 1u) == true);
    CHECK(VariableHashMap_count(&map) == 2u);

    CHECK(VariableHashMap_get(&map, "CHARACTER.POSITION.X") == 1u);

    VariableHashMap_shutdown(&map);
}

// Finds two distinct keys sharing a bucket/slot and verifies both remain retrievable.
static void testCollisionChain(void) {
    printf("[5] forced collision: two names share a bucket+slot and both resolve\n");

    bool seen[1024] = { false };
    u32 owner[1024] = { 0u };
    u32 firstI = 0u;
    u32 secondI = 0u;
    bool found = false;
    for (u32 i = 0u; i < 4096u && !found; i++) {
        char name[16];
        snprintf(name, sizeof(name), "z%u", i);
        u32 s = slotOf(name);
        if (s >= 1024u)
            break;
        if (seen[s]) {
            found = true;
            firstI = owner[s];
            secondI = i;
            break;
        }
        seen[s] = true;
        owner[s] = i;
    }
    CHECK(found);

    if (found) {
        char a[16];
        char b[16];
        snprintf(a, sizeof(a), "z%u", firstI);
        snprintf(b, sizeof(b), "z%u", secondI);
        CHECK(slotOf(a) == slotOf(b));

        VariableHashMap map;
        VariableHashMap_init(&map);
        CHECK(VariableHashMap_add(&map, a, 0x111u) == true);
        CHECK(VariableHashMap_add(&map, b, 0x222u) == true);
        CHECK(VariableHashMap_count(&map) == 2u);

        CHECK(VariableHashMap_get(&map, a) == 0x111u);
        CHECK(VariableHashMap_get(&map, b) == 0x222u);
        VariableHashMap_shutdown(&map);
    }
}

// Inserts and resolves 3,000 same-bucket names to exercise chained-slot volume.
static void testVolume(void) {
    printf("[6] volume: 3000 same-bucket names, all resolve (chained slots)\n");

    VariableHashMap map;
    VariableHashMap_init(&map);

    const u32 total = 3000u;
    for (u32 i = 0u; i < total; i++) {
        char name[16];
        snprintf(name, sizeof(name), "var%u", i);
        if (VariableHashMap_add(&map, name, 0x5000u + i) != true) {
            CHECK(false);
            break;
        }
    }
    CHECK(VariableHashMap_count(&map) == total);

    bool ok = true;
    for (u32 i = 0u; i < total; i++) {
        char name[16];
        snprintf(name, sizeof(name), "var%u", i);
        if (VariableHashMap_get(&map, name) != 0x5000u + i) {
            ok = false;
            break;
        }
    }
    CHECK(ok);

    VariableHashMap_shutdown(&map);
}

// Checks value/structure projections, null rendering, and truncation reporting.
static void testStrings(void) {
    printf("[7] toString / toStringStruct: bounded + truncation flagged\n");

    VariableHashMap map;
    VariableHashMap_init(&map);
    VariableHashMap_add(&map, "health", 0u);

    char buf[128];
    bool truncated = true;
    VariableHashMap_toString(&map, buf, sizeof(buf), &truncated);
    CHECK(truncated == false && strcmp(buf, "VariableHashMap(count=1)") == 0);

    VariableHashMap_toStringStruct(&map, buf, sizeof(buf), &truncated);
    CHECK(truncated == false &&
          strcmp(buf, "VariableHashMap { active=true, count=1, bands=<array[39]> }") == 0);

    char tiny[4];
    VariableHashMap_toString(&map, tiny, sizeof(tiny), &truncated);
    CHECK(truncated == true);

    truncated = true;
    VariableHashMap_toString(nullptr, buf, sizeof(buf), &truncated);
    CHECK(truncated == false && strcmp(buf, "nullptr") == 0);

    VariableHashMap_shutdown(&map);
}

// Exercises the zero-argument constructor chooser and null-safe freeing.
static void testArity(void) {
    printf("[8] arity constructor + free\n");

    VariableHashMap *map = VariableHashMap();
    CHECK(map != nullptr);
    if (map) {
        CHECK((*map).active == true);
        CHECK(VariableHashMap_add(map, "score", 0x42u) == true);
        CHECK(VariableHashMap_get(map, "score") == 0x42u);
        VariableHashMap_free(map);
    }
    VariableHashMap_free(nullptr);
    CHECK(true);
}

typedef struct VisitProbe {
    u32 visited;
    u32 sum;
    bool sawDotted;
} VisitProbe;

// Accumulates visited pointers and records whether the dotted-name entry was seen.
static void visitFn(VariableSlot *slot, void *userdata) {
    VisitProbe *p = (VisitProbe*) userdata;
    (*p).visited++;
    (*p).sum += (u32) VariableSlot_getPointer(slot);
    if (VariableSlot_nameEquals(slot, "character.position.x"))
        (*p).sawDotted = true;
}

// Checks forEach visits each live entry and safely ignores null callback/receiver.
static void testForEach(void) {
    printf("[9] forEach visits every live entry\n");

    VariableHashMap map;
    VariableHashMap_init(&map);
    VariableHashMap_add(&map, "health", 10u);
    VariableHashMap_add(&map, "score", 20u);
    VariableHashMap_add(&map, "character.position.x", 30u);

    VisitProbe probe = { 0u, 0u, false };
    VariableHashMap_forEach(&map, visitFn, &probe);
    CHECK(probe.visited == 3u);
    CHECK(probe.sum == 60u);
    CHECK(probe.sawDotted == true);

    VariableHashMap_forEach(&map, nullptr, &probe);
    VariableHashMap_forEach(nullptr, visitFn, &probe);
    CHECK(probe.visited == 3u);
    VariableHashMap_shutdown(&map);
}

// Runs lifecycle, key policy, collision/growth, projection, constructor, and iteration checks.
int main(void) {
    printf("=== VariableHashMap Test Suite ===\n\n");

    testLifecycle();
    testFoldAndBucket();
    testAllBuckets();
    testRejection();
    testCollisionChain();
    testVolume();
    testStrings();
    testArity();
    testForEach();

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0)
        printf("=== VariableHashMap Test Suite: ALL PASS ===\n");
    return g_fail == 0 ? 0 : 1;
}
