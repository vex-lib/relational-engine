#define NIO_MEM_IMPLEMENTATION
#include "nio/mem.h"

#include "atomic/atomic.h"
#include "debug/throw.h"

#include <string.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: MemoryArena
 * ============================================================================
 * The default master arena and every secondary arena: slab classes for small
 * blocks, a bump region for large blocks, address-range free routing, and
 * enumeration. Each arena owns its backing and reports exhaustion once per
 * epoch. ForeignMemory's public wrappers live in mem.c.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: MemoryArena (nio/arena)
 * ============================================================================
 * Never-moved rows of slab/bump storage addressed by the 32-byte MemoryHeader.
 * The default arena occupies registry slot zero; secondary arenas append.
 * ============================================================================
 */

#define SLAB_COUNT 7
;;INTENTION("fixed SLAB_COUNT: allocator slab classes are a design constant, not a workload ceiling")
#define ANTI_ARENA_DEFAULT_SIZE (64 * 1024 * 1024) // 64 MB master arena

// Triple sugar: BE6C inside the id word, the hash over the stated fields, and
// the redirect the block carries. The hash vetoes the redirect's value without
// constraining what it addresses, so an object and a primitive block are both
// admissible and the id tells a reader which it is.
/** Validate a header against its own stated fields. */
static bool header_valid(const MemoryHeader *h) {
    if (!h)
        return false;
    return (*h).hash == Memory_headerHash((*h).typeId, (*h).length, (*h).pointer);
}

typedef struct FreeNode {
    struct FreeNode *next;
} FreeNode;

typedef struct SlabClass {
    u32 slot_size;
    u32 capacity;
    u32 count;
    u8 *arena;
    FreeNode *free_head;
    SpinLock lock;
} SlabClass;

typedef struct MemoryArena {
    SlabClass slabs[SLAB_COUNT];
    u8 *masterArena;      // 32-aligned base the slabs are carved from
    u8 *masterRaw;        // allocation to release (may sit up to 31B below)
    bytes masterCapacity;     // usable master bytes after the rounding
    u8 *bumpArena;
    bytes bumpCapacity;
    bytes bumpOffset;
    SpinLock bumpLock;
    SpinLock initLock;
    bool live;
    // Exhaustion is LOUD but bounded: the first rejection in an epoch reports
    // through THROW and later ones only count, so a failing frame loop cannot
    // flood stderr. Both reset on init/freeAll (a fresh epoch).
    u64 exhaustionCount;   // rejected requests since the last reset
    bool exhaustionReported;    // the epoch's one diagnostic was emitted
} MemoryArena;

static u32 s_slabSizes[SLAB_COUNT] = { 64, 128, 256, 512, 1024, 2048, 4096 };
static u32 s_slabCaps[SLAB_COUNT] = { 32768, 32768, 16384, 8192, 4096, 2048, 2048 };

static MemoryArena s_default = {0};

// Growable arena registry (the No Hardcoding Law):
// slot 0 is always the default arena once Memory_init runs; created arenas
// append on demand. The pointer table is heap-backed (realloc), like the
// calloc'd MemoryArena shims — never arena-backed, so allocator bookkeeping
// can never recurse into itself. Writes happen at init/create/destroy
// (pre-threads); walks are lock-free reads.
static MemoryArena **s_registry = nullptr;
static bytes s_registryCount = 0;
static bytes s_registryCap = 0;
static SpinLock s_registryLock = SPIN_LOCK_INIT;

// Append an arena pointer, growing the table exponentially. OOM returns false
// and leaves the registry untouched.
/** Append an arena to the global free-routing registry, growing the table on demand. */
static bool registry_push(MemoryArena *a) {
    if (s_registryCount == s_registryCap) {
        bytes newCap = (s_registryCap == 0) ? 8 : s_registryCap * 2;
        MemoryArena **nb = (MemoryArena**) Memory_systemRealloc(
            s_registry, newCap * sizeof(MemoryArena*));
        if (!nb) return false;
        s_registry = nb;
        s_registryCap = newCap;
    }
    s_registry[s_registryCount++] = a;
    return true;
}

