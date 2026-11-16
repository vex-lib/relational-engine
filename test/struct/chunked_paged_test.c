#include "vex.h"
#include "struct/chunked_list.h"
#include "type/re_type.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: ChunkedPagedTest (chunked_paged_test/test.c)
 * LEVEL: L3 — Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the radix (paged) ChunkedList form: the variadic macro,
 * the *_LAYER_DEFAULT FourCC sentinels, arbitrary depth (2/3/4 levels),
 * shift/mask geometry, row addresses stable across root growth, leaf resolve
 * across level boundaries, and the growable root's no-ceiling property.
 *
 * STRUCT FIELDS: none — procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static i32 g_fail = 0;
static i32 g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(cond)) {                                                    \
            g_fail++;                                                     \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);              \
        }                                                                 \
    } while (0)

typedef struct Row16 {
    u32 a;
    u32 b;
    u32 c;
    u32 d;
} Row16;

// Confirms the public sentinel values used by paged chunk traversal.
static fn testSentinels() {
    printf("[1] sentinels: FourCC sugars are negative and byte-exact\n");

    // INTENTIONAL(vex): the values are the FourCCs with the high bit set.
    CHECK(CHUNKED_LIST_TWO_LAYER_DEFAULT < 0);

    CHECK(CHUNKED_LIST_THREE_LAYER_DEFAULT < 0);

    CHECK((u32) CHUNKED_LIST_TWO_LAYER_DEFAULT == 0xD4574F21u);

    CHECK((u32) CHUNKED_LIST_THREE_LAYER_DEFAULT == 0xD4485245u);
}

// Checks page geometry and index-to-page calculations at their boundaries.
static fn testGeometry() {
    printf("[2] radix geometry: explicit depth, page leaf, pow2 rounding\n");

    // Three explicit levels, small radices so boundaries are cheap to cross.
    ChunkedList *l3 = ChunkedList(ID_INT, 16u, 4, 4, 4);

    CHECK(l3 != nullptr);

    if (l3) {
        CHECK(ChunkedList_getLevels(l3) == 3u);

        CHECK(ChunkedList_getRowsPerChunk(l3) == 4u);

        CHECK(ChunkedList_getChunkBytes(l3) == 64u); // 4 rows * 16 B

        CHECK(ChunkedList_stride(l3) == 16u);

        CHECK(ChunkedList_size(l3) == 0u);

        ChunkedList_free(l3);
    }

    // Four levels.
    ChunkedList *l4 = ChunkedList(ID_INT, 16u, 2, 2, 2, 2);

    CHECK(l4 != nullptr);

    if (l4) {
        CHECK(ChunkedList_getLevels(l4) == 4u);

        CHECK(ChunkedList_getRowsPerChunk(l4) == 2u);

        ChunkedList_free(l4);
    }

    // Non-power-of-two radices round DOWN to a power of two.
    ChunkedList *round = ChunkedList(ID_INT, 16u, 100, 100, 20);

    CHECK(round != nullptr);

    if (round) {
        CHECK(ChunkedList_getRowsPerChunk(round) == 16u); // pow2Floor(20)

        ChunkedList_free(round);
    }
}

// Verifies initial layer geometry and its empty-state defaults.
static fn testLayerDefaults() {
    printf("[3] *_LAYER_DEFAULT presets select depth + page leaf\n");

    ChunkedList *two = ChunkedList(ID_INT, 16u, CHUNKED_LIST_TWO_LAYER_DEFAULT);

    CHECK(two != nullptr);

    if (two) {
        CHECK(ChunkedList_getLevels(two) == 2u);

        CHECK(ChunkedList_getRowsPerChunk(two) == 256u); // pow2Floor(4096/16)

        CHECK(ChunkedList_getChunkBytes(two) == 4096u);

        ChunkedList_free(two);
    }

    ChunkedList *three = ChunkedList(ID_INT, 16u, CHUNKED_LIST_THREE_LAYER_DEFAULT);

    CHECK(three != nullptr);

    if (three) {
        CHECK(ChunkedList_getLevels(three) == 3u);

        CHECK(ChunkedList_getRowsPerChunk(three) == 256u);

        ChunkedList_free(three);
    }

    // A sentinel plus an explicit number overrides that level.
    ChunkedList *ov = ChunkedList(ID_INT, 16u, CHUNKED_LIST_THREE_LAYER_DEFAULT, 8);

    CHECK(ov != nullptr);

    if (ov) {
        CHECK(ChunkedList_getLevels(ov) == 3u);

        ChunkedList_free(ov);
    }
}

