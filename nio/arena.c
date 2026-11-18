#define NIO_MEM_IMPLEMENTATION

#include "vex.h"
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

;;GETTER
static u64 MemoryHeader_hash(const MemoryHeader *h) {
    return (*h).hash;
}

;;GETTER
static u64 MemoryHeader_typeId(const MemoryHeader *h) {
    return (*h).typeId;
}

;;GETTER
static bytes MemoryHeader_length(const MemoryHeader *h) {
    return (*h).length;
}

;;GETTER
static u64 MemoryHeader_pointer(const MemoryHeader *h) {
    return (*h).pointer;
}

;;GETTER
static FreeNode *FreeNode_next(const FreeNode *node) {
    return (*node).next;
}

;;SETTER
static fn FreeNode_setNext(FreeNode *node, FreeNode *next) {
    (*node).next = next;
}

;;GETTER
static u32 SlabClass_slotSize(const SlabClass *slab) {
    return (*slab).slot_size;
}

;;GETTER
static u32 SlabClass_capacity(const SlabClass *slab) {
    return (*slab).capacity;
}

;;GETTER
static u32 SlabClass_count(const SlabClass *slab) {
    return (*slab).count;
}

;;GETTER
static u8 *SlabClass_arena(const SlabClass *slab) {
    return (*slab).arena;
}

;;GETTER
static FreeNode *SlabClass_freeHead(const SlabClass *slab) {
    return (*slab).free_head;
}

;;GETTER
static SpinLock *SlabClass_lockMut(SlabClass *slab) {
    return &(*slab).lock;
}

;;SETTER
static fn SlabClass_setSlotSize(SlabClass *slab, u32 slotSize) {
    (*slab).slot_size = slotSize;
}

;;SETTER
static fn SlabClass_setCapacity(SlabClass *slab, u32 capacity) {
    (*slab).capacity = capacity;
}

;;SETTER
static fn SlabClass_setCount(SlabClass *slab, u32 count) {
    (*slab).count = count;
}

;;SETTER
static fn SlabClass_setArena(SlabClass *slab, u8 *arena) {
    (*slab).arena = arena;
}

;;SETTER
static fn SlabClass_setFreeHead(SlabClass *slab, FreeNode *freeHead) {
    (*slab).free_head = freeHead;
}

;;SETTER
static fn SlabClass_setLock(SlabClass *slab, SpinLock lock) {
    (*slab).lock = lock;
}

;;GETTER
static SlabClass *MemoryArena_slabs(MemoryArena *a) {
    return (*a).slabs;
}

;;GETTER
static u8 *MemoryArena_masterArena(const MemoryArena *a) {
    return (*a).masterArena;
}

;;GETTER
static u8 *MemoryArena_masterRaw(const MemoryArena *a) {
    return (*a).masterRaw;
}

;;GETTER
static bytes MemoryArena_masterCapacity(const MemoryArena *a) {
    return (*a).masterCapacity;
}

;;GETTER
static u8 *MemoryArena_bumpArena(const MemoryArena *a) {
    return (*a).bumpArena;
}

;;GETTER
static bytes MemoryArena_bumpCapacity(const MemoryArena *a) {
    return (*a).bumpCapacity;
}

;;GETTER
static bytes MemoryArena_bumpOffset(const MemoryArena *a) {
    return (*a).bumpOffset;
}

;;GETTER
static SpinLock *MemoryArena_bumpLockMut(MemoryArena *a) {
    return &(*a).bumpLock;
}

;;GETTER
static bool MemoryArena_live(const MemoryArena *a) {
    return (*a).live;
}

;;GETTER
static u64 MemoryArena_exhaustionCountRaw(const MemoryArena *a) {
    return (*a).exhaustionCount;
}

;;GETTER
static bool MemoryArena_exhaustionReported(const MemoryArena *a) {
    return (*a).exhaustionReported;
}

;;SETTER
static fn MemoryArena_setMasterArena(MemoryArena *a, u8 *masterArena) {
    (*a).masterArena = masterArena;
}

;;SETTER
static fn MemoryArena_setMasterRaw(MemoryArena *a, u8 *masterRaw) {
    (*a).masterRaw = masterRaw;
}