// The default arena occupies slot 0 so free-routing walks always see it.
/** Ensure the default arena occupies the registry's first slot. */
static bool registry_ensureDefault() {
    if (s_registryCount == 0) return registry_push(&s_default);
    return true;
}
/** Select the smallest slab class that can hold a header and payload, or -1 for bump storage. */
static int find_slab(bytes payload_bytes) {
    bytes needed = payload_bytes + sizeof(MemoryHeader);
    if (needed <= 64)   return 0;
    if (needed <= 128)  return 1;
    if (needed <= 256)  return 2;
    if (needed <= 512)  return 3;
    if (needed <= 1024) return 4;
    if (needed <= 2048) return 5;
    if (needed <= 4096) return 6;
    return -1;
}
/** Initialize a slab descriptor from its fixed class-size and capacity tables. */
static void slab_template(SlabClass *slab, u32 idx) {
    (*slab).slot_size = s_slabSizes[idx];
    (*slab).capacity = s_slabCaps[idx];
    (*slab).count = 0;
    (*slab).arena = nullptr;
    (*slab).free_head = nullptr;
    (*slab).lock = SPIN_LOCK_INIT;
}
/** Allocate and partition an arena into initialized slab freelists and a bump region. */
static bool arena_init(MemoryArena *a, bytes totalBytes) {
    if (!a)
        return false;
    for (bytes s = 0; s < SLAB_COUNT; s++)
        slab_template(&(*a).slabs[s], (u32) s);
    (*a).initLock = SPIN_LOCK_INIT;
    (*a).bumpLock = SPIN_LOCK_INIT;
    (*a).live = false;

    bytes cap = totalBytes > 0 ? totalBytes : ANTI_ARENA_DEFAULT_SIZE;
    if (cap > SIZE_MAX - (MEMORY_ALIGN_MASK + MEMORY_PAYLOAD_ALIGN))
        return false;
    bytes alignedCap = (cap + MEMORY_ALIGN_MASK) & ~(bytes) MEMORY_ALIGN_MASK;
    u8 *raw = Memory_systemAlloc(alignedCap + MEMORY_PAYLOAD_ALIGN);
    if (!raw)
        return false;
    u8 *master = Memory_alignUp(raw);

    u8 *cur = master;
    bytes left = alignedCap;
    for (bytes s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &(*a).slabs[s];
        bytes need = (bytes)(*slab).capacity * (*slab).slot_size;
        if (need > left) {
            Memory_systemFree(raw);
            return false;
        }
        (*slab).arena = cur;
        (*slab).free_head = nullptr;
        (*slab).count = 0;
        u32 sz = (*slab).slot_size;
        for (bytes i = (*slab).capacity; i > 0; i--) {
            u8 *slot_ptr = (*slab).arena + (i - 1) * sz;
            MemoryHeader *h = (MemoryHeader*) slot_ptr;
            memset(h, 0, sizeof(MemoryHeader));
            FreeNode *node = (FreeNode*) (slot_ptr + sizeof(MemoryHeader));
            (*node).next = (*slab).free_head;
            (*slab).free_head = node;
        }
        cur += need;
        left -= need;
    }

    (*a).masterArena = master;
    (*a).masterRaw = raw;
    (*a).masterCapacity = alignedCap;
    (*a).bumpArena = cur;
    (*a).bumpCapacity = left;
    (*a).bumpOffset = 0;
    (*a).live = true;
    (*a).exhaustionCount = 0;
    (*a).exhaustionReported = false;
    return true;
}
/** Allocate from owner storage only; exhaustion is reported loudly, once per epoch. */
;;INTENTION("exhaustion is a cold, terminal detection reached from a hot allocator: it reports once per epoch (never per call) and counts the rest, so the Cold-Strict hot-minimal contract holds for the success path while a failure can never be silent -- the Exhaustion Loudness Law's hot-path clause under the Conflict Triage Law")
static void *arena_alloc(MemoryArena *a, u64 typeId, bytes numBytes) {
    if (!a || !(*a).live)
        return nullptr;
    if (numBytes > SIZE_MAX - sizeof(MemoryHeader) - MEMORY_ALIGN_MASK) {
        (*a).exhaustionCount++;
        if (!(*a).exhaustionReported) {
            (*a).exhaustionReported = true;
            THROW("memory: request %zu overflows the block size", numBytes);
        }
        return nullptr;
    }

    int s_idx = find_slab(numBytes);
    if (s_idx >= 0) {
        SlabClass *slab = &(*a).slabs[s_idx];
        SpinLock_lock(&(*slab).lock);
        FreeNode *node = (*slab).free_head;
        if (node) {
            (*slab).free_head = (*node).next;
            (*slab).count++;
        }
        SpinLock_unlock(&(*slab).lock);

        if (node) {
            MemoryHeader *h = (MemoryHeader*) ((u8*) node - sizeof(MemoryHeader));
            Memory_headerStamp(h, typeId, numBytes);
            return node;
        }
    }

    bytes aligned_len = (numBytes + MEMORY_ALIGN_MASK) & ~(bytes) MEMORY_ALIGN_MASK;
    bytes total = sizeof(MemoryHeader) + aligned_len;

    SpinLock_lock(&(*a).bumpLock);
    if ((*a).bumpOffset + total <= (*a).bumpCapacity) {
        u8 *slot_ptr = (*a).bumpArena + (*a).bumpOffset;
        (*a).bumpOffset += total;
        SpinLock_unlock(&(*a).bumpLock);

        MemoryHeader *h = (MemoryHeader*) slot_ptr;
        Memory_headerStamp(h, typeId, numBytes);
        return (void*) (slot_ptr + sizeof(MemoryHeader));
    }
    SpinLock_unlock(&(*a).bumpLock);

    (*a).exhaustionCount++;
    if (!(*a).exhaustionReported) {
        (*a).exhaustionReported = true;
        THROW("memory: arena exhausted, request %zu bytes (bump used %zu/%zu)",
              numBytes, (*a).bumpOffset, (*a).bumpCapacity);
    }
    return nullptr;
}
/** Validate an in-arena payload header and recycle its slab slot when applicable. */
static void arena_free(MemoryArena *a, void *userPtr) {
    if (!a || !userPtr)
        return;

    pointer u = (pointer) userPtr;
    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0)
        return;

    u8 *p = (u8*) userPtr;
    bool in_range = false;
    if ((*a).masterArena && p >= (*a).masterArena + sizeof(MemoryHeader) && p < (*a).masterArena + (*a).masterCapacity)
        in_range = true;
    if (!in_range) {
        return;
    }

    MemoryHeader *h = (MemoryHeader*) ((u8*) userPtr - sizeof(MemoryHeader));
    if (!header_valid(h))
        return;

    SlabClass *slab = nullptr;
    for (bytes s = 0; s < SLAB_COUNT; s++) {
        SlabClass *candidate = &(*a).slabs[s];
        pointer base = (pointer) (*candidate).arena;
        bytes extent = (bytes) (*candidate).capacity * (*candidate).slot_size;
        if (u >= base + sizeof(MemoryHeader) && u < base + extent) {
            if ((u - base) % (*candidate).slot_size != sizeof(MemoryHeader)
                || (*h).length > (*candidate).slot_size - sizeof(MemoryHeader))
                return;
            slab = candidate;
            break;
        }
    }
    memset(h, 0, sizeof(MemoryHeader));
    if (!slab)
        return;
    FreeNode *node = (FreeNode*) userPtr;

    SpinLock_lock(&(*slab).lock);
    (*node).next = (*slab).free_head;
    (*slab).free_head = node;
    if ((*slab).count > 0)
        (*slab).count--;
    SpinLock_unlock(&(*slab).lock);
}
/** Reset slab freelists and the bump cursor, invalidating all arena payloads. */
static void arena_freeAll(MemoryArena *a) {
    if (!a || !(*a).live)
        return;

    for (bytes s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &(*a).slabs[s];
        SpinLock_lock(&(*slab).lock);
        (*slab).free_head = nullptr;
        (*slab).count = 0;

        u32 sz = (*slab).slot_size;
        for (bytes i = (*slab).capacity; i > 0; i--) {
            u8 *slot_ptr = (*slab).arena + (i - 1) * sz;
            MemoryHeader *h = (MemoryHeader*) slot_ptr;
            memset(h, 0, sizeof(MemoryHeader));
            FreeNode *node = (FreeNode*) (slot_ptr + sizeof(MemoryHeader));
            (*node).next = (*slab).free_head;
            (*slab).free_head = node;
        }
        SpinLock_unlock(&(*slab).lock);
    }

    SpinLock_lock(&(*a).bumpLock);
    memset((*a).bumpArena, 0, (*a).bumpOffset);
    (*a).bumpOffset = 0;
    SpinLock_unlock(&(*a).bumpLock);
    (*a).exhaustionCount = 0;
    (*a).exhaustionReported = false;
}
// Free-routing: headers carry no arena tag (ABI-stable by design), so the
// owner is whoever's master range contains the header. Registry writes happen at
// create/destroy (pre-threads); reads are lock-free.
/** Find the live registered arena whose address range contains a valid payload. */
MemoryArena *Memory_arenaFor(void *userPtr) {
    if (!userPtr)
        return nullptr;
    pointer u = (pointer) userPtr;
    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0)
        return nullptr;

    u8 *p = (u8*) userPtr;

    for (bytes i = 0; i < s_registryCount; i++) {
        MemoryArena *a = s_registry[i];
        if (a && (*a).live && (*a).masterArena) {
            u8 *base = (*a).masterArena;
            if (p >= base + sizeof(MemoryHeader) && p < base + (*a).masterCapacity) {
                MemoryHeader *h = (MemoryHeader*) (p - sizeof(MemoryHeader));
                if (header_valid(h))
                    return a;
            }
        }
    }
    return nullptr;
}

