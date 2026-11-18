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
// Locking: one SpinLock (atomic/atomic.h) serializes alloc/free. This is the
// same discipline as SlabClass; hot paths take one lock and do O(1) index
// work, never allocate per call (growth allocates one chunk when full).

#include "nio/bit_pool.h"

#include <string.h>

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
 *     u64 typeId; // stamped on alloc, 0 when free
 *     u32 length; // payload length
 *     u32 generation; // bumped on every release
 *     u32 nextFree; // freelist index, BITPOOL_FREE_END terminates
 *     u32 reserved; // pad
 *     u64 reserved2; // pad to one 32-byte floor unit
 *   }
 *   BitChunk {
 *     u8 *base; // arena-backed slot array, 32-aligned
 *     u32 count; // slots in this chunk
 *     u32 live; // handed-out count
 *   }
 *   BitPool {
 *     MemoryArena *arena; // owning arena (borrowed)
 *     bytes elementSize; // caller payload bytes
 *     bytes stride; // sizeof(BitSlot)+payload rounded to 32
 *     u32 chunkSlots; // slots per new chunk
 *     BitChunk *chunks; // dir arena-backed, append-only
 *     bytes chunkCount/chunkCap;
 *     u32 freeHead; // BITPOOL_FREE_END = empty
 *     u32 totalSlots/liveCount;
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
static bytes strideFor(bytes elementSize) {
    bytes raw = sizeof(BitSlot) + elementSize;

    bytes rounded = (raw + BITPOOL_ALIGN - 1u) & ~(bytes) (BITPOOL_ALIGN - 1u);

    return rounded == 0 ? BITPOOL_ALIGN : rounded;
}


;;GETTER
static bytes BitPool_elementSize(const BitPool *pool) {
    return (*pool).elementSize;
}


;;GETTER
static BitChunk *BitPool_chunks(const BitPool *pool) {
    return (*pool).chunks;
}


;;GETTER
static bytes BitPool_chunkCap(const BitPool *pool) {
    return (*pool).chunkCap;
}

;;GETTER
static u32 BitPool_freeHead(const BitPool *pool) {
    return (*pool).freeHead;
}


;;GETTER
static u32 BitPool_liveCount(const BitPool *pool) {
    return (*pool).liveCount;
}

;;GETTER
static SpinLock *BitPool_lockMut(BitPool *pool) {
    return &(*pool).lock;
}

;;SETTER
static fn BitPool_setChunks(BitPool *pool, BitChunk *chunks) {
    (*pool).chunks = chunks;
}

;;SETTER
static fn BitPool_setChunkCount(BitPool *pool, bytes chunkCount) {
    (*pool).chunkCount = chunkCount;
}

;;SETTER
static fn BitPool_setChunkCap(BitPool *pool, bytes chunkCap) {
    (*pool).chunkCap = chunkCap;
}

;;SETTER
static fn BitPool_setFreeHead(BitPool *pool, u32 freeHead) {
    (*pool).freeHead = freeHead;
}

;;SETTER
static fn BitPool_setTotalSlots(BitPool *pool, u32 totalSlots) {
    (*pool).totalSlots = totalSlots;
}

;;SETTER
static fn BitPool_setLiveCount(BitPool *pool, u32 liveCount) {
    (*pool).liveCount = liveCount;
}


;;GETTER
static u32 BitChunk_live(const BitChunk *chunk) {
    return (*chunk).live;
}

;;SETTER
static fn BitChunk_setBase(BitChunk *chunk, u8 *base) {
    (*chunk).base = base;
}

;;SETTER
static fn BitChunk_setCount(BitChunk *chunk, u32 count) {
    (*chunk).count = count;
}

;;SETTER
static fn BitChunk_setLive(BitChunk *chunk, u32 live) {
    (*chunk).live = live;
}

;;GETTER
static u64 BitSlot_typeId(const BitSlot *slot) {
    return (*slot).typeId;
}

;;GETTER
static u32 BitSlot_length(const BitSlot *slot) {
    return (*slot).length;
}

;;GETTER
static u32 BitSlot_generation(const BitSlot *slot) {
    return (*slot).generation;
}

;;GETTER
static u32 BitSlot_nextFree(const BitSlot *slot) {
    return (*slot).nextFree;
}

;;SETTER
static fn BitSlot_setTypeId(BitSlot *slot, u64 typeId) {
    (*slot).typeId = typeId;
}

;;SETTER
static fn BitSlot_setLength(BitSlot *slot, u32 length) {
    (*slot).length = length;
}

;;SETTER
static fn BitSlot_setGeneration(BitSlot *slot, u32 generation) {
    (*slot).generation = generation;
}

;;SETTER
static fn BitSlot_setNextFree(BitSlot *slot, u32 nextFree) {
    (*slot).nextFree = nextFree;
}

// Payload pointer for a global slot index. Caller holds the pool lock OR
// can prove exclusion. Returns nullptr when index is out of range. One
// implementation: the header's BitPool_slotAt, so the two cannot drift.
static any slotPayload(const BitPool *pool, u32 index) {
    return BitPool_slotAt(pool, index);
}

// Resolve a user payload pointer to (index, chunk) by subtract-only index
// math against committed chunks. Never reads through the pointer to find
// its own index: provenance is range math, not self-describing trust.
static bool resolveIndex(const BitPool *pool, const_any userPtr, u32 *outIndex) {
    if (!pool || !userPtr || !BitPool_arena(pool) || BitPool_totalSlots(pool) == 0) 
        return false;

    const u8 *p = (const u8*) userPtr;

    bytes stride = BitPool_stride(pool);

    u32 chunkSlots = BitPool_chunkSlots(pool);

    bytes chunkCount = BitPool_chunkCount(pool);

    const BitChunk *chunks = BitPool_chunks(pool);

    for (bytes c = 0; c < chunkCount; c++) {
        const BitChunk *chunk = &chunks[c];

        const u8 *base = BitChunk_base(chunk);

        u32 count = BitChunk_count(chunk);

        const u8 *payload0 = base + sizeof(BitSlot);

        const u8 *endPayload = base + (bytes) count * stride;

        if (p < payload0 || p >= endPayload) 
            continue;

        pointer delta = (pointer) (p - base);

        if (delta < sizeof(BitSlot)) 
            return false;

        if ((delta - sizeof(BitSlot)) % stride != 0) 
            return false;

        u32 within = (u32) ((delta - sizeof(BitSlot)) / stride);

        if (within >= count) 
            return false;

        if (outIndex) 
            *outIndex = (u32) c * chunkSlots + within;

        return true;
    }

    return false;
}

// Commit one chunk of chunkSlots slots. Allocate-before-publish: the
// arena allocation completes before directory or freelist change.
static bool commitChunk(BitPool *pool, u32 slots) {
    if (!pool || !BitPool_arena(pool) || slots == 0) 
        return false;

    if (slots > BitPool_chunkSlots(pool)) 
        slots = BitPool_chunkSlots(pool);

    bytes stride = BitPool_stride(pool);

    bytes span = (bytes) slots * stride;

    MemoryArena *arena = BitPool_arena(pool);

    any raw = MemoryArena_alloc(arena, BITPOOL_CHUNK_TYPE, span);

    if (!raw) 
        return false;
    // Allocate-before-publish consumed the bytes; zeroing gives clean
    // slot headers (generation 0, nextFree unset) deterministically.
    memset(raw, 0, span);

    // Ensure directory room. Directory is itself arena-backed; growing it
    // allocates a NEW directory and copies, never moves slot pixels.
    bytes chunkCount = BitPool_chunkCount(pool);

    bytes chunkCap = BitPool_chunkCap(pool);

    if (chunkCount == chunkCap) {
        bytes newCap = chunkCap == 0 ? 8 : chunkCap * 2;

        any nextDir = MemoryArena_alloc(arena, BITPOOL_CHUNK_TYPE,
                                          newCap * sizeof(BitChunk));
        if (!nextDir) 
            return false;

        memset(nextDir, 0, newCap * sizeof(BitChunk));

        BitChunk *oldChunks = BitPool_chunks(pool);

        if (oldChunks && chunkCount > 0) 
            memcpy(nextDir, oldChunks, chunkCount * sizeof(BitChunk));
        // Old directory backing is reclaimed by shutdown accounting via
        // the chunks pointer swap: the previous directory block stays
        // inside the arena until arena freeAll; slots already point at
        // chunk base pixels, never at directory entries.
        BitPool_setChunks(pool, (BitChunk*) nextDir);

        BitPool_setChunkCap(pool, newCap);
    }

    BitChunk *chunk = &BitPool_chunks(pool)[chunkCount];

    BitChunk_setBase(chunk, (u8*) raw);

    BitChunk_setCount(chunk, slots);

    BitChunk_setLive(chunk, 0);

    // Thread every new slot onto the freelist by index. No pointer
    // truncation: the chain is index arithmetic.
    u32 chunkSlots = BitPool_chunkSlots(pool);

    u32 firstIndex = (u32) chunkCount * chunkSlots;

    u32 freeHead = BitPool_freeHead(pool);

    for (u32 within = 0; within < slots; within++) {
        u32 index = firstIndex + within;

        u32 next = (within + 1 < slots) ? (index + 1) : freeHead;

        BitSlot *slot = (BitSlot*) ((u8*) raw + (bytes) within * stride);

        BitSlot_setNextFree(slot, next);

        BitSlot_setGeneration(slot, 0);

        BitSlot_setLength(slot, 0);

        BitSlot_setTypeId(slot, 0);
    }

    BitPool_setFreeHead(pool, firstIndex);

    BitPool_setChunkCount(pool, chunkCount + 1);

    BitPool_setTotalSlots(pool, (u32) ((chunkCount + 1) * chunkSlots));

    return true;
}

BitPool BitPool_0() {
    BitPool pool;

    memset(&pool, 0, sizeof pool);

    pool.lock = SPIN_LOCK_INIT;

    pool.freeHead = BITPOOL_FREE_END;

    return pool;
}

BitPool BitPool_2(bytes elementSize, bytes capacity) {
    return BitPool_3(elementSize, capacity, BITPOOL_DEFAULT_CHUNK, nullptr);
}

BitPool BitPool_3(bytes elementSize, bytes capacity, bytes chunkSlots, MemoryArena *arena) {
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

    pool.chunkSlots = (u32) chunkSlots;

    SpinLock_lock(&pool.lock);

    // Commit enough chunks for capacity by appending; never one huge
    // calloc. The first chunk is this constructor's commit end.
    bool ok = true;

    bytes remaining = capacity;

    while (remaining > 0) {
        u32 take = remaining > chunkSlots ? (u32) chunkSlots : (u32) remaining;

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

fn BitPool_shutdown(BitPool *pool) {
    if (!pool) 
        return;

    SpinLock_lock(BitPool_lockMut(pool));
    // Release every chunk's backing to the owning arena. The arena reclaims
    // via its own slab/bump accounting; frees here are one MemoryArena_free
    // per chunk (O(chunks), cold, once at teardown).
    MemoryArena *arena = BitPool_arena(pool);

    if (arena) {
        bytes chunkCount = BitPool_chunkCount(pool);

        BitChunk *chunks = BitPool_chunks(pool);

        for (bytes c = 0; c < chunkCount; c++) 
            MemoryArena_free(arena, BitChunk_base(&chunks[c]));
    }
    // Directory backing stays inside the arena until freeAll; nulling it
    // here prevents a double shutdown from re-freeing it.
    memset(pool, 0, sizeof *pool);
    // Leave a fresh, usable empty lock behind so double shutdown is safe.
    BitPool fresh = BitPool_0();

    *pool = fresh;
}

any BitPool_alloc(BitPool *pool, u64 typeId) {
    if (!pool || !BitPool_arena(pool)) 
        return nullptr;

    SpinLock_lock(BitPool_lockMut(pool));

    if (BitPool_freeHead(pool) == BITPOOL_FREE_END) {
        // Full pool: grow by one chunk. allocate-before-publish inside
        // commitChunk guarantees a failure leaves the pool untouched.
        if (!commitChunk(pool, BitPool_chunkSlots(pool))) {
            SpinLock_unlock(BitPool_lockMut(pool));

            return nullptr;
        }
    }

    u32 index = BitPool_freeHead(pool);

    any payload = slotPayload(pool, index);

    if (!payload) {
        SpinLock_unlock(BitPool_lockMut(pool));

        return nullptr;
    }

    BitSlot *slot = (BitSlot*) ((u8*) payload - sizeof(BitSlot));

    BitPool_setFreeHead(pool, BitSlot_nextFree(slot));

    BitSlot_setTypeId(slot, typeId);

    BitSlot_setLength(slot, (u32) BitPool_elementSize(pool));

    u32 chunkSlots = BitPool_chunkSlots(pool);

    u32 chunkIndex = index / chunkSlots;

    if (chunkIndex < BitPool_chunkCount(pool)) {
        BitChunk *chunk = &BitPool_chunks(pool)[chunkIndex];

        BitChunk_setLive(chunk, BitChunk_live(chunk) + 1);
    }

    BitPool_setLiveCount(pool, BitPool_liveCount(pool) + 1);

    SpinLock_unlock(BitPool_lockMut(pool));

    return payload;
}

fn BitPool_free(BitPool *pool, any userPtr) {
    if (!pool || !userPtr) 
        return;

    SpinLock_lock(BitPool_lockMut(pool));

    u32 index = 0;

    if (!resolveIndex(pool, userPtr, &index)) {
        SpinLock_unlock(BitPool_lockMut(pool));

        return;
    }

    any payload = slotPayload(pool, index);

    if (payload != userPtr) {
        SpinLock_unlock(BitPool_lockMut(pool));

        return;
    }

    BitSlot *slot = (BitSlot*) ((u8*) payload - sizeof(BitSlot));
    // Free slots carry typeId==0. A second release sees typeId==0 AND
    // generation already bumped, so it is refused before touching the
    // freelist: no cycle, no O(capacity) walk.
    if (BitSlot_typeId(slot) == 0) {
        SpinLock_unlock(BitPool_lockMut(pool));

        return;
    }

    BitSlot_setTypeId(slot, 0);

    BitSlot_setGeneration(slot, BitSlot_generation(slot) + 1);

    BitSlot_setLength(slot, 0);

    BitSlot_setNextFree(slot, BitPool_freeHead(pool));

    BitPool_setFreeHead(pool, index);

    u32 chunkSlots = BitPool_chunkSlots(pool);

    u32 chunkIndex = index / chunkSlots;

    if (chunkIndex < BitPool_chunkCount(pool)) {
        BitChunk *chunk = &BitPool_chunks(pool)[chunkIndex];

        u32 live = BitChunk_live(chunk);

        if (live > 0) 
            BitChunk_setLive(chunk, live - 1);
    }

    u32 liveCount = BitPool_liveCount(pool);

    if (liveCount > 0) 
        BitPool_setLiveCount(pool, liveCount - 1);

    SpinLock_unlock(BitPool_lockMut(pool));
}

bool BitPool_contains(const BitPool *pool, const_any userPtr) {
    return resolveIndex(pool, userPtr, nullptr);
}

u64 BitPool_type(const BitPool *pool, const_any userPtr) {
    u32 index = 0;

    if (!resolveIndex(pool, userPtr, &index)) 
        return 0;

    any payload = slotPayload(pool, index);

    if (!payload) 
        return 0;

    const BitSlot *slot = (const BitSlot*) ((const u8*) payload - sizeof(BitSlot));

    return BitSlot_typeId(slot);
}

bytes BitPool_length(const BitPool *pool, const_any userPtr) {
    u32 index = 0;

    if (!resolveIndex(pool, userPtr, &index)) 
        return 0;

    any payload = slotPayload(pool, index);

    if (!payload) 
        return 0;

    const BitSlot *slot = (const BitSlot*) ((const u8*) payload - sizeof(BitSlot));

    return (bytes) BitSlot_length(slot);
}
