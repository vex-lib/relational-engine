#include "vex.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "struct/chunked_list.h"
#include "type/re_type.h"

// vexspoke/test/chunked_list_test.c — never-moved chunked list verifier.
//
// Asserts the properties the class exists for:
//   1. a row address handed out once NEVER moves (growth cannot invalidate it);
//   2. index lookup resolves through published chunks with no lock;
//   3. concurrent writers claim every row exactly once while lock-free readers
//      never observe a hole below the published count;
//   4. extreme size math fails closed instead of wrapping.
// plus the byte-budget chunk math, the COW directory doubling, and the
// dest-last truncation contract of packInto (the Truncation-Never-Silent
// clause).

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

// Checks empty-list defaults, row geometry, and initial capacity.
static fn testDefaults() {
    printf("[1] defaults: 128-byte budget, 16-byte rows -> 8 rows/chunk\n");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, VEX_CHUNKED_BYTES_DEFAULT);

    CHECK(list != nullptr);

    if (!list) 
        return;

    CHECK(ChunkedList_stride(list) == 16u);

    CHECK(ChunkedList_getChunkBytes(list) == 128u);

    CHECK(ChunkedList_getRowsPerChunk(list) == 8u);

    CHECK(ChunkedList_getChunkCount(list) == 0u);

    CHECK(ChunkedList_size(list) == 0u);
    // The Exhaustion Loudness Law accessor: a fresh owner has no refusals.
    CHECK(ChunkedList_exhaustionCount(list) == 0);

    CHECK(ChunkedList_length(list) == 0u);

    CHECK(ChunkedList_capacity(list) == 0u);

    CHECK(ChunkedList_isEmpty(list) == true);

    CHECK(ChunkedList_slot(list, 0) == nullptr);

    CHECK(ChunkedList_getChunk(list, 0) == nullptr);

    // Embed-first contract: a ChunkedList* is a Collection*.
    Collection *c = (Collection*) list;

    CHECK(Collection_size(c) == 0u);

    CHECK(Collection_stride(c) == 16u);

    // Byte budget is a floor of 16 (reject-clamp policy).
    ChunkedList *tiny = ChunkedList_2(ID_INT, 4u);

    CHECK(tiny != nullptr);

    if (tiny) {
        CHECK(ChunkedList_getChunkBytes(tiny) == 16u);
        // Invariant: the chunk never exceeds the budget it was given.
        CHECK(ChunkedList_getRowsPerChunk(tiny) >= 1u);

        CHECK((bytes)ChunkedList_getRowsPerChunk(tiny) * (bytes)ChunkedList_stride(tiny)
              <= (bytes)ChunkedList_getChunkBytes(tiny));
        ChunkedList_free(tiny);
    }

    ChunkedList_free(list);
}

// Verifies row addresses remain stable when the chunk directory grows.
static fn testStableAddresses() {
    printf("[2] stable addresses + chunk boundaries across growth\n");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (!list) 
        return;

    u8 *first = ChunkedList_addSlot(list);

    CHECK(first != nullptr);

    CHECK(ChunkedList_size(list) == 1u);

    CHECK(ChunkedList_getChunkCount(list) == 1u);

    if (!first) {
        ChunkedList_free(list);

        return;
    }

    Row16 *row0 = (Row16*) first;

    (*row0).a = 0xA0u;

    (*row0).b = 0xB0u;

    (*row0).c = 0xC0u;

    (*row0).d = 0xD0u;

    // Fill well past a chunk boundary (8 rows) and past the first directory
    // generation (64 chunks = 512 rows) so COW doubling happens too.
    const u32 total = 1200u;

    for (u32 i = 1u; i < total; i++) {
        u8 *row = ChunkedList_addSlot(list);

        if (!row) {
            CHECK(false);

            break;
        }

        Row16 *r = (Row16*) row;

        (*r).a = i;

        (*r).b = i + 1u;

        (*r).c = i + 2u;

        (*r).d = i + 3u;
    }

    CHECK(ChunkedList_size(list) == total);

    CHECK(ChunkedList_capacity(list) >= total);

    CHECK(ChunkedList_getChunkCount(list) == (total + 7u) / 8u);

    // The very first row must be untouched: same address, same Bytes.
    CHECK(ChunkedList_slot(list, 0u) == first);

    CHECK((*row0).a == 0xA0u);

    CHECK((*row0).b == 0xB0u);

    CHECK((*row0).c == 0xC0u);

    CHECK((*row0).d == 0xD0u);

    // Rows 7 and 8 straddle the chunk boundary -> different chunks.
    u8 *row7 = ChunkedList_slot(list, 7u);

    u8 *row8 = ChunkedList_slot(list, 8u);

    CHECK(row7 != nullptr && row8 != nullptr);

    CHECK(ChunkedList_getChunk(list, 0u) == first);

    CHECK(ChunkedList_getChunk(list, 1u) == row8);

    CHECK(ChunkedList_getChunk(list, 1u) != ChunkedList_getChunk(list, 0u));

    // Every earlier row is still readable at its original contents.
    bool intact = true;

    for (u32 i = 1u; i < 64u; i++) {
        Row16 *r = (Row16*) ChunkedList_slot(list, i);

        if (!r || (*r).a != i || (*r).d != i + 3u) {
            intact = false;

            break;
        }
    }

    CHECK(intact);

    // Out-of-range and unpromoted chunks stay null.
    CHECK(ChunkedList_slot(list, total) == nullptr);

    CHECK(ChunkedList_slot(list, total + 5000u) == nullptr);

    CHECK(ChunkedList_getChunk(list, ChunkedList_getChunkCount(list)) == nullptr);

    ChunkedList_free(list);
}

