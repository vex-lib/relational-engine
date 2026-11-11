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
static const u64 TYPE_A = UINT64_C(0x1200000000000041);
static const u64 TYPE_B = UINT64_C(0x1300000000000041);
static bytes heapCalls;
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
void *MemTest_malloc(bytes size) { return failHeap() ? nullptr : malloc(size); }
void *MemTest_calloc(bytes count, bytes elementBytes) { return failHeap() ? nullptr : calloc(count, elementBytes); }
void *MemTest_realloc(void *ptr, bytes size) { return failHeap() ? nullptr : realloc(ptr, size); }

/** Assert every byte, not merely byte zero, survives a rejection or relocation. */
static void checkBytes(const u8 *ptr, bytes length, u8 value) {
    for (bytes i = 0; i < length; i++)
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
    for (bytes i = 0; i < 7; i++) {
        owners[i] = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
        assert(owners[i] != nullptr);
    }
#if defined(MEM_TEST_FAULTS)
    failAfter = 2;
    assert(MemoryArena_create(SLAB_BYTES + BUMP_BYTES) == nullptr);
    failAfter = -1;
#endif
    owners[7] = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(owners[7] != nullptr);
    for (bytes i = 0; i < 8; i++) {
        u8 *p = MemoryArena_alloc(owners[i], TYPE_A, 32);
        assert(p != nullptr && Memory_type(p) == TYPE_A);
        MemoryArena_destroy(owners[i]);
    }
}

/** Exhaust both slab and spill; repeat rejection, preserve bytes and reclaim safely. */
static void exhaustion() {
    MemoryArena *a = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(a != nullptr);
    assert(MemoryArena_exhaustionCount(a) == 0);
    u8 *blocks[SMALL_SLOTS + SPILL_SLOTS];
    bytes calls = heapCalls;
    for (bytes i = 0; i < SMALL_SLOTS + SPILL_SLOTS; i++) {
        blocks[i] = MemoryArena_alloc(a, TYPE_A, SMALL_PAYLOAD);
        assert(blocks[i] != nullptr);
        assert(((pointer) blocks[i] & 15u) == 0);
        memset(blocks[i], (u8) i, SMALL_PAYLOAD);
    }
    assert(MemoryArena_activeBytes(a) == SMALL_SLOTS * 64u + BUMP_BYTES);
    u64 rejects = MemoryArena_exhaustionCount(a);
    for (bytes i = 0; i < 32; i++)
        assert(MemoryArena_alloc(a, TYPE_B, SMALL_PAYLOAD) == nullptr);
    assert(MemoryArena_exhaustionCount(a) == rejects + 32);
    assert(heapCalls == calls);
    assert(MemoryArena_findAll(a, TYPE_A, nullptr, 0) == SMALL_SLOTS + SPILL_SLOTS);
    void *found[3] = { nullptr, (void*) blocks[0], (void*) blocks[1] };
    assert(MemoryArena_findAll(a, TYPE_A, found, 1) == SMALL_SLOTS + SPILL_SLOTS);
    assert(found[1] == blocks[0] && found[2] == blocks[1]);
    void *grown = MemoryArena_realloc(a, blocks[0], 128);
    assert(grown != nullptr);
    Memory_free(grown);
    blocks[0] = nullptr;
    for (bytes i = 1; i < SMALL_SLOTS + SPILL_SLOTS; i++)
        checkBytes(blocks[i], SMALL_PAYLOAD, (u8) i);
    u8 *spill = blocks[SMALL_SLOTS];
    MemoryArena_free(a, spill);
    MemoryArena_free(a, spill);
    assert(Memory_length(spill) == 0);
    u8 *reused = MemoryArena_alloc(a, TYPE_B, SMALL_PAYLOAD);
    assert(reused != nullptr && reused != spill);
    u64 beforeReject = MemoryArena_exhaustionCount(a);
    assert(MemoryArena_alloc(a, TYPE_B, SMALL_PAYLOAD) == nullptr);
    assert(MemoryArena_exhaustionCount(a) == beforeReject + 1);
    assert(MemoryArena_findAll(a, TYPE_A, nullptr, 0) == SMALL_SLOTS + SPILL_SLOTS - 2);
    MemoryArena_freeAll(a);
    assert(MemoryArena_activeBytes(a) == 0);
    assert(MemoryArena_exhaustionCount(a) == 0);
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
    u8 *p = MemoryArena_alloc(a, TYPE_A, BUMP_BYTES - MEMORY_HEADER_SIZE);
    assert(p != nullptr);
    MemoryArena_free(a, p);
    p = MemoryArena_alloc(a, TYPE_A, BUMP_BYTES + 1);
    assert(p == nullptr);
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
    u8 *q = MemoryArena_realloc(b, p, 31);
    assert(q && Memory_type(q) == TYPE_A && Memory_length(q) == 31);
    checkBytes(q, 31, 0xA5);
    assert(Memory_type(p) == 0);
    assert(MemoryArena_findAll(a, TYPE_A, nullptr, 0) == 0);
    assert(MemoryArena_findAll(b, TYPE_A, nullptr, 0) == 1);
    MemoryArena_free(a, q);
    checkBytes(q, 31, 0xA5);
    assert(Memory_length(q) == 31);
    MemoryArena_destroy(a);
    MemoryArena_destroy(b);
}

