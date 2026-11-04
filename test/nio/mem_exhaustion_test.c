// MODULE: native Memory adversarial owner supplement. Trust is earned at
// exhaustion, failure/recovery and mixed-size histories, not from timing.
// Public seam: Memory/MemoryArena alloc, realloc, free, reset, metadata and
// enumeration. Construction failures inject real libc failures into mem.o only.
// Capacity is finite: exhausting slab+bump storage rejects without heap fallback.
// Legal concurrent scope: disjoint allocations/frees after single-threaded setup;
// registry changes, reset, enumeration and shared-block mutation require exclusion.
// No generational pointer identity, hostile forged checksum rejection, OS OOM,
// downstream lifetime, Windows, or full allocator readiness is claimed.
#include "nio/mem.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Physical slab geometry is a white-box exhaustion oracle, not a workload cap.
// SMALL_PAYLOAD fills one 64-byte slot exactly (header + payload), so the slab
// class, its capacity and the bump stride stay a pure function of the header.
enum { MIB = 1024 * 1024, SLAB_BYTES = 30 * MIB, BUMP_BYTES = 4096,
       SMALL_PAYLOAD = 64 - MEMORY_HEADER_SIZE, SMALL_SLOTS = 32768,
       SPILL_SLOTS = BUMP_BYTES / 64,
       MODEL_SLOTS = 64, MODEL_STEPS = 10000, WORKERS = 4, WORKER_STEPS = 4000 };
static const uint64_t TYPE_A = UINT64_C(0x1200000000000041);
static const uint64_t TYPE_B = UINT64_C(0x1300000000000041);
static size_t heapCalls;
static int failAfter = -1;

/** Inject a failure at a chosen allocation stage, otherwise call actual libc. */
static bool failHeap() {
    heapCalls++;
    if (failAfter < 0)
        return false;
    if (failAfter == 0) {
        failAfter = -1;
        return true;
    }
    failAfter--;
    return false;
}
void *MemTest_malloc(size_t bytes) { return failHeap() ? nullptr : malloc(bytes); }
void *MemTest_calloc(size_t count, size_t bytes) { return failHeap() ? nullptr : calloc(count, bytes); }
void *MemTest_realloc(void *ptr, size_t bytes) { return failHeap() ? nullptr : realloc(ptr, bytes); }

/** Assert every byte, not merely byte zero, survives a rejection or relocation. */
static void checkBytes(const uint8_t *ptr, size_t bytes, uint8_t value) {
    for (size_t i = 0; i < bytes; i++)
        assert(ptr[i] == value);
}

/** Reject partial construction and registry growth, preserving existing owners. */
static void construction() {
#if defined(MEM_TEST_FAULTS)
    for (int stage = 0; stage < 2; stage++) {
        failAfter = stage;
        assert(MemoryArena_create(SLAB_BYTES + BUMP_BYTES) == nullptr);
    }
#endif
    failAfter = -1;
    assert(MemoryArena_create(1) == nullptr);
    MemoryArena *owners[8];
    for (size_t i = 0; i < 7; i++) {
        owners[i] = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
        assert(owners[i] != nullptr);
    }
    // Default occupies registry slot zero: the eighth secondary grows directory.
#if defined(MEM_TEST_FAULTS)
    failAfter = 2;
    assert(MemoryArena_create(SLAB_BYTES + BUMP_BYTES) == nullptr);
    failAfter = -1;
#endif
    owners[7] = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(owners[7] != nullptr);
    for (size_t i = 0; i < 8; i++) {
        uint8_t *p = MemoryArena_alloc(owners[i], TYPE_A, 32);
        assert(p != nullptr && Memory_type(p) == TYPE_A);
        MemoryArena_destroy(owners[i]);
    }
}