;;SETTER
static fn MemoryArena_setMasterCapacity(MemoryArena *a, bytes masterCapacity) {
    (*a).masterCapacity = masterCapacity;
}

;;SETTER
static fn MemoryArena_setBumpArena(MemoryArena *a, u8 *bumpArena) {
    (*a).bumpArena = bumpArena;
}

;;SETTER
static fn MemoryArena_setBumpCapacity(MemoryArena *a, bytes bumpCapacity) {
    (*a).bumpCapacity = bumpCapacity;
}

;;SETTER
static fn MemoryArena_setBumpOffset(MemoryArena *a, bytes bumpOffset) {
    (*a).bumpOffset = bumpOffset;
}

;;SETTER
static fn MemoryArena_setBumpLock(MemoryArena *a, SpinLock bumpLock) {
    (*a).bumpLock = bumpLock;
}

;;SETTER
static fn MemoryArena_setInitLock(MemoryArena *a, SpinLock initLock) {
    (*a).initLock = initLock;
}

;;SETTER
static fn MemoryArena_setLive(MemoryArena *a, bool live) {
    (*a).live = live;
}

;;SETTER
static fn MemoryArena_setExhaustionCount(MemoryArena *a, u64 exhaustionCount) {
    (*a).exhaustionCount = exhaustionCount;
}

;;SETTER
static fn MemoryArena_setExhaustionReported(MemoryArena *a, bool exhaustionReported) {
    (*a).exhaustionReported = exhaustionReported;
}

/** Validate a header against its own stated fields. */
static bool header_valid(const MemoryHeader *h) {
    if (!h) 
        return false;

    return MemoryHeader_hash(h) == Memory_headerHash(MemoryHeader_typeId(h), MemoryHeader_length(h), MemoryHeader_pointer(h));
}

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
        if (!nb) 
           return false;

        s_registry = nb;

        s_registryCap = newCap;
    }

    s_registry[s_registryCount++] = a;

    return true;
}

