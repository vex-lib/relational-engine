#include "nio/mem.h"
// Production R2 storage owner: Relational Engine. Memory_* ABI is preserved;
// this native allocator is not the separate Rust byte/string learning API.
#include "atomic/atomic.h"
#include "debug/throw.h"

#include "vex.h"
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ForeignMemory
 * ============================================================================
 * Pre-allocated Master Arena and Size-Class Slab Allocator fulfilling the
 * Vex Paradigm: zero steady-state malloc, cache-hot slot recycling, and
 * 32-byte negative pointer math. The 32-byte MemoryHeader (id with sugar,
 * hash, length, pointer) is read backwards from the user pointer; the hash is
 * the recomputed veto over the stated fields and the stored pointer is the
 * block's own payload address, so cleared headers always fail verification
 * with no special case. Phase-4 instancing makes the globals the default
 * MemoryArena while secondaries register for address-range free routing.
 * Teardown is Memory_freeAll last, after every dependent subsystem has released
 * its blocks.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ForeignMemory (nio/mem)
 * ============================================================================
 * Pre-allocated Master Arena and Size-Class Slab Allocator fulfilling the
 * Vex Paradigm: zero steady-state malloc, cache-hot slot recycling, and
 * 32-byte negative pointer math.
 *
 * Phase-4 instancing: globals are the DEFAULT MemoryArena; secondaries
 * register for address-range free routing. Header layout untouched.
 *
 * STRUCT FIELDS (Mirroring nio/mem.h + local to this file):
 * ----------------------------------------------------------------------------
 *   MemoryHeader {
 *     u64 typeId;  // triple-sugar self-describe id (BE6C sugar inside)
 *     u64 hash;    // recomputed veto over (typeId, length, pointer)
 *     u64 length;  // payload byte count
 *     u64 pointer; // redirect: the object's address, or a primitive's value
 *   }
 *   Zones: identity (id+length+pointer, stated), provenance (hash veto +
 *   arena-range gate, no magic cookie), allocator (size class is a pure
 *   function of length — no stored index), future (none in-band; struct
 *   evolution is detected per object via length + refused at the manifest
 *   gate). The header is frozen at 32 Bytes, read backwards.
 *   SlabClass {
 *     u32 slot_size;        // Bytes per slot in this size class
 *     u32 capacity;         // total slots carved from the arena
 *     u32 count;            // live (checked-out) slots
 *     u8 *arena;            // backing store for this class
 *     FreeNode *free_head;       // lock-free recycled slot stack
 *     SpinLock lock;             // serializes alloc/free on this class
 *   }
 *   MemoryArena {
 *     SlabClass slabs[SLAB_COUNT]; // size-class slab table (64B..4K)
 *     u8 *masterArena;      // 32-aligned base the slabs are carved from
 *     u8 *masterRaw;        // allocation to release (rounded base +31)
 *     bytes masterCapacity;     // usable master bytes after the rounding
 *     u8 *bumpArena;        // large/allocation bump region
 *     bytes bumpCapacity;       // bump region capacity
 *     bytes bumpOffset;         // bump cursor (monotonic)
 *     SpinLock bumpLock;         // serializes bump allocation
 *     SpinLock initLock;         // serializes lazy arena init
 *     bool live;                 // arena ready flag
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - Memory_init(totalBytes)
 *   - Memory_alloc(typeId, numBytes)
 *   - Memory_defaultArena(void)
 *   - Memory_realloc(userPtr, newBytes)
 *   - Memory_free(userPtr)
 *   - Memory_freeAll(void)
 *   - Memory_length(userPtr)
 *   - Memory_type(userPtr)
 *   - Memory_similar(a, b.sh)
 *   - Memory_findAll(typeId, outArray, maxCount)
 *
 * Arena Functions (Phase-4):
 *   - MemoryArena_create(totalBytes)
 *   - MemoryArena_destroy(a)
 *   - MemoryArena_alloc(a, typeId, numBytes)
 *   - MemoryArena_realloc(a, userPtr, newBytes)
 *   - MemoryArena_free(a, userPtr)
 *   - MemoryArena_freeAll(a)
 *   - MemoryArena_findAll(a, typeId, outArray, maxCount)
 *   - MemoryArena_activeBytes(a)
 *   - MemoryArena_capacity(a)
 *
 * Transient Functions (Dynamic Lifetime Verifier):
 *   Scratch payload lengths fit the 64-bit header length; a request whose
 *   header-plus-alignment arithmetic overflows, or that exceeds capacity, is
 *   refused LOUDLY once per epoch (THROW) and counted thereafter, without
 *   changing headers, generation or bump cursor. Initialization capacity must
 *   round to the 32-byte payload floor without bytes overflow.
 *   - Memory_initTransient(capacity)
 *   - Transient_alloc(typeId, numBytes)
 *   - Transient_reset(void)
 *   - Transient_contains(ptr)
 *   - Transient_getGeneration(void)
 *   - Transient_getBuffer(void)
 *   - Memory_getLifetime(ptr)
 *
 * CAPACITY / OWNERSHIP:
 *   Construction owns all backing; alloc never escapes into libc on exhaustion.
 *   Exhaustion returns nullptr and is reported once per epoch (THROW) with the
 *   request size, then counted (Memory_exhaustionCount / MemoryArena_exhaustionCount)
 *   without changing existing blocks. Slab slots recycle individually; bump blocks
 *   invalidate individually and reclaim space only at freeAll. Enumeration includes
 *   both storage regions. Registry mutation/reset/destroy/metadata enumeration
 *   require exclusion; initialized disjoint alloc/free may contend on class/bump
 *   locks. Raw pointers have no generations: reuse can alias an old address. Not
 *   stale-handle safety.
 * ============================================================================
 */

#define SLAB_COUNT 7
;;INTENTION("fixed SLAB_COUNT: allocator slab classes are a design constant, not a workload ceiling")
#define ANTI_ARENA_DEFAULT_SIZE (64 * 1024 * 1024) // 64 MB master arena

// The payload floor (MEMORY_PAYLOAD_ALIGN, declared in mem.h) is CONSTRUCTED,
// never inherited: malloc promises 16 on Windows x64/arm64 and macOS arm64, so
// each arena over-allocates one unit and rounds its base forward, and every
// slot size and stride is a multiple of 32. The 32-byte header then sits 32
// bytes behind a 32-byte payload, so a block's own start is aligned too.
#define MEMORY_ALIGN_MASK (MEMORY_PAYLOAD_ALIGN - 1u)

/** Round a freshly allocated base forward to the payload floor. */
static inline u8 *align_up(const u8 *raw) {
    return (u8*) (((uintptr_t) raw + MEMORY_ALIGN_MASK) & ~(uintptr_t) MEMORY_ALIGN_MASK);
}


/**
 * Compute the nonzero header veto word from type id, length and payload address.
 * Hash-clarification veto over the stated identity, fed little-endian (never
 * serialized, but explicit anyway). LSB forced so a stored hash is never 0:
 * cleared (zeroed) headers always fail verification with no special case.
 */
static u64 header_hash(u64 typeId, u64 length, u64 pointer) {
    u64 hash = 1469598103934665603ull;
    const u64 words[3] = { typeId, length, pointer };
    for (int w = 0; w < 3; w++) {
        for (int i = 0; i < 8; i++) {
            hash ^= (words[w] >> (i * 8)) & 0xFFull;
            hash *= 1099511628211ull;
        }
    }
    return hash | 1ull;
}

/** Stamp a header: identity, length, the block's redirect, and the veto.
 * he redirect is the object's own address here; a primitive form redirects
 * to its value cell instead, which the id's form bits decide (Type_isPrimitive).
 */
static void header_stamp(MemoryHeader *h, u64 typeId, bytes numBytes) {
    (*h).typeId = typeId;
    (*h).length = (u64) numBytes;
    (*h).pointer = (u64) (uintptr_t) ((u8*) h + sizeof(MemoryHeader));
    (*h).hash = header_hash((*h).typeId, (*h).length, (*h).pointer);
}

// Triple sugar: BE6C inside the id word, the hash over the stated fields, and
// the redirect the block carries. The hash vetoes the redirect's value without
// constraining what it addresses, so an object and a primitive block are both
// admissible and the id tells a reader which it is.
/** Validate a header against its own stated fields. */
static bool header_valid(const MemoryHeader *h) {
    if (!h)
        return false;
    return (*h).hash == header_hash((*h).typeId, (*h).length, (*h).pointer);
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

struct MemoryArena {
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
};

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
        MemoryArena **nb = (MemoryArena**) realloc(
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
static bool registry_ensureDefault(void) {
    if (s_registryCount == 0) return registry_push(&s_default);
    return true;
}

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


/** Select the smallest slab class that can hold a header and payload, or -1 for bump storage. */
static inline int find_slab(bytes payload_bytes) {
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
    u8 *raw = (u8*) malloc(alignedCap + MEMORY_PAYLOAD_ALIGN);
    if (!raw)
        return false;
    u8 *master = align_up(raw);

    u8 *cur = master;
    bytes left = alignedCap;
    for (bytes s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &(*a).slabs[s];
        bytes need = (bytes)(*slab).capacity * (*slab).slot_size;
        if (need > left) {
            free(raw);
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
            header_stamp(h, typeId, numBytes);
            return (void*) node;
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
        header_stamp(h, typeId, numBytes);
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

    uintptr_t u = (uintptr_t) userPtr;
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
        uintptr_t base = (uintptr_t) (*candidate).arena;
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
static MemoryArena *arena_for(void *userPtr) {
    if (!userPtr)
        return nullptr;
    uintptr_t u = (uintptr_t) userPtr;
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
static const MemoryHeader *safe_header(const void *userPtr) {
    if (!userPtr)
        return nullptr;
    uintptr_t u = (uintptr_t) userPtr;
    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0)
        return nullptr;

    u8 *p = (u8*) userPtr;

    if (s_transient.live && s_transient.buffer) {
        if (p >= s_transient.buffer + sizeof(MemoryHeader) && p < s_transient.buffer + s_transient.bumpOffset) {
            const MemoryHeader *h = (const MemoryHeader*) (p - sizeof(MemoryHeader));
            if (header_valid(h))
                return h;
        }
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
static inline void ensure_initialized(void) {
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

/** Allocate a default-arena payload with a 32-byte self-describing header. */
void *Memory_alloc(u64 typeId, bytes numBytes) {
    ensure_initialized();
    return arena_alloc(&s_default, typeId, numBytes);
}

/** Return the process-wide default arena, initializing it on first access. */
MemoryArena *Memory_defaultArena(void) {
    ensure_initialized();
    return &s_default;
}

/** Allocate a replacement in the default arena, copy the shorter length, and free the old arena block. */
void *Memory_realloc(void *userPtr, bytes newBytes) {
    if (!userPtr)
        return Memory_alloc(0, newBytes);
    if (!safe_header(userPtr))
        return nullptr;

    u64 typeId = Memory_type(userPtr);
    bytes oldLen = Memory_length(userPtr);
    void *next = Memory_alloc(typeId, newBytes);
    if (!next)
        return nullptr;

    memcpy(next, userPtr, oldLen < newBytes ? oldLen : newBytes);
    Memory_free(userPtr);
    return next;
}

/** Route a validated permanent block to its owning arena; transient or invalid pointers are ignored. */
void Memory_free(void *userPtr) {
    if (!userPtr)
        return;
    if (Transient_contains(userPtr))
        return;

    const MemoryHeader *h = safe_header(userPtr);
    if (!h)
        return;

    MemoryArena *a = arena_for(userPtr);
    if (!a)
        return;
    arena_free(a, userPtr);
}

/** Reset all allocations in the default arena; dependent users must already be stopped. */
void Memory_freeAll(void) {
    arena_freeAll(&s_default);
}

/** Return a validated block's payload length, or zero when the pointer is rejected. */
bytes Memory_length(void *userPtr) {
    const MemoryHeader *h = safe_header(userPtr);
    if (h)
        return (bytes) (*h).length;
    return 0;
}

/** Return a validated block's type id, or zero when the pointer is rejected. */
u64 Memory_type(void *userPtr) {
    const MemoryHeader *h = safe_header(userPtr);
    if (h)
        return (*h).typeId;
    return 0;
}

/** Return whether both validated blocks carry the same type id. */
bool Memory_similar(const void *a, const void *b) {
    if (!a || !b)
        return false;
    const MemoryHeader *ha = safe_header(a);
    if (!ha)
        return false;
    const MemoryHeader *hb = safe_header(b);
    if (!hb)
        return false;
    return (*ha).typeId == (*hb).typeId;
}

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
    u8 *raw = (u8*) malloc(aligned_cap + MEMORY_PAYLOAD_ALIGN);
    if (!raw) {
        SpinLock_unlock(&s_transient.lock);
        return false;
    }

    s_transient.raw = raw;
    s_transient.buffer = align_up(raw);
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
static inline void ensure_transient_initialized(void) {
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
    header_stamp(h, typeId, numBytes);

    return (void*) (slot + sizeof(MemoryHeader));
}

/** Rewind the transient bump cursor and advance its generation, invalidating prior borrows. */
void Transient_reset(void) {
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
u32 Transient_getGeneration(void) {
    return s_transient.generation;
}

/** Return the number of rejected scratch requests since the last reset. */
u64 Transient_exhaustionCount(void) {
    return s_transient.exhaustionCount;
}

#if defined(DEBUG_BORROW_CHECK)
;;VALIDATOR
/** Return the transient arena backing buffer for debug-only borrow diagnostics. */
const u8 *Transient_getBuffer(void) {
    return s_transient.buffer;
}
#endif

/** Classify a pointer as transient, permanent, or unknown using registered address ranges. */
MemoryLifetime Memory_getLifetime(const void *ptr) {
    if (!ptr)
        return MEMORY_LIFETIME_UNKNOWN;
    if (Transient_contains(ptr))
        return MEMORY_LIFETIME_TRANSIENT;
    if (arena_for((void*) ptr) != nullptr)
        return MEMORY_LIFETIME_PERMANENT;
    return MEMORY_LIFETIME_UNKNOWN;
}


/** Enumerate default-arena live slab blocks matching typeId, returning total matches found. */
bytes Memory_findAll(u64 typeId, void **outArray, bytes maxCount) {
    return MemoryArena_findAll(&s_default, typeId, outArray, maxCount);
}

/** Allocate, initialize, and register an independent arena with the requested capacity. */
MemoryArena *MemoryArena_create(bytes totalBytes) {
    MemoryArena *a = (MemoryArena*) calloc(1, sizeof(MemoryArena));
    if (!a)
        return nullptr;
    if (!arena_init(a, totalBytes)) {
        free(a);
        return nullptr;
    }
    SpinLock_lock(&s_registryLock);
    bool placed = registry_push(a);
    SpinLock_unlock(&s_registryLock);
    if (!placed) {
        free((*a).masterRaw);
        free(a);
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
    free((*a).masterRaw);
    (*a).masterRaw = nullptr;
    (*a).masterArena = nullptr;
    (*a).bumpArena = nullptr;
    (*a).masterCapacity = 0;
    free(a);
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
    if (!safe_header(userPtr))
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
u64 Memory_exhaustionCount(void) {
    return s_default.live ? s_default.exhaustionCount : 0;
}