/** Exhaust both slab and spill; repeat rejection, preserve bytes and reclaim safely. */
static void exhaustion() {
    MemoryArena *a = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(a != nullptr);
    assert(MemoryArena_exhaustionCount(a) == 0);   // fresh epoch
    uint8_t *blocks[SMALL_SLOTS + SPILL_SLOTS];
    size_t calls = heapCalls;
    for (size_t i = 0; i < SMALL_SLOTS + SPILL_SLOTS; i++) {
        blocks[i] = MemoryArena_alloc(a, TYPE_A, SMALL_PAYLOAD);
        assert(blocks[i] != nullptr);
        assert(((uintptr_t) blocks[i] & 15u) == 0);
        memset(blocks[i], (uint8_t) i, SMALL_PAYLOAD);
    }
    assert(MemoryArena_activeBytes(a) == SMALL_SLOTS * 64u + BUMP_BYTES);
    // Exhaustion is LOUD but bounded: each rejection is counted, exactly once
    // reported (the Exhaustion Loudness Law).
    uint64_t rejects = MemoryArena_exhaustionCount(a);
    for (size_t i = 0; i < 32; i++)
        assert(MemoryArena_alloc(a, TYPE_B, SMALL_PAYLOAD) == nullptr);
    assert(MemoryArena_exhaustionCount(a) == rejects + 32);   // every reject counted
    assert(heapCalls == calls); // Exhaustion may not escape its owner into malloc.
    assert(MemoryArena_findAll(a, TYPE_A, nullptr, 0) == SMALL_SLOTS + SPILL_SLOTS);
    void *found[3] = { nullptr, (void*) blocks[0], (void*) blocks[1] };
    assert(MemoryArena_findAll(a, TYPE_A, found, 1) == SMALL_SLOTS + SPILL_SLOTS);
    assert(found[1] == blocks[0] && found[2] == blocks[1]); // Output canaries.
    void *grown = MemoryArena_realloc(a, blocks[0], 128);
    assert(grown != nullptr); // Other slab available.
    Memory_free(grown);
    blocks[0] = nullptr;
    for (size_t i = 1; i < SMALL_SLOTS + SPILL_SLOTS; i++)
        checkBytes(blocks[i], SMALL_PAYLOAD, (uint8_t) i);
    uint8_t *spill = blocks[SMALL_SLOTS];
    MemoryArena_free(a, spill);
    MemoryArena_free(a, spill);
    assert(Memory_length(spill) == 0);
    // Freed spill remains bump-resident and must NOT enter an unrelated slab list.
    uint8_t *reused = MemoryArena_alloc(a, TYPE_B, SMALL_PAYLOAD);
    assert(reused != nullptr && reused != spill);
    uint64_t beforeReject = MemoryArena_exhaustionCount(a);
    assert(MemoryArena_alloc(a, TYPE_B, SMALL_PAYLOAD) == nullptr);
    assert(MemoryArena_exhaustionCount(a) == beforeReject + 1);
    assert(MemoryArena_findAll(a, TYPE_A, nullptr, 0) == SMALL_SLOTS + SPILL_SLOTS - 2);
    MemoryArena_freeAll(a);
    assert(MemoryArena_activeBytes(a) == 0);
    assert(MemoryArena_exhaustionCount(a) == 0);   // reset starts a fresh epoch
    assert(Memory_type(blocks[SMALL_SLOTS + 1]) == 0);
    assert(Memory_length(blocks[SMALL_SLOTS + 1]) == 0);
    assert(MemoryArena_findAll(a, TYPE_A, nullptr, 0) == 0);
    assert(MemoryArena_alloc(a, TYPE_A, SMALL_PAYLOAD) != nullptr);
    MemoryArena_destroy(a);
}

/** Exercise exact bump capacity, failed realloc, shrink and true owner transfer. */
static void boundaries() {
    MemoryArena *a = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    MemoryArena *b = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(a && b);
    uint8_t *p = MemoryArena_alloc(a, TYPE_A, BUMP_BYTES - MEMORY_HEADER_SIZE);
    assert(p != nullptr); // Exactly 4096 total: slab, not bump.
    MemoryArena_free(a, p);
    p = MemoryArena_alloc(a, TYPE_A, BUMP_BYTES + 1);
    assert(p == nullptr); // Too large for either slab or this bump region.
    MemoryArena_destroy(a);
    a = MemoryArena_create(SLAB_BYTES + BUMP_BYTES + MEMORY_HEADER_SIZE);
    assert(a);
    p = MemoryArena_alloc(a, TYPE_A, BUMP_BYTES);
    assert(p && Memory_length(p) == BUMP_BYTES);
    memset(p, 0xA5, BUMP_BYTES);
    assert(MemoryArena_findAll(a, TYPE_A, nullptr, 0) == 1);
    assert(MemoryArena_realloc(a, p, BUMP_BYTES) == nullptr);
    checkBytes(p, BUMP_BYTES, 0xA5);
    assert(Memory_type(p) == TYPE_A && Memory_length(p) == BUMP_BYTES);
    uint8_t *q = MemoryArena_realloc(b, p, 31);
    assert(q && Memory_type(q) == TYPE_A && Memory_length(q) == 31);
    checkBytes(q, 31, 0xA5);
    assert(Memory_type(p) == 0);
    assert(MemoryArena_findAll(a, TYPE_A, nullptr, 0) == 0);
    assert(MemoryArena_findAll(b, TYPE_A, nullptr, 0) == 1);
    MemoryArena_free(a, q); // Wrong owner must preserve B's bytes.
    checkBytes(q, 31, 0xA5);
    assert(Memory_length(q) == 31);
    MemoryArena_destroy(a);
    MemoryArena_destroy(b);
}