// Exercises explicit reservation, including its accepted and rejected bounds.
static fn testReserve() {
    printf("[3] reserve pre-allocates chunks without activating rows\n");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (!list) 
        return;

    CHECK(ChunkedList_reserve(list, 100u) == true);

    CHECK(ChunkedList_getChunkCount(list) == 13u);

    CHECK(ChunkedList_capacity(list) == 104u);

    CHECK(ChunkedList_size(list) == 0u);

    CHECK(ChunkedList_isEmpty(list) == true);

    // A reserved chunk is addressable before any row is live.
    u8 *chunk = ChunkedList_getChunk(list, 12u);

    CHECK(chunk != nullptr);

    CHECK(ChunkedList_slot(list, 0u) == nullptr);

    // Growing past the reservation keeps the same chunk addresses.
    u8 *before = ChunkedList_getChunk(list, 0u);

    for (u32 i = 0u; i < 200u; i++) 
        CHECK(ChunkedList_addSlot(list) != nullptr);

    CHECK(ChunkedList_getChunk(list, 0u) == before);

    CHECK(ChunkedList_reserve(list, 10u) == true); // already covered: no-op

    CHECK(ChunkedList_getChunkCount(list) == 25u);

    ChunkedList_free(list);
}

// Checks packing rows into caller-provided storage and preserves output on rejection.
static fn testPackInto() {
    printf("[4] packInto snapshot: dest-last + truncation flag\n");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (!list) 
        return;

    for (u32 i = 0u; i < 20u; i++) {
        Row16 *r = (Row16*) ChunkedList_addSlot(list);

        if (!r) 
            break;

        (*r).a = i;

        (*r).b = i * 2u;

        (*r).c = i * 3u;

        (*r).d = i * 4u;
    }

    CHECK(ChunkedList_size(list) == 20u);

    Row16 flat[20];

    memset(flat, 0, sizeof(flat));

    u32 rows = 0u;

    bool truncated = true;

    CHECK(ChunkedList_packInto(list, (u8*) flat, sizeof(flat), &rows, &truncated) == true);

    CHECK(rows == 20u);

    CHECK(truncated == false);

    CHECK(flat[0].a == 0u && flat[19].a == 19u);

    CHECK(flat[7].d == 28u && flat[8].a == 8u);

    // Short destination: copy what fits, flag the cut loudly.
    Row16 shortDest[3];

    rows = 0u;

    truncated = false;

    CHECK(ChunkedList_packInto(list, (u8*) shortDest, sizeof(shortDest), &rows, &truncated) == false);

    CHECK(rows == 3u);

    CHECK(truncated == true);

    CHECK(shortDest[0].a == 0u && shortDest[2].a == 2u);

    // Null dest / null list: safe false with cleared out-params.
    rows = 99u;

    truncated = true;

    CHECK(ChunkedList_packInto(list, nullptr, 64u, &rows, &truncated) == false);

    CHECK(rows == 0u && truncated == false);

    CHECK(ChunkedList_packInto(nullptr, (u8*) shortDest, sizeof(shortDest), &rows, &truncated) == false);

    // Out-params may be omitted entirely.
    CHECK(ChunkedList_packInto(list, (u8*) flat, sizeof(flat), nullptr, nullptr) == true);

    ChunkedList_free(list);
}