/** Resolve a validated header only after range-checking transient and registered arenas. */
const MemoryHeader *Memory_safeHeader(const void *userPtr) {
    if (!userPtr)
        return nullptr;
    pointer u = (pointer) userPtr;
    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0)
        return nullptr;

    u8 *p = (u8*) userPtr;

    if (Transient_contains(userPtr)) {
        const MemoryHeader *h = (const MemoryHeader*) (p - sizeof(MemoryHeader));
        if (header_valid(h))
            return h;
    }

    for (bytes i = 0; i < s_registryCount; i++) {
        MemoryArena *a = s_registry[i];
        if (a && (*a).live && (*a).masterArena) {
            u8 *base = (*a).masterArena;
            if (p >= base + sizeof(MemoryHeader) && p < base + (*a).masterCapacity) {
                const MemoryHeader *h = (const MemoryHeader*) (p - sizeof(MemoryHeader));
                if (header_valid(h))
                    return h;
            }
        }
    }
    return nullptr;
}

/** Lazily initialize the default arena with its named default size. */
static void ensure_initialized() {
    if (!s_default.live)
        Memory_init(ANTI_ARENA_DEFAULT_SIZE);
}
/** Initialize and register the default arena; repeated calls succeed without reinitializing it. */
bool Memory_init(bytes totalBytes) {
    SpinLock_lock(&s_default.initLock);
    if (s_default.live) {
        SpinLock_unlock(&s_default.initLock);
        return true;
    }
    SpinLock_unlock(&s_default.initLock);
    if (!registry_ensureDefault()) return false;
    return arena_init(&s_default, totalBytes);
}
/** Return the process-wide default arena, initializing it on first access. */
MemoryArena *Memory_defaultArena() {
    ensure_initialized();
    return &s_default;
}
/** Reset all allocations in the default arena; dependent users must already be stopped. */
void Memory_freeAll() {
    arena_freeAll(&s_default);
}
/** Enumerate default-arena live slab blocks matching typeId, returning total matches found. */
bytes Memory_findAll(u64 typeId, void **outArray, bytes maxCount) {
    return MemoryArena_findAll(&s_default, typeId, outArray, maxCount);
}
/** Allocate, initialize, and register an independent arena with the requested capacity. */
MemoryArena *MemoryArena_create(bytes totalBytes) {
    MemoryArena *a = (MemoryArena*) Memory_systemCalloc(1, sizeof(MemoryArena));
    if (!a)
        return nullptr;
    if (!arena_init(a, totalBytes)) {
        Memory_systemFree(a);
        return nullptr;
    }
    SpinLock_lock(&s_registryLock);
    bool placed = registry_push(a);
    SpinLock_unlock(&s_registryLock);
    if (!placed) {
        Memory_systemFree((*a).masterRaw);
        Memory_systemFree(a);
        return nullptr;
    }
    return a;
}
/** Unregister and destroy a non-default arena and its backing storage. */
void MemoryArena_destroy(MemoryArena *a) {
    if (!a || a == &s_default)
        return;
    SpinLock_lock(&s_registryLock);
    for (bytes i = 0; i < s_registryCount; i++) {
        if (s_registry[i] == a) {
            s_registry[i] = s_registry[--s_registryCount];
            break;
        }
    }
    SpinLock_unlock(&s_registryLock);
    (*a).live = false;
    Memory_systemFree((*a).masterRaw);
    (*a).masterRaw = nullptr;
    (*a).masterArena = nullptr;
    (*a).bumpArena = nullptr;
    (*a).masterCapacity = 0;
    Memory_systemFree(a);
}
/** Allocate a typed payload from the specified arena. */
void *MemoryArena_alloc(MemoryArena *a, u64 typeId, bytes numBytes) {
    if (!a)
        return nullptr;
    return arena_alloc(a, typeId, numBytes);
}
/** Allocate replacement storage in a, copy bytes from userPtr, and route old-block release by owner. */
void *MemoryArena_realloc(MemoryArena *a, void *userPtr, bytes newBytes) {
    if (!a)
        return nullptr;
    if (!userPtr)
        return arena_alloc(a, 0, newBytes);
    if (!Memory_safeHeader(userPtr))
        return nullptr;

    u64 typeId = Memory_type(userPtr);
    bytes oldLen = Memory_length(userPtr);
    void *next = arena_alloc(a, typeId, newBytes);
    if (!next)
        return nullptr;

    memcpy(next, userPtr, oldLen < newBytes ? oldLen : newBytes);
    Memory_free(userPtr);
    return next;
}
/** Attempt to recycle a validated payload within the specified arena. */
void MemoryArena_free(MemoryArena *a, void *userPtr) {
    if (!a)
        return;
    arena_free(a, userPtr);
}
/** Reset every slab and bump allocation in the specified arena. */
void MemoryArena_freeAll(MemoryArena *a) {
    if (!a)
        return;
    arena_freeAll(a);
}
/** Enumerate live slab and bump blocks; total matches may exceed output capacity. */
bytes MemoryArena_findAll(MemoryArena *a, u64 typeId, void **outArray, bytes maxCount) {
    bytes count = 0;
    if (!a || !(*a).live)
        return 0;

    for (bytes s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &(*a).slabs[s];
        SpinLock_lock(&(*slab).lock);
        u32 sz = (*slab).slot_size;
        for (bytes i = 0; i < (*slab).capacity; i++) {
            u8 *slot_ptr = (*slab).arena + i * sz;
            MemoryHeader *h = (MemoryHeader*) slot_ptr;
            if (header_valid(h)) {
                if (typeId == 0 || (*h).typeId == typeId) {
                    if (outArray && count < maxCount) {
                        outArray[count] = (void*) (slot_ptr + sizeof(MemoryHeader));
                    }
                    count++;
                }
            }
        }
        SpinLock_unlock(&(*slab).lock);
    }
    SpinLock_lock(&(*a).bumpLock);
    bytes offset = 0;
    while (offset < (*a).bumpOffset) {
        bytes remaining = (*a).bumpOffset - offset;
        MemoryHeader *h = (MemoryHeader*) ((*a).bumpArena + offset);
        if (remaining < sizeof(MemoryHeader))
            break;
        bytes length = (*h).length;
        bytes total = sizeof(MemoryHeader) + ((length + MEMORY_ALIGN_MASK) & ~(u64) MEMORY_ALIGN_MASK);
        if (total > remaining)
            break;
        if (header_valid(h) && (typeId == 0 || (*h).typeId == typeId)) {
            if (outArray && count < maxCount)
                outArray[count] = (u8*) h + sizeof(MemoryHeader);
            count++;
        }
        offset += total;
    }
    SpinLock_unlock(&(*a).bumpLock);
    return count;
}
/** Return occupied slab bytes plus the arena's current bump offset. */
bytes MemoryArena_activeBytes(MemoryArena *a) {
    bytes total = 0;
    if (!a || !(*a).live)
        return 0;
    for (bytes s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &(*a).slabs[s];
        SpinLock_lock(&(*slab).lock);
        total += (bytes)(*slab).count * (*slab).slot_size;
        SpinLock_unlock(&(*slab).lock);
    }
    SpinLock_lock(&(*a).bumpLock);
    total += (*a).bumpOffset;
    SpinLock_unlock(&(*a).bumpLock);
    return total;
}
/** Return the backing master-arena capacity, or zero for a null arena. */
bytes MemoryArena_capacity(MemoryArena *a) {
    if (!a)
        return 0;
    return (*a).masterCapacity;
}
/** Return the number of rejected requests since the arena's last init/freeAll. */
u64 MemoryArena_exhaustionCount(const MemoryArena *a) {
    return a ? (*a).exhaustionCount : 0;
}
/** Return the default arena's rejected-request count since the last freeAll. */
u64 Memory_exhaustionCount() {
    return s_default.live ? s_default.exhaustionCount : 0;
}