/** Exhaust every physical size class independently, then free/reuse its last slot. */
static void allClasses() {
    // Each payload fills its own 16/128/... slot exactly, so header growth
    // shifts the whole table rather than the class each payload lands in.
    const size_t payloads[] = {64 - MEMORY_HEADER_SIZE, 128 - MEMORY_HEADER_SIZE,
                               256 - MEMORY_HEADER_SIZE, 512 - MEMORY_HEADER_SIZE,
                               1024 - MEMORY_HEADER_SIZE, 2048 - MEMORY_HEADER_SIZE,
                               4096 - MEMORY_HEADER_SIZE};
    const size_t capacities[] = {32768, 32768, 16384, 8192, 4096, 2048, 2048};
    MemoryArena *a = MemoryArena_create(SLAB_BYTES); // Deliberately zero bump space.
    assert(a);
    for (size_t c = 0; c < sizeof(payloads) / sizeof(payloads[0]); c++) {
        size_t calls = heapCalls;
        uint8_t *last = nullptr;
        for (size_t i = 0; i < capacities[c]; i++) {
            last = MemoryArena_alloc(a, TYPE_A, payloads[c]);
            assert(last && Memory_length(last) == payloads[c]);
            memset(last, 0x5A, payloads[c]);
        }
        assert(MemoryArena_alloc(a, TYPE_A, payloads[c]) == nullptr);
        assert(MemoryArena_realloc(a, last, SIZE_MAX) == nullptr);
        checkBytes(last, payloads[c], 0x5A);
        Memory_free(last);
        uint8_t *replacement = MemoryArena_alloc(a, TYPE_B, payloads[c]);
        assert(replacement == last && Memory_type(replacement) == TYPE_B);
        assert(MemoryArena_alloc(a, TYPE_A, payloads[c]) == nullptr);
        assert(heapCalls == calls);
        MemoryArena_freeAll(a);
        assert(Memory_length(last) == 0);
        assert(MemoryArena_activeBytes(a) == 0);
    }
    MemoryArena_destroy(a);
}

/** Reject ordinary hostile metadata/pointers while preserving unrelated state. */
static void hostile() {
    MemoryArena *a = MemoryArena_create(SLAB_BYTES);
    assert(a);
    uint8_t *p = MemoryArena_alloc(a, TYPE_A, 48);
    uint8_t *other = MemoryArena_alloc(a, TYPE_B, 48);
    assert(p && other);
    memset(p, 0, 48);
    memset(other, 0xC3, 48);
    assert(!Memory_similar(p, other)); // Same local class, different full project ID.
    assert(MemoryArena_alloc(a, TYPE_A, SIZE_MAX) == nullptr);
    assert(MemoryArena_alloc(a, TYPE_A, (size_t) UINT32_MAX + 1u) == nullptr);
    assert(MemoryArena_alloc(a, TYPE_A, UINT32_MAX) == nullptr);
    for (size_t offset = 1; offset < 48; offset++) {
        assert(Memory_type(p + offset) == 0);
        assert(Memory_length(p + offset) == 0);
        assert(Memory_realloc(p + offset, 32) == nullptr);
        assert(MemoryArena_realloc(a, p + offset, 32) == nullptr);
        Memory_free(p + offset);
        MemoryArena_free(a, p + offset);
    }
    MemoryHeader *header = (MemoryHeader*) (p - MEMORY_HEADER_SIZE);
    MemoryHeader saved = *header;
    (*header).hash ^= 2u;
    assert(Memory_type(p) == 0 && Memory_length(p) == 0);
    assert(MemoryArena_realloc(a, p, 32) == nullptr);
    Memory_free(p);
    *header = saved;
    (*header).typeId ^= UINT64_C(0x100000000);
    assert(Memory_type(p) == 0);
    *header = saved;
    (*header).length++;
    assert(Memory_length(p) == 0);
    *header = saved;
    assert(Memory_type(p) == TYPE_A && Memory_length(p) == 48);
    checkBytes(other, 48, 0xC3);
    MemoryArena_destroy(a);
}