/** Exhaust every physical size class independently, then free/reuse its last slot. */
static void allClasses() {
    const bytes payloads[] = {64 - MEMORY_HEADER_SIZE, 128 - MEMORY_HEADER_SIZE,
                               256 - MEMORY_HEADER_SIZE, 512 - MEMORY_HEADER_SIZE,
                               1024 - MEMORY_HEADER_SIZE, 2048 - MEMORY_HEADER_SIZE,
                               4096 - MEMORY_HEADER_SIZE};
    const bytes capacities[] = {32768, 32768, 16384, 8192, 4096, 2048, 2048};
    MemoryArena *a = MemoryArena_create(SLAB_BYTES);
    assert(a);
    for (bytes c = 0; c < sizeof(payloads) / sizeof(payloads[0]); c++) {
        bytes calls = heapCalls;
        u8 *last = nullptr;
        for (bytes i = 0; i < capacities[c]; i++) {
            last = MemoryArena_alloc(a, TYPE_A, payloads[c]);
            assert(last && Memory_length(last) == payloads[c]);
            memset(last, 0x5A, payloads[c]);
        }
        assert(MemoryArena_alloc(a, TYPE_A, payloads[c]) == nullptr);
        assert(MemoryArena_realloc(a, last, SIZE_MAX) == nullptr);
        checkBytes(last, payloads[c], 0x5A);
        Memory_free(last);
        u8 *replacement = MemoryArena_alloc(a, TYPE_B, payloads[c]);
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
    u8 *p = MemoryArena_alloc(a, TYPE_A, 48);
    u8 *other = MemoryArena_alloc(a, TYPE_B, 48);
    assert(p && other);
    memset(p, 0, 48);
    memset(other, 0xC3, 48);
    assert(!Memory_similar(p, other));
    assert(MemoryArena_alloc(a, TYPE_A, SIZE_MAX) == nullptr);
    assert(MemoryArena_alloc(a, TYPE_A, (bytes) UINT32_MAX + 1u) == nullptr);
    assert(MemoryArena_alloc(a, TYPE_A, UINT32_MAX) == nullptr);
    for (bytes offset = 1; offset < 48; offset++) {
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
    const bytes sizes[] = {0, 1, 15, 16, 47, 48, 49, 111, 112, 113,
                           239, 240, 241, 495, 496, 497, 1007, 1008, 1009,
                           2031, 2032, 2033, 4079, 4080, 4081, 8192};

    u8 *ptrs[MODEL_SLOTS] = {0};
    bytes lengths[MODEL_SLOTS] = {0};
    u32 random = UINT32_C(0xA110CA7E);
    printf("memory model seed=0x%08x steps=%u\n", random, MODEL_STEPS);
    MemoryArena *a = MemoryArena_create(64u * MIB);
    assert(a);

    for (bytes step = 0; step < MODEL_STEPS; step++) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        bytes slot = random % MODEL_SLOTS;
        bytes length = sizes[(random >> 8) % (sizeof(sizes) / sizeof(sizes[0]))];
        u8 value = (u8) slot;

        if (ptrs[slot]) {
            checkBytes(ptrs[slot], lengths[slot], value);
            assert(Memory_type(ptrs[slot]) == TYPE_A);
            assert(Memory_length(ptrs[slot]) == lengths[slot]);
        }
        if (random & 1u) {
            u8 *next = ptrs[slot] ? MemoryArena_realloc(a, ptrs[slot], length)
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
    bytes identity = (bytes) (pointer) context;
    atomic_fetch_add_explicit(&ready, 1, memory_order_release);
    while (!atomic_load_explicit(&start, memory_order_acquire))
        ;
    for (bytes i = 0; i < WORKER_STEPS; i++) {
        u8 *p = MemoryArena_alloc(threadArena, TYPE_A, 49 + identity);
        assert(p);
        memset(p, (int) identity, 49 + identity);
        checkBytes(p, 49 + identity, (u8) identity);
        Memory_free(p);
    }
    return nullptr;
}

/** Concurrent alloc/free only: registry and resets remain externally excluded. */
static void concurrent() {
    threadArena = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(threadArena);
    pthread_t threads[WORKERS];
    for (bytes i = 0; i < WORKERS; i++)
        assert(pthread_create(&threads[i], nullptr, worker, (void*) (pointer) i) == 0);
    while (atomic_load_explicit(&ready, memory_order_acquire) != WORKERS)
        ;
    atomic_store_explicit(&start, true, memory_order_release);
    for (bytes i = 0; i < WORKERS; i++)
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