// Verifies chunk-byte budgeting limits admission without corrupting existing rows.
static fn testChunkBytesBudget() {
    printf("[5] chunk byte budget: settable before growth, rejected after\n");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (!list) 
        return;

    CHECK(ChunkedList_getRowsPerChunk(list) == 8u);

    ChunkedList_setChunkBytes(list, 512u);

    CHECK(ChunkedList_getChunkBytes(list) == 512u);

    CHECK(ChunkedList_getRowsPerChunk(list) == 32u);

    // One row per chunk when the row is at least as big as the budget: the row
    // gets a chunk to itself, so neighbours never share that chunk's Bytes.
    // (Chunk alignment stays 16 Bytes — owning a chunk is not owning a cache
    // line.)
    ChunkedList *own = ChunkedList_3(ID_INT, 128u, 128u);

    if (own) {
        CHECK(ChunkedList_getRowsPerChunk(own) == 1u);

        u8 *a = ChunkedList_addSlot(own);

        u8 *b = ChunkedList_addSlot(own);

        CHECK(a != nullptr && b != nullptr);

        CHECK(a != b);

        CHECK(ChunkedList_getChunk(own, 0u) == a);

        CHECK(ChunkedList_getChunk(own, 1u) == b);

        ChunkedList_free(own);
    }

    // After the first chunk the budget is locked (reject).
    CHECK(ChunkedList_addSlot(list) != nullptr);

    ChunkedList_setChunkBytes(list, 1024u);

    CHECK(ChunkedList_getChunkBytes(list) == 512u);

    CHECK(ChunkedList_getRowsPerChunk(list) == 32u);

    ChunkedList_free(list);
}

// Confirms nullable list operations return safe defaults without mutation.
static fn testNullSafety() {
    printf("[6] cold-path null safety (no crash on null self)\n");

    CHECK(ChunkedList_size(nullptr) == 0u);

    CHECK(ChunkedList_length(nullptr) == 0u);

    CHECK(ChunkedList_capacity(nullptr) == 0u);

    CHECK(ChunkedList_isEmpty(nullptr) == true);

    CHECK(ChunkedList_elementClassId(nullptr) == 0u);

    CHECK(ChunkedList_stride(nullptr) == 0u);

    CHECK(ChunkedList_getChunkCount(nullptr) == 0u);

    CHECK(ChunkedList_getRowsPerChunk(nullptr) == 0u);

    CHECK(ChunkedList_getChunkBytes(nullptr) == 0u);

    CHECK(ChunkedList_addSlot(nullptr) == nullptr);

    CHECK(ChunkedList_reserve(nullptr, 10u) == false);

    CHECK(ChunkedList_slot(nullptr, 0u) == nullptr);

    CHECK(ChunkedList_getChunk(nullptr, 0u) == nullptr);

    ChunkedList_setChunkBytes(nullptr, 256u);

    ChunkedList_free(nullptr);

    CHECK(true);
}

// Exercises overflow-checked capacity and byte-size calculations.
static fn testCheckedMath() {
    printf("[7] checked size math: extreme budgets fail closed, never wrap\n");

    // Stride 1 with the maximum budget: rowsPerChunk is 2^31. Construction is
    // math only (no chunks grown), so this stays cheap and allocation-free.
    ChunkedList *huge = ChunkedList_3(ID_INT, 1u, UINT32_MAX);

    CHECK(huge != nullptr);

    if (huge) {
        CHECK(ChunkedList_getRowsPerChunk(huge) == 0x80000000u);

        CHECK(ChunkedList_size(huge) == 0u);

        CHECK(ChunkedList_capacity(huge) == 0u);

        CHECK(ChunkedList_reserve(huge, 0u) == true);

        CHECK(ChunkedList_getChunkCount(huge) == 0u);

        ChunkedList_free(huge);
    }

    // A row far bigger than the budget: exactly one row per chunk, and the
    // budget invariant still holds.
    ChunkedList *wide = ChunkedList_3(ID_INT, 4096u, 128u);

    CHECK(wide != nullptr);

    if (wide) {
        CHECK(ChunkedList_getRowsPerChunk(wide) == 1u);

        CHECK(ChunkedList_addSlot(wide) != nullptr);

        CHECK(ChunkedList_capacity(wide) == 1u);

        ChunkedList_free(wide);
    }

    // reserve(0) grows nothing and succeeds.
    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (list) {
        CHECK(ChunkedList_reserve(list, 0u) == true);

        CHECK(ChunkedList_getChunkCount(list) == 0u);

        ChunkedList_free(list);
    }
}

#define CONC_WRITERS 4
#define CONC_ROWS 1000u
#define CONC_READERS 2
#define CONC_TOTAL (CONC_WRITERS * CONC_ROWS)

static ChunkedList *g_concList;
static _Atomic u32 g_concSeq;
static _Atomic bool g_concDone;

// Appends a fixed batch of rows and stamps each admitted row with a unique sequence.
static any concWriter(any arg) {
    (discard) arg;

    for (u32 i = 0u; i < CONC_ROWS; i++) {
        u8 *row = ChunkedList_addSlot(g_concList);

        if (!row) 
            return (any) 1;

        u32 seq = atomic_fetch_add(&g_concSeq, 1u) + 1u;

        memcpy(row, &seq, sizeof(seq));
    }

    return nullptr;
}