// The default arena occupies slot 0 so free-routing walks always see it.
/** Ensure the default arena occupies the registry's first slot. */
static bool registry_ensureDefault() {
    if (s_registryCount == 0) 
       return registry_push(&s_default);

    return true;
}
/** Select the smallest slab class that can hold a header and payload, or -1 for bump storage. */
static i32 find_slab(bytes payload_bytes) {
    bytes needed = payload_bytes + sizeof(MemoryHeader);

    if (needed <= 64) 
       return 0;

    if (needed <= 128) 
       return 1;

    if (needed <= 256) 
       return 2;

    if (needed <= 512) 
       return 3;

    if (needed <= 1024) 
       return 4;

    if (needed <= 2048) 
       return 5;

    if (needed <= 4096) 
       return 6;

    return -1;
}
/** Initialize a slab descriptor from its fixed class-size and capacity tables. */
static fn slab_template(SlabClass *slab, u32 idx) {
    SlabClass_setSlotSize(slab, s_slabSizes[idx]);

    SlabClass_setCapacity(slab, s_slabCaps[idx]);

    SlabClass_setCount(slab, 0);

    SlabClass_setArena(slab, nullptr);

    SlabClass_setFreeHead(slab, nullptr);

    SlabClass_setLock(slab, SPIN_LOCK_INIT);
}
/** Allocate and partition an arena into initialized slab freelists and a bump region. */
static bool arena_init(MemoryArena *a, bytes totalBytes) {
    if (!a) 
        return false;

    SlabClass *slabs = MemoryArena_slabs(a);

    for (bytes s = 0; s < SLAB_COUNT; s++) 
        slab_template(&slabs[s], (u32) s);

    MemoryArena_setInitLock(a, SPIN_LOCK_INIT);

    MemoryArena_setBumpLock(a, SPIN_LOCK_INIT);

    MemoryArena_setLive(a, false);

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
        SlabClass *slab = &slabs[s];

        u32 slabCap = SlabClass_capacity(slab);

        u32 sz = SlabClass_slotSize(slab);

        bytes need = (bytes)slabCap * sz;

        if (need > left) {
            Memory_systemFree(raw);

            return false;
        }

        SlabClass_setArena(slab, cur);

        SlabClass_setFreeHead(slab, nullptr);

        SlabClass_setCount(slab, 0);

        for (bytes i = slabCap; i > 0; i--) {
            u8 *slot_ptr = cur + (i - 1) * sz;

            MemoryHeader *h = (MemoryHeader*) slot_ptr;

            memset(h, 0, sizeof(MemoryHeader));

            FreeNode *node = (FreeNode*) (slot_ptr + sizeof(MemoryHeader));

            FreeNode_setNext(node, SlabClass_freeHead(slab));

            SlabClass_setFreeHead(slab, node);
        }

        cur += need;

        left -= need;
    }

    MemoryArena_setMasterArena(a, master);

    MemoryArena_setMasterRaw(a, raw);

    MemoryArena_setMasterCapacity(a, alignedCap);

    MemoryArena_setBumpArena(a, cur);

    MemoryArena_setBumpCapacity(a, left);

    MemoryArena_setBumpOffset(a, 0);

    MemoryArena_setLive(a, true);

    MemoryArena_setExhaustionCount(a, 0);

    MemoryArena_setExhaustionReported(a, false);

    return true;
}
/** Allocate from owner storage only; exhaustion is reported loudly, once per epoch. */
;;INTENTION("exhaustion is a cold, terminal detection reached from a hot allocator: it reports once per epoch (never per call) and counts the rest, so the Cold-Strict hot-minimal contract holds for the success path while a failure can never be silent -- the Exhaustion Loudness Law's hot-path clause under the Conflict Triage Law")
static any arena_alloc(MemoryArena *a, u64 typeId, bytes numBytes) {
    if (!a || !MemoryArena_live(a)) 
        return nullptr;

    if (numBytes > SIZE_MAX - sizeof(MemoryHeader) - MEMORY_ALIGN_MASK) {
        MemoryArena_setExhaustionCount(a, MemoryArena_exhaustionCountRaw(a) + 1);

        if (!MemoryArena_exhaustionReported(a)) {
            MemoryArena_setExhaustionReported(a, true);

            THROW("memory: request %zu overflows the block size", numBytes);
        }

        return nullptr;
    }

    i32 s_idx = find_slab(numBytes);

    if (s_idx >= 0) {
        SlabClass *slab = &MemoryArena_slabs(a)[s_idx];

        SpinLock_lock(SlabClass_lockMut(slab));

        FreeNode *node = SlabClass_freeHead(slab);

        if (node) {
            SlabClass_setFreeHead(slab, FreeNode_next(node));

            SlabClass_setCount(slab, SlabClass_count(slab) + 1);
        }

        SpinLock_unlock(SlabClass_lockMut(slab));

        if (node) {
            MemoryHeader *h = (MemoryHeader*) ((u8*) node - sizeof(MemoryHeader));

            Memory_headerStamp(h, typeId, numBytes);

            return node;
        }
    }

    bytes aligned_len = (numBytes + MEMORY_ALIGN_MASK) & ~(bytes) MEMORY_ALIGN_MASK;

    bytes total = sizeof(MemoryHeader) + aligned_len;

    SpinLock_lock(MemoryArena_bumpLockMut(a));

    bytes bumpOffset = MemoryArena_bumpOffset(a);

    bytes bumpCap = MemoryArena_bumpCapacity(a);

    if (bumpOffset + total <= bumpCap) {
        u8 *slot_ptr = MemoryArena_bumpArena(a) + bumpOffset;

        MemoryArena_setBumpOffset(a, bumpOffset + total);

        SpinLock_unlock(MemoryArena_bumpLockMut(a));

        MemoryHeader *h = (MemoryHeader*) slot_ptr;

        Memory_headerStamp(h, typeId, numBytes);

        return (any) (slot_ptr + sizeof(MemoryHeader));
    }

    SpinLock_unlock(MemoryArena_bumpLockMut(a));

    MemoryArena_setExhaustionCount(a, MemoryArena_exhaustionCountRaw(a) + 1);

    if (!MemoryArena_exhaustionReported(a)) {
        MemoryArena_setExhaustionReported(a, true);

        THROW("memory: arena exhausted, request %zu bytes (bump used %zu/%zu)",
              numBytes, MemoryArena_bumpOffset(a), MemoryArena_bumpCapacity(a));
    }

    return nullptr;
}
/** Validate an in-arena payload header and recycle its slab slot when applicable. */
static fn arena_free(MemoryArena *a, any userPtr) {
    if (!a || !userPtr) 
        return;

    pointer u = (pointer) userPtr;

    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0) 
        return;

    u8 *p = (u8*) userPtr;

    bool in_range = false;

    u8 *masterArena = MemoryArena_masterArena(a);

    if (masterArena && p >= masterArena + sizeof(MemoryHeader) && p < masterArena + MemoryArena_masterCapacity(a)) 
        in_range = true;

    if (!in_range) 
        return;

    MemoryHeader *h = (MemoryHeader*) ((u8*) userPtr - sizeof(MemoryHeader));

    if (!header_valid(h)) 
        return;

    SlabClass *slab = nullptr;

    SlabClass *slabs = MemoryArena_slabs(a);

    for (bytes s = 0; s < SLAB_COUNT; s++) {
        SlabClass *candidate = &slabs[s];

        pointer base = (pointer) SlabClass_arena(candidate);

        u32 slotSize = SlabClass_slotSize(candidate);

        bytes extent = (bytes) SlabClass_capacity(candidate) * slotSize;

        if (u >= base + sizeof(MemoryHeader) && u < base + extent) {
            if ((u - base) % slotSize != sizeof(MemoryHeader)
                || MemoryHeader_length(h) > slotSize - sizeof(MemoryHeader))
                return;
            slab = candidate;

            break;
        }
    }

    memset(h, 0, sizeof(MemoryHeader));

    if (!slab) 
        return;

    FreeNode *node = (FreeNode*) userPtr;

    SpinLock_lock(SlabClass_lockMut(slab));

    FreeNode_setNext(node, SlabClass_freeHead(slab));

    SlabClass_setFreeHead(slab, node);

    u32 count = SlabClass_count(slab);

    if (count > 0) 
        SlabClass_setCount(slab, count - 1);

    SpinLock_unlock(SlabClass_lockMut(slab));
}
/** Reset slab freelists and the bump cursor, invalidating all arena payloads. */
static fn arena_freeAll(MemoryArena *a) {
    if (!a || !MemoryArena_live(a)) 
        return;

    SlabClass *slabs = MemoryArena_slabs(a);

    for (bytes s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &slabs[s];

        SpinLock_lock(SlabClass_lockMut(slab));

        SlabClass_setFreeHead(slab, nullptr);

        SlabClass_setCount(slab, 0);

        u32 sz = SlabClass_slotSize(slab);

        u8 *slabArena = SlabClass_arena(slab);

        for (bytes i = SlabClass_capacity(slab); i > 0; i--) {
            u8 *slot_ptr = slabArena + (i - 1) * sz;

            MemoryHeader *h = (MemoryHeader*) slot_ptr;

            memset(h, 0, sizeof(MemoryHeader));

            FreeNode *node = (FreeNode*) (slot_ptr + sizeof(MemoryHeader));

            FreeNode_setNext(node, SlabClass_freeHead(slab));

            SlabClass_setFreeHead(slab, node);
        }

        SpinLock_unlock(SlabClass_lockMut(slab));
    }

    SpinLock_lock(MemoryArena_bumpLockMut(a));

    memset(MemoryArena_bumpArena(a), 0, MemoryArena_bumpOffset(a));

    MemoryArena_setBumpOffset(a, 0);

    SpinLock_unlock(MemoryArena_bumpLockMut(a));

    MemoryArena_setExhaustionCount(a, 0);

    MemoryArena_setExhaustionReported(a, false);
}
// Free-routing: headers carry no arena tag (ABI-stable by design), so the
// owner is whoever's master range contains the header. Registry writes happen at
// create/destroy (pre-threads); reads are lock-free.
/** Find the live registered arena whose address range contains a valid payload. */
MemoryArena *Memory_arenaFor(any userPtr) {
    if (!userPtr) 
        return nullptr;

    pointer u = (pointer) userPtr;

    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0) 
        return nullptr;

    u8 *p = (u8*) userPtr;

    for (bytes i = 0; i < s_registryCount; i++) {
        MemoryArena *a = s_registry[i];

        if (a && MemoryArena_live(a) && MemoryArena_masterArena(a)) {
            u8 *base = MemoryArena_masterArena(a);

            if (p >= base + sizeof(MemoryHeader) && p < base + MemoryArena_masterCapacity(a)) {
                MemoryHeader *h = (MemoryHeader*) (p - sizeof(MemoryHeader));

                if (header_valid(h)) 
                    return a;
            }
        }
    }

    return nullptr;
}

