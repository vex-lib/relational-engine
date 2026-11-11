#define NIO_MEM_IMPLEMENTATION
#include "nio/mem.h"

#include "atomic/atomic.h"
#include "debug/throw.h"

#include <string.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: TransientArena
 * ============================================================================
 * Frame-scoped scratch storage. A single bump arena is rewound by
 * Transient_reset, which advances the generation and invalidates every prior
 * borrow. Exhaustion is loud once per epoch and counted thereafter.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: TransientArena (nio/transient)
 * ============================================================================
 * Bump-allocated scratch payloads with a generation stamp. Permanent arenas
 * live in arena.c; this owner only serves transient_* calls.
 * ============================================================================
 */

#define ANTI_TRANSIENT_DEFAULT_SIZE (64 * 1024 * 1024) // 64 MB

typedef struct TransientArena {
    u8 *buffer;    // 32-aligned scratch base
    u8 *raw;       // allocation to release (buffer is rounded forward)
    bytes capacity;
    bytes bumpOffset;
    u32 generation;
    SpinLock lock;
    bool live;
    // Same bounded loudness as the permanent arenas: report the first scratch
    // rejection of an epoch, count the rest.
    u64 exhaustionCount;
    bool exhaustionReported;
} TransientArena;

static TransientArena s_transient = {
    .buffer = nullptr,
    .capacity = 0,
    .bumpOffset = 0,
    .generation = 1,
    .lock = { 0 },
    .live = false,
};
/** Initialize the transient bump arena once, rounding capacity up to the payload floor. */
bool Memory_initTransient(bytes capacity) {
    if (capacity > SIZE_MAX - (MEMORY_ALIGN_MASK + MEMORY_PAYLOAD_ALIGN))
        return false;
    SpinLock_lock(&s_transient.lock);
    if (s_transient.live) {
        SpinLock_unlock(&s_transient.lock);
        return true;
    }
    if (capacity == 0)
        capacity = ANTI_TRANSIENT_DEFAULT_SIZE;

    bytes aligned_cap = (capacity + MEMORY_ALIGN_MASK) & ~(bytes) MEMORY_ALIGN_MASK;
    u8 *raw = (u8*) Memory_systemAlloc(aligned_cap + MEMORY_PAYLOAD_ALIGN);
    if (!raw) {
        SpinLock_unlock(&s_transient.lock);
        return false;
    }

    s_transient.raw = raw;
    s_transient.buffer = Memory_alignUp(raw);
    s_transient.capacity = aligned_cap;
    s_transient.bumpOffset = 0;
    s_transient.generation = 1;
    s_transient.live = true;
    s_transient.exhaustionCount = 0;
    s_transient.exhaustionReported = false;
    SpinLock_unlock(&s_transient.lock);
    return true;
}
/** Lazily initialize the transient arena using its named default capacity. */
static void ensure_transient_initialized() {
    if (!s_transient.live)
        Memory_initTransient(ANTI_TRANSIENT_DEFAULT_SIZE);
}
/** Allocate an aligned transient payload, rejecting length, arithmetic, and capacity overflow. */
void *Transient_alloc(u64 typeId, bytes numBytes) {
    if (numBytes > SIZE_MAX - sizeof(MemoryHeader) - MEMORY_ALIGN_MASK) {
        s_transient.exhaustionCount++;
        if (!s_transient.exhaustionReported) {
            s_transient.exhaustionReported = true;
            THROW("transient: request %zu overflows the scratch block size", numBytes);
        }
        return nullptr;
    }
    ensure_transient_initialized();
    if (!s_transient.live)
        return nullptr;

    bytes aligned_len = (numBytes + MEMORY_ALIGN_MASK) & ~(bytes) MEMORY_ALIGN_MASK;
    bytes total = sizeof(MemoryHeader) + aligned_len;

    SpinLock_lock(&s_transient.lock);
    if (total > s_transient.capacity - s_transient.bumpOffset) {
        bytes used = s_transient.bumpOffset;
        bytes capacity = s_transient.capacity;
        SpinLock_unlock(&s_transient.lock);
        s_transient.exhaustionCount++;
        if (!s_transient.exhaustionReported) {
            s_transient.exhaustionReported = true;
            THROW("transient: scratch exhausted, request %zu bytes (used %zu/%zu)",
                  numBytes, used, capacity);
        }
        return nullptr;
    }

    u8 *slot = s_transient.buffer + s_transient.bumpOffset;
    s_transient.bumpOffset += total;
    SpinLock_unlock(&s_transient.lock);

    MemoryHeader *h = (MemoryHeader*) slot;
    Memory_headerStamp(h, typeId, numBytes);

    return (void*) (slot + sizeof(MemoryHeader));
}
/** Rewind the transient bump cursor and advance its generation, invalidating prior borrows. */
void Transient_reset() {
    if (!s_transient.live)
        return;

    SpinLock_lock(&s_transient.lock);
#if defined(DEBUG_BORROW_CHECK)
    bytes used = s_transient.bumpOffset;
#endif
    s_transient.bumpOffset = 0;
    s_transient.generation++;
    s_transient.exhaustionCount = 0;
    s_transient.exhaustionReported = false;
    SpinLock_unlock(&s_transient.lock);

#if defined(DEBUG_BORROW_CHECK)
    if (used > 0 && s_transient.buffer) {
        memset(s_transient.buffer, 0xDD, used);
    }
#endif
}
/** Return whether ptr lies in the currently used payload range of the transient arena. */
bool Transient_contains(const void *ptr) {
    if (!ptr || !s_transient.live || !s_transient.buffer)
        return false;
    u8 *p = (u8*) ptr;
    return (p >= s_transient.buffer + sizeof(MemoryHeader) && p < s_transient.buffer + s_transient.bumpOffset);
}
/** Return the current transient allocation generation. */
u32 Transient_getGeneration() {
    return s_transient.generation;
}
/** Return the number of rejected scratch requests since the last reset. */
u64 Transient_exhaustionCount() {
    return s_transient.exhaustionCount;
}
#if defined(DEBUG_BORROW_CHECK)
;;VALIDATOR
/** Return the transient arena backing buffer for debug-only borrow diagnostics. */
const u8 *Transient_getBuffer() {
    return s_transient.buffer;
}
#endif
