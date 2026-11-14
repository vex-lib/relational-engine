#include "vex.h"
#include "nio/mem.h"

#include <stdlib.h>
#include <string.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ForeignMemory
 * ============================================================================
 * The master allocator's public ownership layer. Backing storage is carved by
 * arena.c; transient scratch is owned by transient.c; this file owns the
 * process-wide Memory_* operations and the single libc backing seam. Every
 * payload carries the 32-byte MemoryHeader and is validated by the shared
 * header hash before it is read, freed, or routed.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ForeignMemory (nio/mem)
 * ============================================================================
 * Master allocation API: default-arena allocation, reallocation, free routing,
 * metadata reads, and lifetime classification. The default arena itself and
 * secondary arenas live in arena.c; scratch allocations live in transient.c.
 * ============================================================================
 */

// The only libc allocation calls in the ecosystem: arena.c and transient.c use
// this seam, never malloc/calloc/realloc/free directly.
any Memory_systemAlloc(bytes size) {
    return malloc((size_t) size);
}

any Memory_systemCalloc(bytes count, bytes size) {
    return calloc((size_t) count, (size_t) size);
}

any Memory_systemRealloc(any ptr, bytes size) {
    return realloc(ptr, (size_t) size);
}

fn Memory_systemFree(any ptr) {
    free(ptr);
}

/** Allocate a default-arena payload with a 32-byte self-describing header. */
any Memory_alloc(u64 typeId, bytes numBytes) {
    MemoryArena *arena = Memory_defaultArena();
    if (!arena)
        return nullptr;
    return MemoryArena_alloc(arena, typeId, numBytes);
}

/** Allocate a replacement in the default arena, copy the shorter length, and free the old arena block. */
any Memory_realloc(any userPtr, bytes newBytes) {
    if (!userPtr)
        return Memory_alloc(0, newBytes);
    if (!Memory_safeHeader(userPtr))
        return nullptr;

    u64 typeId = Memory_type(userPtr);
    bytes oldLen = Memory_length(userPtr);
    any next = Memory_alloc(typeId, newBytes);
    if (!next)
        return nullptr;

    memcpy(next, userPtr, oldLen < newBytes ? oldLen : newBytes);
    Memory_free(userPtr);
    return next;
}

/** Route a validated permanent block to its owning arena; transient or invalid pointers are ignored. */
fn Memory_free(any userPtr) {
    if (!userPtr)
        return;
    if (Transient_contains(userPtr))
        return;

    if (!Memory_safeHeader(userPtr))
        return;

    MemoryArena *a = Memory_arenaFor(userPtr);
    if (!a)
        return;
    MemoryArena_free(a, userPtr);
}

/** Return a validated block's payload length, or zero when the pointer is rejected. */
bytes Memory_length(any userPtr) {
    const MemoryHeader *h = Memory_safeHeader(userPtr);
    if (h)
        return (bytes) (*h).length;
    return 0;
}
/** Return a validated block's type id, or zero when the pointer is rejected. */
u64 Memory_type(any userPtr) {
    const MemoryHeader *h = Memory_safeHeader(userPtr);
    if (h)
        return (*h).typeId;
    return 0;
}
/** Return whether both validated blocks carry the same type id. */
bool Memory_similar(const_any a, const_any b) {
    if (!a || !b)
        return false;
    const MemoryHeader *ha = Memory_safeHeader(a);
    if (!ha)
        return false;
    const MemoryHeader *hb = Memory_safeHeader(b);
    if (!hb)
        return false;
    return (*ha).typeId == (*hb).typeId;
}
/** Classify a pointer as transient, permanent, or unknown using registered address ranges. */
MemoryLifetime Memory_getLifetime(const_any ptr) {
    if (!ptr)
        return MEMORY_LIFETIME_UNKNOWN;
    if (Transient_contains(ptr))
        return MEMORY_LIFETIME_TRANSIENT;
    if (Memory_arenaFor((any) ptr) != nullptr)
        return MEMORY_LIFETIME_PERMANENT;
    return MEMORY_LIFETIME_UNKNOWN;
}
