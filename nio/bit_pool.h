#ifndef NIO_BIT_POOL_H
#define NIO_BIT_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util/annotation.h"
#include "atomic/spin.h"
#include "vexspoke/c23/constructor.h"
#include "nio/mem.h"

// nio/bit_pool.h — engine-owned fixed-stride pool API (replaces vexspoke bit/bit).
//
// Relational Engine owns this production ForeignMemory-side API and its
// implementation. It is a layout over engine bytes, never a second heap:
// one typed chunk carves slots from a MemoryArena and hands them out by
// INDEX. Freelist entries are slot indices with a generation dial, never
// truncated pointers; there is no ABA-tagged pointer to mask, no 48-bit
// VA cliff, no calloc/calloc'd arena, and no O(capacity) double-free walk.
// Release is O(1): bump the slot's generation, push the index back.
//
// One pool per element width (4B Int, 8B Long, 2B Brain, ...): slots never
// waste space, and the stride rounds to the ecosystem 32-byte payload floor
// so a pool payload agrees with Memory_alloc on alignment.
//
// Growth: construction takes an initial slot count and a per-chunk growth
// step. A full pool grows by appending one chunk to its directory; payload
// pointers never move (MemoryHeader.pointer and handed-out primitive
// addresses stay valid). Occupancy is sparse demand: only chunks actually
// touched are committed in the arena.
//
// Concurrency is serialized by one SpinLock per pool: the same discipline
// as the engine slab classes. This is not the old lock-free tagged list;
// that mechanism is retired, deliberately, because it could not be made
// generation-safe without pointer surgery.


// A slot's in-arena header. Payload begins at arena + index*stride + sizeof.
// typeId/length are stamped on alloc and cleared on release; generation
// bumps on every release so a stale raw pointer is refused by index check,
// not aliased to the new occupant.
//
// The header is padded to exactly BITPOOL_ALIGN bytes. That is what makes
// the payload of every slot 32-aligned: chunk base is a 32-aligned arena
// payload, stride is a multiple of 32, so base + index*stride + sizeof
// stays on the floor for every index. The 8 pad bytes are the price of
// that guarantee and are cheaper than a per-slot offset table.
typedef struct BitSlot {
    uint64_t typeId;
    uint32_t length;
    uint32_t generation;
    uint32_t nextFree;    // freelist index, BITPOOL_FREE_END terminates
    uint32_t reserved;    // pad
    uint64_t reserved2;   // pad to exactly 32 bytes
} BitSlot;

#define BITPOOL_ALIGN MEMORY_PAYLOAD_ALIGN
_Static_assert(sizeof(BitSlot) == MEMORY_PAYLOAD_ALIGN,
               "BitSlot header must be one 32-byte floor unit");

#define BITPOOL_FREE_END UINT32_MAX
#define BITPOOL_DEFAULT_CHUNK 1024u

// One committed chunk of equal-stride slots. Kept in a directory array
// that grows by appending; chunks themselves never move once committed.
typedef struct BitChunk {
    uint8_t *base;    // 32-aligned arena base (slot array)
    uint32_t count;   // slots this chunk holds
    uint32_t live;    // handed-out count
} BitChunk;

typedef struct BitPool {
    MemoryArena *arena;   // owning arena for chunk backing (borrowed)
    size_t elementSize;   // payload bytes per slot (caller's width)
    size_t stride;        // sizeof(BitSlot) + payload, rounded to 32
    uint32_t chunkSlots;  // slots per new chunk (growth step)
    BitChunk *chunks;     // arena-backed directory of committed chunks
    size_t chunkCount;    // committed chunks
    size_t chunkCap;      // directory capacity (slots for chunk pointers)
    uint32_t freeHead;    // freelist head index, BITPOOL_FREE_END = empty
    uint32_t totalSlots;  // slots across all committed chunks
    uint32_t liveCount;   // currently handed-out slots
    SpinLock lock;        // embedded; zero-init via BitPool_0
    bool live;            // true after successful construction
} BitPool;

// Empty pool: no arena, no backing. BITPOOL_NOT_INIT for g_*Pool globals.
BitPool BitPool_0(void);
static inline BitPool BitPool_empty(void) {
    return BitPool_0();
}

// Carve capacity slots, on the default arena. Returns an empty pool
// (live==false) on invalid geometry (elementSize==0, capacity==0) or
// when the arena cannot back the first chunk.
//
// Growth after construction is automatic: alloc appends a chunk when every
// committed slot is live. The pool never grows by more than one chunk per
// full-pool alloc, and backing allocation is allocate-before-publish.
BitPool BitPool_2(size_t elementSize, size_t capacity);
// Explicit-arena form: same geometry, caller-named arena and chunk step.
BitPool BitPool_3(size_t elementSize, size_t capacity, size_t chunkSlots, MemoryArena *arena);
#define BitPool(...) CONSTRUCTOR_DISPATCH(BitPool, __VA_ARGS__)

// True when the pool was successfully constructed and not shut down.
static inline bool BitPool_live(const BitPool *pool) {
    return pool && (*pool).live && (*pool).arena != nullptr;
}

// Release all chunk backing to the owning arena and reset the pool.
// Idempotent. Does not destroy the arena itself.
void BitPool_shutdown(BitPool *pool);

// Pop a slot and stamp typeId/length into its header. Grows one chunk
// when the pool is full. Returns nullptr on exhaustion of the owning
// arena (loud once per arena epoch) or on a null/uninitialized pool.
void *BitPool_alloc(BitPool *pool, uint64_t typeId);

// Push a slot back: generation bump makes stale pointers stale, index
// push is O(1). Null and foreign pointers are no-ops. Double release is
// refused by the generation check without creating a freelist cycle.
void BitPool_free(BitPool *pool, void *userPtr);

// Mixed-allocator dispatch helpers. These let primitive *_free/_type/
// _length safely route between BitPool slots and Memory_alloc blocks.
bool BitPool_contains(const BitPool *pool, const void *userPtr);
uint64_t BitPool_type(const BitPool *pool, const void *userPtr);
size_t BitPool_length(const BitPool *pool, const void *userPtr);

// Payload base of the slot at global index (chunk-local + directory).
// Returns nullptr when index is out of range.
static inline void *BitPool_slotAt(const BitPool *pool, uint32_t index) {
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

#endif