/** Resolve a validated header only after range-checking transient and registered arenas. */
const MemoryHeader *Memory_safeHeader(const_any userPtr) {
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

        if (a && MemoryArena_live(a) && MemoryArena_masterArena(a)) {
            u8 *base = MemoryArena_masterArena(a);

            if (p >= base + sizeof(MemoryHeader) && p < base + MemoryArena_masterCapacity(a)) {
                const MemoryHeader *h = (const MemoryHeader*) (p - sizeof(MemoryHeader));

                if (header_valid(h)) 
                    return h;
            }
        }
    }

    return nullptr;
}

/** Lazily initialize the default arena with its named default size. */
static fn ensure_initialized() {
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

    if (!registry_ensureDefault()) 
       return false;

    return arena_init(&s_default, totalBytes);
}
/** Return the process-wide default arena, initializing it on first access. */
MemoryArena *Memory_defaultArena() {
    ensure_initialized();

    return &s_default;
}
/** Reset all allocations in the default arena; dependent users must already be stopped. */
fn Memory_freeAll() {
    arena_freeAll(&s_default);
}
/** Enumerate default-arena live slab blocks matching typeId, returning total matches found. */
bytes Memory_findAll(u64 typeId, any *outArray, bytes maxCount) {
    return MemoryArena_findAll(&s_default, typeId, outArray, maxCount);
}
/** Total capacity a fresh arena needs to serve one payload of this length.
 *
 * An arena carves its slab classes from the front of its backing before the
 * bump region starts, so an owner that wants one large block asks for the
 * payload plus that carve. The geometry stays private to the allocator instead
 * of being copied into every owner that needs a big scratch block.
 */