// Scans all published rows until writers finish, rejecting holes or invalid stamps.
static any concReader(any arg) {
    (discard) arg;

    while (!atomic_load(&g_concDone)) {
        u32 n = ChunkedList_size(g_concList);

        for (u32 i = 0u; i < n; i++) {
            u8 *row = ChunkedList_slot(g_concList, i);

            if (!row) 
                return (any) 1; // hole below the published count: defect

            u32 seq = 0u;

            memcpy(&seq, row, sizeof(seq));

            if (seq > (u32) CONC_TOTAL) 
                return (any) 1; // out of range (0 = claimed but not yet stamped)
        }
    }

    return nullptr;
}

// Orders pointer values for deterministic comparison of concurrent writer results.
static i32 cmpPtr(const_any a, const_any b) {
    u8 * const *pa = (u8 * const *) a;

    u8 * const *pb = (u8 * const *) b;

    if (*pa < *pb) 
        return -1;

    if (*pa > *pb) 
        return 1;

    return 0;
}

// Runs concurrent appenders and checks completion and the resulting row set.
static fn testConcurrentWriters() {
    printf("[8] concurrent writers claim every row exactly once; readers see no holes\n");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (!list) {
        CHECK(false);

        return;
    }

    g_concList = list;

    atomic_store(&g_concSeq, 0u);

    atomic_store(&g_concDone, false);

    pthread_t writers[CONC_WRITERS];

    pthread_t readers[CONC_READERS];

    for (i32 i = 0; i < CONC_READERS; i++) 
        pthread_create(&readers[i], nullptr, concReader, nullptr);

    for (i32 i = 0; i < CONC_WRITERS; i++) 
        pthread_create(&writers[i], nullptr, concWriter, nullptr);

    i32 writerFail = 0;

    for (i32 i = 0; i < CONC_WRITERS; i++) {
        any ret = nullptr;

        pthread_join(writers[i], &ret);

        if (ret != nullptr) 
            writerFail = 1;
    }

    atomic_store(&g_concDone, true);

    i32 readerFail = 0;

    for (i32 i = 0; i < CONC_READERS; i++) {
        any ret = nullptr;

        pthread_join(readers[i], &ret);

        if (ret != nullptr) 
            readerFail = 1;
    }

    CHECK(writerFail == 0);

    CHECK(readerFail == 0);

    // Exactly-once claim: stamps must be exactly 1..TOTAL — no gaps, no dups.
    // (A duplicate claim would surface as a repeated stamp plus a i16 size.)
    CHECK(ChunkedList_size(list) == (u32) CONC_TOTAL);

    u8 *seen = (u8*) calloc((bytes) CONC_TOTAL + 1u, 1u);

    u8 **ptrs = (u8**) malloc((bytes) CONC_TOTAL * sizeof(u8*));

    CHECK(seen != nullptr && ptrs != nullptr);

    bool stampsOk = seen != nullptr && ptrs != nullptr;

    for (u32 i = 0u; stampsOk && i < (u32) CONC_TOTAL; i++) {
        u8 *row = ChunkedList_slot(list, i);

        if (!row) {
            stampsOk = false;

            break;
        }

        u32 seq = 0u;

        memcpy(&seq, row, sizeof(seq));

        if (seq < 1u || seq > (u32) CONC_TOTAL || seen[seq]) {
            stampsOk = false;

            break;
        }

        seen[seq] = 1u;

        ptrs[i] = row;
    }

    CHECK(stampsOk);

    // No two rows share an address: chunks never aliased under contention.
    bool distinct = stampsOk;

    if (distinct) {
        qsort(ptrs, (bytes) CONC_TOTAL, sizeof(u8*), cmpPtr);

        for (u32 i = 1u; i < (u32) CONC_TOTAL; i++) 
            if (ptrs[i] == ptrs[i - 1u]) {
                distinct = false;

                break;
            }
    }

    CHECK(distinct);

    free(seen);

    free(ptrs);

    g_concList = nullptr;

    ChunkedList_free(list);
}

// Runs the chunked-list geometry, stability, rejection, and concurrency cases.
i32 main() {
    printf("=== ChunkedList Test Suite ===\n\n");

    testDefaults();

    testStableAddresses();

    testReserve();

    testPackInto();

    testChunkBytesBudget();

    testNullSafety();

    testCheckedMath();

    testConcurrentWriters();

    printf("\n%d checks, %d failed\n", g_checks, g_fail);

    if (g_fail == 0) 
        printf("=== ChunkedList Test Suite: ALL PASS ===\n");

    return g_fail == 0 ? 0 : 1;
}
