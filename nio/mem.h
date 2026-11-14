#ifndef NIO_MEM_H
#define NIO_MEM_H

#include "vex.h"
#include "lang/primitive.h"

// Relational Engine owns this production ForeignMemory API and its implementation.
// Canonical nio/mem.h includes preserve the existing C ABI for every consumer.
//
// 32-BYTE ARENA DOCTRINE (Vex Paradigm):
// In standard C, programs malloc/free constantly. In vex, the engine carves
// out one massive master arena at startup. memory is partitioned into
// size-class slabs for O(1) slot claiming and cache-hot recycling.
//
// Every allocation returns a payload pointer whose 32-byte prefix holds four
// 64-bit words:
//
//     [ id with sugar ][ hash ][ length ]<*>[ pointer ]
//
// The id is the triple-sugar self-describe word — form, project, modifier,
// wrapper one, wrapper two, the BE6C sugar and the class number — so any
// pointer names its own project and class without a registry lookup. `hash` is
// the recomputed veto over (id, length, pointer); `length` is the payload's
// byte count; `pointer` is the redirect this block carries — the object's own
// address, or the address of its value when the id's form says the type is a
// primitive (`Type_isPrimitive` on the id word decides which; the id's BE6C
// sugar is what makes the id trustworthy enough to ask). A header therefore
// validates two ways and fails closed on either: the BE6C sugar inside the id,
// and the hash over the stated fields. Provenance beyond that is the
// arena-range gate, never a magic cookie. Negative pointer math recovers the
// header in 1 subtraction; Memory_type() and Memory_length() are free O(1)
// reads with zero locking. The header is frozen: no growth room lives here
// (struct evolution is detected per object via length + refused at the manifest
// gate, never accommodated in-band).
//
// Blocks are immovable: `pointer` records an address, so a block must never be
// relocated after allocation or that address goes stale. The master arena
// mallocs its backing once and never moves a live block.

#define MEMORY_HEADER_SIZE 32

// The ecosystem payload floor (Universal Alignment Law): every block the three
// arenas hand out is 32-byte aligned on every supported platform. Consumers may
// assert a type against it; a type that needs more than this is not allocated
// directly here.
#define MEMORY_PAYLOAD_ALIGN 32u
_Static_assert(MEMORY_PAYLOAD_ALIGN == 32, "the payload floor is 32 bytes");
#define MEMORY_ALIGN_MASK (MEMORY_PAYLOAD_ALIGN - 1u)

typedef struct MemoryHeader {
    u64 typeId;   // triple-sugar self-describe id (BE6C sugar inside)
    u64 hash;     // recomputed veto over (typeId, length, pointer)
    u64 length;   // payload byte count
    u64 pointer;  // redirect: the object's address, or a primitive's value address
} MemoryHeader;
_Static_assert(sizeof(MemoryHeader) == 32, "MemoryHeader must stay 32 Bytes");

bool Memory_init(bytes totalBytes);

any Memory_alloc(u64 typeId, bytes numBytes);

any Memory_realloc(any userPtr, bytes newBytes);

fn Memory_free(any userPtr);

fn Memory_freeAll();

bytes Memory_length(any userPtr);
u64 Memory_type(any userPtr);

bool Memory_similar(const_any a, const_any b);

bytes Memory_findAll(u64 typeId, any *outArray, bytes maxCount);

// Phase-4 instancing (Arena-B): the process-global allocator above is the
// DEFAULT arena. Secondary arenas are fully isolated slab sets carved from
// their own malloc — allocate in B, verify, free B, default untouched.
// Headers are unchanged (32B, no arena tag), so this is ABI-stable: free
// routes by address-range lookup across a growable registry.
// Registration happens pre-threads; the hot path takes no extra locks.
// Initialized disjoint alloc/free use per-region locks. Init/create/destroy,
// reset/enumeration and shared-block access require caller exclusion. Freed raw
// addresses have no generations: reuse may alias them; not a stale-handle API.
// Slab blocks recycle individually. Bump blocks (including small overflow spill)
// invalidate on free but reclaim capacity only on freeAll, never individually.
typedef struct MemoryArena MemoryArena;

// Private allocator seam, kept here so nio has one public header. The system
// wrappers are the only libc allocation path; arena.c and transient.c call
// them. Consumers may see these declarations but never call them directly.
any Memory_systemAlloc(bytes size);
any Memory_systemCalloc(bytes count, bytes size);
any Memory_systemRealloc(any ptr, bytes size);
fn Memory_systemFree(any ptr);

const MemoryHeader *Memory_safeHeader(const_any userPtr);
MemoryArena *Memory_arenaFor(any userPtr);

#ifdef NIO_MEM_IMPLEMENTATION

/** Round a freshly allocated base forward to the payload floor. */
static u8 *Memory_alignUp(const u8 *raw) {
    return (u8*) (((pointer) raw + MEMORY_ALIGN_MASK) & ~(pointer) MEMORY_ALIGN_MASK);
}

/**
 * Compute the nonzero header veto word from type id, length and payload address.
 * Hash-clarification veto over the stated identity, fed little-endian (never
 * serialized, but explicit anyway). LSB forced so a stored hash is never 0:
 * cleared (zeroed) headers always fail verification with no special case.
 */
static u64 Memory_headerHash(u64 typeId, u64 length, u64 pointer) {
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
 * The redirect is the object's own address here; a primitive form redirects
 * to its value cell instead, which the id's form bits decide (Type_isPrimitive).
 */
static fn Memory_headerStamp(MemoryHeader *h, u64 typeId, bytes numBytes) {
    (*h).typeId = typeId;
    (*h).length = (u64) numBytes;
    (*h).pointer = (u64) (pointer) ((u8*) h + sizeof(MemoryHeader));
    (*h).hash = Memory_headerHash((*h).typeId, (*h).length, (*h).pointer);
}

#endif

MemoryArena *Memory_defaultArena();

MemoryArena *MemoryArena_create(bytes totalBytes);
fn MemoryArena_destroy(MemoryArena *a);
any MemoryArena_alloc(MemoryArena *a, u64 typeId, bytes numBytes);
any MemoryArena_realloc(MemoryArena *a, any userPtr, bytes newBytes);
fn MemoryArena_free(MemoryArena *a, any userPtr);
fn MemoryArena_freeAll(MemoryArena *a);
bytes MemoryArena_findAll(MemoryArena *a, u64 typeId, any *outArray, bytes maxCount);
bytes MemoryArena_activeBytes(MemoryArena *a);
bytes MemoryArena_capacity(MemoryArena *a);

u64 Memory_exhaustionCount();
u64 MemoryArena_exhaustionCount(const MemoryArena *a);

// Lifetime verification classification
typedef enum MemoryLifetime {
    MEMORY_LIFETIME_UNKNOWN = 0,
    MEMORY_LIFETIME_PERMANENT,
    MEMORY_LIFETIME_TRANSIENT,
} MemoryLifetime;

bool Memory_initTransient(bytes capacity);
any Transient_alloc(u64 typeId, bytes numBytes);
fn Transient_reset();
bool Transient_contains(const_any ptr);
u32 Transient_getGeneration();
u64 Transient_exhaustionCount();
#if defined(DEBUG_BORROW_CHECK)
;;VALIDATOR
const u8 *Transient_getBuffer();
#endif
MemoryLifetime Memory_getLifetime(const_any ptr);

#endif
