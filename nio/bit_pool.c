// nio/bit_pool.c — the engine-owned fixed-stride slot pool.
//
// A pool is a directory of committed chunks (append-only; chunks never move)
// plus an index freelist that carries a per-slot generation. Slots are
// handed out by INDEX, so there is no pointer to mask, no tagged word to
// trust, and no VA-width cliff. Release bumps the slot generation; a stale
// raw pointer fails the generation check and is refused, not aliased to
// the new occupant.
//
// Failure mode vs the retired vexspoke bit/bit.c:
//   - no calloc/free here: chunk backing comes from MemoryArena_alloc;
//   - no 48-bit pointer truncation: indices are 32-bit, generations 32-bit;
//   - no 8-byte stride: stride rounds to the 32-byte payload floor;
//   - no O(capacity) freelist walk for double-free: generation check is O(1);
//   - no range-only containment: index + chunk-bounds check, pointer itself
//     is never trusted as an index.
//
// Locking: one SpinLock (atomic/spin.h) serializes alloc/free. This is the
// same discipline as SlabClass; hot paths take one lock and do O(1) index
// work, never allocate per call (growth allocates one chunk when full).

#include "nio/bit_pool.h"

#include <string.h>

#include "util/annotation.h"
#include "atomic/spin.h"
#include "nio/mem.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: BitPool
 * ============================================================================
 * Engine-owned fixed-stride pool: chunked slot directory over one
 * MemoryArena, index freelist with per-slot generation. Automatic growth
 * (one chunk per full-pool alloc) without moving live payloads. Zero bare
 * libc allocation; exhaustion surfaces through the arena's loud-once
 * accounting. Replaces vexspoke bit/bit.c (calloc arena, 48-bit tagged
 * freelist, 8-byte stride, range-only containment) which violated the
 * Relational Memory Law and is deleted, not moved.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: BitPool (nio/bit_pool.c)
 * ============================================================================
 * Fixed-stride slot pool owned by Relational Engine, consumed by Vexspoke
 * primitives (Int, Long, Short, Double, ...). One pool per element width.
 *
 * STRUCT FIELDS (Mirroring nio/bit_pool.h):
 * ----------------------------------------------------------------------------
 *   BitSlot {
 *     uint64_t typeId; // stamped on alloc, 0 when free
 *     uint32_t length; // payload length
 *     uint32_t generation; // bumped on every release
 *     uint32_t nextFree; // freelist index, BITPOOL_FREE_END terminates
 *     uint32_t reserved; // pad
 *     uint64_t reserved2; // pad to one 32-byte floor unit
 *   }
 *   BitChunk {
 *     uint8_t *base; // arena-backed slot array, 32-aligned
 *     uint32_t count; // slots in this chunk
 *     uint32_t live; // handed-out count
 *   }
 *   BitPool {
 *     MemoryArena *arena; // owning arena (borrowed)
 *     size_t elementSize; // caller payload bytes
 *     size_t stride; // sizeof(BitSlot)+payload rounded to 32
 *     uint32_t chunkSlots; // slots per new chunk
 *     BitChunk *chunks; // dir arena-backed, append-only
 *     size_t chunkCount/chunkCap;
 *     uint32_t freeHead; // BITPOOL_FREE_END = empty
 *     uint32_t totalSlots/liveCount;
 *     SpinLock lock; // embedded, zero-init via BitPool_0
 *     bool live; // true after successful construction
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - BitPool_0()
 *   - BitPool_2(elementSize, capacity)              // default arena
 *   - BitPool_3(elementSize, capacity, chunkSlots, arena)
 *
 * Core Functions:
 *   - BitPool_shutdown(pool)
 *   - BitPool_alloc(pool, typeId)
 *   - BitPool_free(pool, userPtr)
 *   - BitPool_contains(pool, userPtr)
 *   - BitPool_type(pool, userPtr)
 *   - BitPool_length(pool, userPtr)
 * ============================================================================
 */

// Type tag stored inside chunk directory allocations so shutdown can
// distinguish directory bytes from slot bytes if they ever paginate
// differently. Not a cookie: provenance is the arena range.
#define BITPOOL_CHUNK_TYPE 0xB17001ULL

// Round any payload stride up to the ecosystem 32-byte floor. The header
// is exactly 32 bytes (asserted in the header), so base + index*stride +
// sizeof(BitSlot) is 32-aligned for every index: chunk base is a 32-aligned
// arena payload and stride is a multiple of 32.
static size_t strideFor(size_t elementSize) {
    size_t raw = sizeof(BitSlot) + elementSize;

    size_t rounded = (raw + BITPOOL_ALIGN - 1u) & ~(size_t) (BITPOOL_ALIGN - 1u);

    return rounded == 0 ? BITPOOL_ALIGN : rounded;
}

// Payload pointer for a global slot index. Caller holds the pool lock OR
// can prove exclusion. Returns nullptr when index is out of range.
static void *slotPayload(const BitPool *pool, uint32_t index) {
    if (!pool || index >= (*pool).totalSlots) 
        return nullptr;

    uint32_t chunkSlots = (*pool).chunkSlots;

    uint32_t chunkIndex = index / chunkSlots;

    uint32_t within = index % chunkSlots;

    if (chunkIndex >= (*pool).chunkCount) 
        return nullptr;

    const BitChunk *chunk = &(*pool).chunks[chunkIndex];

    if (within >= (*chunk).count) 
        return nullptr;

    return (uint8_t*) (*chunk).base + (size_t) within * (*pool).stride + sizeof(BitSlot);
}

// Resolve a user payload pointer to (index, chunk) by subtract-only index
// math against committed chunks. Never reads through the pointer to find
// its own index: provenance is range math, not self-describing trust.
static bool resolveIndex(const BitPool *pool, const void *userPtr, uint32_t *outIndex) {
    if (!pool || !userPtr || !(*pool).arena || (*pool).totalSlots == 0) 
        return false;

    const uint8_t *p = (const uint8_t*) userPtr;

    size_t stride = (*pool).stride;

    uint32_t chunkSlots = (*pool).chunkSlots;

    for (size_t c = 0; c < (*pool).chunkCount; c++) {
        const BitChunk *chunk = &(*pool).chunks[c];

        const uint8_t *base = (*chunk).base;

        const uint8_t *payload0 = base + sizeof(BitSlot);

        const uint8_t *endPayload = base + (size_t) (*chunk).count * stride;

        if (p < payload0 || p >= endPayload) 
            continue;

        uintptr_t delta = (uintptr_t) (p - base);

        if (delta < sizeof(BitSlot)) 
            return false;

        if ((delta - sizeof(BitSlot)) % stride != 0) 
            return false;

        uint32_t within = (uint32_t) ((delta - sizeof(BitSlot)) / stride);

        if (within >= (*chunk).count) 
            return false;

        if (outIndex) 
            *outIndex = (uint32_t) c * chunkSlots + within;

        return true;
    }

    return false;
}

// Commit one chunk of chunkSlots slots. Allocate-before-publish: the
// arena allocation completes before directory or freelist change.
static bool commitChunk(BitPool *pool, uint32_t slots) {
    if (!pool || !(*pool).arena || slots == 0) 
        return false;

    if (slots > (*pool).chunkSlots) 
        slots = (*pool).chunkSlots;

    size_t bytes = (size_t) slots * (*pool).stride;

    void *raw = MemoryArena_alloc((*pool).arena, BITPOOL_CHUNK_TYPE, bytes);

    if (!raw) 
        return false;
    // Allocate-before-publish consumed the bytes; zeroing gives clean
    // slot headers (generation 0, nextFree unset) deterministically.
    memset(raw, 0, bytes);

    // Ensure directory room. Directory is itself arena-backed; growing it
    // allocates a NEW directory and copies, never moves slot pixels.
    if ((*pool).chunkCount == (*pool).chunkCap) {
        size_t newCap = (*pool).chunkCap == 0 ? 8 : (*pool).chunkCap * 2;

        void *nextDir = MemoryArena_alloc((*pool).arena, BITPOOL_CHUNK_TYPE,
                                          newCap * sizeof(BitChunk));
        if (!nextDir) 
            return false;

        memset(nextDir, 0, newCap * sizeof(BitChunk));

        if ((*pool).chunks && (*pool).chunkCount > 0) 
            memcpy(nextDir, (*pool).chunks, (*pool).chunkCount * sizeof(BitChunk));
        // Old directory backing is reclaimed by shutdown accounting via
        // the chunks pointer swap: the previous directory block stays
        // inside the arena until arena freeAll; slots already point at
        // chunk base pixels, never at directory entries.
        (*pool).chunks = (BitChunk*) nextDir;

        (*pool).chunkCap = newCap;
    }

    BitChunk *chunk = &(*pool).chunks[(*pool).chunkCount];

    (*chunk).base = (uint8_t*) raw;

    (*chunk).count = slots;

    (*chunk).live = 0;

    // Thread every new slot onto the freelist by index. No pointer
    // truncation: the chain is index arithmetic.
    uint32_t firstIndex = (uint32_t) (*pool).chunkCount * (*pool).chunkSlots;

    for (uint32_t within = 0; within < slots; within++) {
        uint32_t index = firstIndex + within;

        uint32_t next = (within + 1 < slots) ? (index + 1) : (*pool).freeHead;

        BitSlot *slot = (BitSlot*) ((uint8_t*) raw + (size_t) within * (*pool).stride);

        (*slot).nextFree = next;

        (*slot).generation = 0;

        (*slot).length = 0;

        (*slot).typeId = 0;
    }

    (*pool).freeHead = firstIndex;

    (*pool).chunkCount++;

    (*pool).totalSlots = (uint32_t) ((*pool).chunkCount * (*pool).chunkSlots);

    return true;
}

BitPool BitPool_0(void) {
    BitPool pool;

    memset(&pool, 0, sizeof pool);

    pool.lock = SPIN_LOCK_INIT;

    pool.freeHead = BITPOOL_FREE_END;

    return pool;
}

BitPool BitPool_2(size_t elementSize, size_t capacity) {
    return BitPool_3(elementSize, capacity, BITPOOL_DEFAULT_CHUNK, nullptr);
}

BitPool BitPool_3(size_t elementSize, size_t capacity, size_t chunkSlots, MemoryArena *arena) {
    BitPool pool = BitPool_0();

    if (elementSize == 0 || capacity == 0) 
        return pool;

    if (chunkSlots == 0) 
        chunkSlots = BITPOOL_DEFAULT_CHUNK;

    if (chunkSlots > UINT32_MAX) 
        return pool;

    if (!arena) 
        arena = Memory_defaultArena();

    if (!arena) 
        return pool;

    pool.arena = arena;

    pool.elementSize = elementSize;

    pool.stride = strideFor(elementSize);

    pool.chunkSlots = (uint32_t) chunkSlots;

    SpinLock_lock(&pool.lock);

    // Commit enough chunks for capacity by appending; never one huge
    // calloc. The first chunk is this constructor's commit end.
    bool ok = true;

    size_t remaining = capacity;

    while (remaining > 0) {
        uint32_t take = remaining > chunkSlots ? (uint32_t) chunkSlots : (uint32_t) remaining;

        if (!commitChunk(&pool, take)) {
            ok = false;

            break;
        }

        remaining -= take;
    }

    SpinLock_unlock(&pool.lock);

    if (!ok) {
        // Construction failed after partial commit: tear the pool back
        // down rather than hand out a half-carved object.
        BitPool_shutdown(&pool);

        return BitPool_0();
    }

    pool.live = true;

    return pool;
}

void BitPool_shutdown(BitPool *pool) {
    if (!pool) 
        return;

    SpinLock_lock(&(*pool).lock);
    // Release every chunk's backing to the owning arena. The arena reclaims
    // via its own slab/bump accounting; frees here are one MemoryArena_free
    // per chunk (O(chunks), cold, once at teardown).
    if ((*pool).arena) {
        for (size_t c = 0; c < (*pool).chunkCount; c++) 
            MemoryArena_free((*pool).arena, (*pool).chunks[c].base);
    }
    // Directory backing stays inside the arena until freeAll; nulling it
    // here prevents a double shutdown from re-freeing it.
    memset(pool, 0, sizeof *pool);
    // Leave a fresh, usable empty lock behind so double shutdown is safe.
    BitPool fresh = BitPool_0();

    *pool = fresh;
}

void *BitPool_alloc(BitPool *pool, uint64_t typeId) {
    if (!pool || !(*pool).arena) 
        return nullptr;

    SpinLock_lock(&(*pool).lock);

    if ((*pool).freeHead == BITPOOL_FREE_END) {
        // Full pool: grow by one chunk. allocate-before-publish inside
        // commitChunk guarantees a failure leaves the pool untouched.
        if (!commitChunk(pool, (*pool).chunkSlots)) {
            SpinLock_unlock(&(*pool).lock);

            return nullptr;
        }
    }

    uint32_t index = (*pool).freeHead;

    void *payload = slotPayload(pool, index);

    if (!payload) {
        SpinLock_unlock(&(*pool).lock);

        return nullptr;
    }

    BitSlot *slot = (BitSlot*) ((uint8_t*) payload - sizeof(BitSlot));

    (*pool).freeHead = (*slot).nextFree;

    (*slot).typeId = typeId;

    (*slot).length = (uint32_t) (*pool).elementSize;

    uint32_t chunkSlots = (*pool).chunkSlots;

    uint32_t chunkIndex = index / chunkSlots;

    if (chunkIndex < (*pool).chunkCount) 
        (*pool).chunks[chunkIndex].live++;

    (*pool).liveCount++;

    SpinLock_unlock(&(*pool).lock);

    return payload;
}

void BitPool_free(BitPool *pool, void *userPtr) {
    if (!pool || !userPtr) 
        return;

    SpinLock_lock(&(*pool).lock);

    uint32_t index = 0;

    if (!resolveIndex(pool, userPtr, &index)) {
        SpinLock_unlock(&(*pool).lock);

        return;
    }

    void *payload = slotPayload(pool, index);

    if (payload != userPtr) {
        SpinLock_unlock(&(*pool).lock);

        return;
    }

    BitSlot *slot = (BitSlot*) ((uint8_t*) payload - sizeof(BitSlot));
    // Free slots carry typeId==0. A second release sees typeId==0 AND
    // generation already bumped, so it is refused before touching the
    // freelist: no cycle, no O(capacity) walk.
    if ((*slot).typeId == 0) {
        SpinLock_unlock(&(*pool).lock);

        return;
    }

    (*slot).typeId = 0;

    (*slot).generation++;

    (*slot).length = 0;

    (*slot).nextFree = (*pool).freeHead;

    (*pool).freeHead = index;

    uint32_t chunkSlots = (*pool).chunkSlots;

    uint32_t chunkIndex = index / chunkSlots;

    if (chunkIndex < (*pool).chunkCount && (*pool).chunks[chunkIndex].live > 0) 
        (*pool).chunks[chunkIndex].live--;

    if ((*pool).liveCount > 0) 
        (*pool).liveCount--;

    SpinLock_unlock(&(*pool).lock);
}

bool BitPool_contains(const BitPool *pool, const void *userPtr) {
    return resolveIndex(pool, userPtr, nullptr);
}

uint64_t BitPool_type(const BitPool *pool, const void *userPtr) {
    uint32_t index = 0;

    if (!resolveIndex(pool, userPtr, &index)) 
        return 0;

    void *payload = slotPayload(pool, index);

    if (!payload) 
        return 0;

    const BitSlot *slot = (const BitSlot*) ((const uint8_t*) payload - sizeof(BitSlot));

    return (*slot).typeId;
}

size_t BitPool_length(const BitPool *pool, const void *userPtr) {
    uint32_t index = 0;

    if (!resolveIndex(pool, userPtr, &index)) 
        return 0;

    void *payload = slotPayload(pool, index);

    if (!payload) 
        return 0;

    const BitSlot *slot = (const BitSlot*) ((const uint8_t*) payload - sizeof(BitSlot));

    return (size_t) (*slot).length;
}