// Exercises multi-level growth and checks that previously admitted entries remain accessible.
static fn testDeepGrowth() {
    printf("[4] 3-level growth: stable rows across root doubling, leaf boundaries\n");

    ChunkedList *list = ChunkedList(ID_INT, 16u, 4, 4, 4); // 64 rows per root slot

    if (!list) {
        CHECK(false);

        return;
    }

    // 1100 rows: crosses the initial 64-slot root (needs 69 slots) so the COW
    // root f64s, and crosses many leaves/internal nodes.
    const u32 total = 1100u;

    u8 *first = nullptr;

    for (u32 i = 0u; i < total; i++) {
        Row16 *r = (Row16*) ChunkedList_addSlot(list);

        if (!r) {
            CHECK(false);

            break;
        }

        if (i == 0u) 
            first = (u8*) r;

        (*r).a = i;

        (*r).b = i + 1u;

        (*r).c = i + 2u;

        (*r).d = i + 3u;
    }

    CHECK(ChunkedList_size(list) == total);

    CHECK(ChunkedList_getLevels(list) == 3u);

    CHECK(ChunkedList_capacity(list) >= total);

    CHECK(ChunkedList_getChunkCount(list) == (total + 3u) / 4u); // 275 leaves

    // Row 0 kept its address AND its Bytes across the root doubling.
    CHECK(ChunkedList_slot(list, 0u) == first);

    if (first) {
        Row16 *r0 = (Row16*) first;

        CHECK((*r0).a == 0u && (*r0).b == 1u && (*r0).c == 2u && (*r0).d == 3u);
    }

    // Every row resolves at its original contents (no holes below size).
    bool intact = true;

    for (u32 i = 0u; i < total; i++) {
        Row16 *r = (Row16*) ChunkedList_slot(list, i);

        if (!r || (*r).a != i || (*r).d != i + 3u) {
            intact = false;

            break;
        }
    }

    CHECK(intact);

    // Leaf resolve across level boundaries, and out-of-range stays null.
    CHECK(ChunkedList_getChunk(list, 0u) == first);

    CHECK(ChunkedList_getChunk(list, 4u) != nullptr);          // second root slot

    CHECK(ChunkedList_getChunk(list, 274u) != nullptr);        // last live leaf

    CHECK(ChunkedList_getChunk(list, 275u) == nullptr);        // not published

    CHECK(ChunkedList_slot(list, total) == nullptr);

    // packInto still works over the deep table.
    Row16 flat[8];

    u32 rows = 0u;

    bool truncated = true;

    memset(flat, 0, sizeof(flat));

    CHECK(ChunkedList_packInto(list, (u8*) flat, sizeof(flat), &rows, &truncated) == false);

    CHECK(rows == 8u && truncated == true);

    CHECK(flat[0].a == 0u && flat[4].a == 4u && flat[7].a == 7u);

    ChunkedList_free(list);
}

typedef struct ChainProbe {
    u32 chunks;
    bool ok;
} ChainProbe;

// Records each chunk and index visited by the chain traversal callback.
static fn chainFn(u8 *chunk, u32 index, any userdata) {
    ChainProbe *p = (ChainProbe*) userdata;

    if (!chunk || index != (*p).chunks) 
        (*p).ok = false;

    (*p).chunks++;
}

// Verifies chain traversal visits the expected chunks in order.
static fn testChain() {
    printf("[5] leaf tail-link chain: nextChunk + forEachChunk\n");

    ChunkedList *list = ChunkedList(ID_INT, 16u, 4, 4, 4); // leaf rows 4

    if (!list) {
        CHECK(false);

        return;
    }

    const u32 total = 30u; // -> 8 leaves, last partially filled

    for (u32 i = 0u; i < total; i++) {
        Row16 *r = (Row16*) ChunkedList_addSlot(list);

        if (!r) {
            CHECK(false);

            break;
        }

        (*r).a = i;
    }

    u32 chunks = ChunkedList_getChunkCount(list);

    CHECK(chunks == 8u);

    // The chain visits exactly the indexed leaves, in order.
    u8 *chunk = ChunkedList_getChunk(list, 0u);

    CHECK(chunk != nullptr);

    u32 walked = 0u;

    bool matches = true;

    while (chunk != nullptr) {
        if (chunk != ChunkedList_getChunk(list, walked)) 
            matches = false;

        walked++;

        chunk = ChunkedList_nextChunk(list, chunk);

        if (walked > chunks + 1u) { // guard a broken chain from hanging the test
            matches = false;

            break;
        }
    }

    CHECK(matches);

    CHECK(walked == chunks);

    // The tail has no next; a null leaf has no next.
    u8 *tail = ChunkedList_getChunk(list, chunks - 1u);

    CHECK(ChunkedList_nextChunk(list, tail) == nullptr);

    CHECK(ChunkedList_nextChunk(list, nullptr) == nullptr);

    CHECK(ChunkedList_nextChunk(nullptr, tail) == nullptr);

    // forEachChunk visits every leaf with an ascending index.
    ChainProbe probe = { 0u, true };

    ChunkedList_forEachChunk(list, chainFn, &probe);

    CHECK(probe.ok);

    CHECK(probe.chunks == chunks);

    // Indexed random access still resolves (the chain did not disturb rows).
    Row16 *r0 = (Row16*) ChunkedList_slot(list, 0u);

    Row16 *r29 = (Row16*) ChunkedList_slot(list, 29u);

    CHECK(r0 != nullptr && (*r0).a == 0u);

    CHECK(r29 != nullptr && (*r29).a == 29u);

    ChunkedList_free(list);
}

// Runs the paged-chunk sentinel, geometry, growth, and chain callback checks.
i32 main() {
    printf("=== ChunkedList Paged (radix) Test Suite ===\n\n");

    testSentinels();

    testGeometry();

    testLayerDefaults();

    testDeepGrowth();

    testChain();

    printf("\n%d checks, %d failed\n", g_checks, g_fail);

    if (g_fail == 0) 
        printf("=== ChunkedList Paged Test Suite: ALL PASS ===\n");

    return g_fail == 0 ? 0 : 1;
}