bytes MemoryArena_capacityFor(bytes payloadBytes) {
    bytes slabBytes = 0;

    for (bytes s = 0; s < SLAB_COUNT; s++) 
        slabBytes += (bytes) s_slabCaps[s] * s_slabSizes[s];

    if (payloadBytes > SIZE_MAX - slabBytes - sizeof(MemoryHeader) - MEMORY_ALIGN_MASK) 
        return 0;

    return slabBytes + payloadBytes + sizeof(MemoryHeader) + MEMORY_ALIGN_MASK;
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
        Memory_systemFree(MemoryArena_masterRaw(a));

        Memory_systemFree(a);

        return nullptr;
    }

    return a;
}
/** Unregister and destroy a non-default arena and its backing storage. */
fn MemoryArena_destroy(MemoryArena *a) {
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

    MemoryArena_setLive(a, false);

    Memory_systemFree(MemoryArena_masterRaw(a));

    MemoryArena_setMasterRaw(a, nullptr);

    MemoryArena_setMasterArena(a, nullptr);

    MemoryArena_setBumpArena(a, nullptr);

    MemoryArena_setMasterCapacity(a, 0);

    Memory_systemFree(a);
}
/** Allocate a typed payload from the specified arena. */
any MemoryArena_alloc(MemoryArena *a, u64 typeId, bytes numBytes) {
    if (!a) 
        return nullptr;

    return arena_alloc(a, typeId, numBytes);
}
/** Allocate replacement storage in a, copy bytes from userPtr, and route old-block release by owner. */
any MemoryArena_realloc(MemoryArena *a, any userPtr, bytes newBytes) {
    if (!a) 
        return nullptr;

    if (!userPtr) 
        return arena_alloc(a, 0, newBytes);

    if (!Memory_safeHeader(userPtr)) 
        return nullptr;

    u64 typeId = Memory_type(userPtr);

    bytes oldLen = Memory_length(userPtr);

    any next = arena_alloc(a, typeId, newBytes);

    if (!next) 
        return nullptr;

    memcpy(next, userPtr, oldLen < newBytes ? oldLen : newBytes);

    Memory_free(userPtr);

    return next;
}
/** Attempt to recycle a validated payload within the specified arena. */
fn MemoryArena_free(MemoryArena *a, any userPtr) {
    if (!a) 
        return;

    arena_free(a, userPtr);
}
/** Reset every slab and bump allocation in the specified arena. */
fn MemoryArena_freeAll(MemoryArena *a) {
    if (!a) 
        return;

    arena_freeAll(a);
}
/** Enumerate live slab and bump blocks; total matches may exceed output capacity. */
bytes MemoryArena_findAll(MemoryArena *a, u64 typeId, any *outArray, bytes maxCount) {
    bytes count = 0;

    if (!a || !MemoryArena_live(a)) 
        return 0;

    SlabClass *slabs = MemoryArena_slabs(a);

    for (bytes s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &slabs[s];

        SpinLock_lock(SlabClass_lockMut(slab));

        u32 sz = SlabClass_slotSize(slab);

        u8 *slabArena = SlabClass_arena(slab);

        u32 cap = SlabClass_capacity(slab);

        for (bytes i = 0; i < cap; i++) {
            u8 *slot_ptr = slabArena + i * sz;

            MemoryHeader *h = (MemoryHeader*) slot_ptr;

            if (header_valid(h)) {
                if (typeId == 0 || MemoryHeader_typeId(h) == typeId) {
                    if (outArray && count < maxCount) 
                        outArray[count] = (any) (slot_ptr + sizeof(MemoryHeader));

                    count++;
                }
            }
        }

        SpinLock_unlock(SlabClass_lockMut(slab));
    }

    SpinLock_lock(MemoryArena_bumpLockMut(a));

    bytes offset = 0;

    bytes bumpOffset = MemoryArena_bumpOffset(a);

    u8 *bumpArena = MemoryArena_bumpArena(a);

    while (offset < bumpOffset) {
        bytes remaining = bumpOffset - offset;

        MemoryHeader *h = (MemoryHeader*) (bumpArena + offset);

        if (remaining < sizeof(MemoryHeader)) 
            break;

        bytes length = MemoryHeader_length(h);

        bytes total = sizeof(MemoryHeader) + ((length + MEMORY_ALIGN_MASK) & ~(u64) MEMORY_ALIGN_MASK);

        if (total > remaining) 
            break;

        if (header_valid(h) && (typeId == 0 || MemoryHeader_typeId(h) == typeId)) {
            if (outArray && count < maxCount) 
                outArray[count] = (u8*) h + sizeof(MemoryHeader);

            count++;
        }

        offset += total;
    }

    SpinLock_unlock(MemoryArena_bumpLockMut(a));

    return count;
}
/** Return occupied slab bytes plus the arena's current bump offset. */
bytes MemoryArena_activeBytes(MemoryArena *a) {
    bytes total = 0;

    if (!a || !MemoryArena_live(a)) 
        return 0;

    SlabClass *slabs = MemoryArena_slabs(a);

    for (bytes s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &slabs[s];

        SpinLock_lock(SlabClass_lockMut(slab));

        total += (bytes)SlabClass_count(slab) * SlabClass_slotSize(slab);

        SpinLock_unlock(SlabClass_lockMut(slab));
    }

    SpinLock_lock(MemoryArena_bumpLockMut(a));

    total += MemoryArena_bumpOffset(a);

    SpinLock_unlock(MemoryArena_bumpLockMut(a));

    return total;
}
/** Return the backing master-arena capacity, or zero for a null arena. */
bytes MemoryArena_capacity(MemoryArena *a) {
    if (!a) 
        return 0;

    return MemoryArena_masterCapacity(a);
}
/** Return the number of rejected requests since the arena's last init/freeAll. */
u64 MemoryArena_exhaustionCount(const MemoryArena *a) {
    return a ? MemoryArena_exhaustionCountRaw(a) : 0;
}
/** Return the default arena's rejected-request count since the last freeAll. */
u64 Memory_exhaustionCount() {
    return s_default.live ? s_default.exhaustionCount : 0;
}