/** Fixed seeded mixed-size histories assert bytes/type/length after every change. */
static void model() {
    const size_t sizes[] = {0, 1, 15, 16, 47, 48, 49, 111, 112, 113,
                           239, 240, 241, 495, 496, 497, 1007, 1008, 1009,
                           2031, 2032, 2033, 4079, 4080, 4081, 8192};

    uint8_t *ptrs[MODEL_SLOTS] = {0};
    size_t lengths[MODEL_SLOTS] = {0};
    uint32_t random = UINT32_C(0xA110CA7E);
    printf("memory model seed=0x%08x steps=%u\n", random, MODEL_STEPS);
    MemoryArena *a = MemoryArena_create(64u * MIB);
    assert(a);

    for (size_t step = 0; step < MODEL_STEPS; step++) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        size_t slot = random % MODEL_SLOTS;
        size_t length = sizes[(random >> 8) % (sizeof(sizes) / sizeof(sizes[0]))];
        uint8_t value = (uint8_t) slot;

        if (ptrs[slot]) {
            checkBytes(ptrs[slot], lengths[slot], value);
            assert(Memory_type(ptrs[slot]) == TYPE_A);
            assert(Memory_length(ptrs[slot]) == lengths[slot]);
        }
        if (random & 1u) {
            uint8_t *next = ptrs[slot] ? MemoryArena_realloc(a, ptrs[slot], length)
                                       : MemoryArena_alloc(a, TYPE_A, length);
            assert(next);
            checkBytes(next, lengths[slot] < length ? lengths[slot] : length, value);
            ptrs[slot] = next;
            lengths[slot] = length;
        } else {
            MemoryArena_free(a, ptrs[slot]);
            ptrs[slot] = MemoryArena_alloc(a, TYPE_A, length);
            assert(ptrs[slot]);
            lengths[slot] = length;
        }
        memset(ptrs[slot], value, length);
    }
    MemoryArena_destroy(a);
}

static MemoryArena *threadArena;
static atomic_uint ready;
static atomic_bool start;

/** Disjoint worker ownership with an explicit synchronized start, never sleeps. */
static void *worker(void *context) {
    size_t identity = (size_t) (uintptr_t) context;
    atomic_fetch_add_explicit(&ready, 1, memory_order_release);
    while (!atomic_load_explicit(&start, memory_order_acquire))
        ;
    for (size_t i = 0; i < WORKER_STEPS; i++) {
        uint8_t *p = MemoryArena_alloc(threadArena, TYPE_A, 49 + identity);
        assert(p);
        memset(p, (int) identity, 49 + identity);
        checkBytes(p, 49 + identity, (uint8_t) identity);
        Memory_free(p);
    }
    return nullptr;
}

/** Concurrent alloc/free only: registry and resets remain externally excluded. */
static void concurrent() {
    threadArena = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(threadArena);
    pthread_t threads[WORKERS];
    for (size_t i = 0; i < WORKERS; i++)
        assert(pthread_create(&threads[i], nullptr, worker, (void*) (uintptr_t) i) == 0);
    while (atomic_load_explicit(&ready, memory_order_acquire) != WORKERS)
        ;
    atomic_store_explicit(&start, true, memory_order_release);
    for (size_t i = 0; i < WORKERS; i++)
        assert(pthread_join(threads[i], nullptr) == 0);
    assert(MemoryArena_findAll(threadArena, TYPE_A, nullptr, 0) == 0);
    assert(MemoryArena_activeBytes(threadArena) == 0);
    MemoryArena_destroy(threadArena);
}

int main() {
#if !defined(MEM_TEST_FAULTS)
    puts("Scoped native target: use native_run.py for libc fault/no-heap instrumentation");
#endif
    assert(Memory_init(0));
    construction();
    exhaustion();
    boundaries();
    allClasses();
    hostile();
    model();
    concurrent();
    puts("mem_exhaustion_test: all scenarios passed");
}
