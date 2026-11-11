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

typedef struct MemoryHeader {
    u64 typeId;   // triple-sugar self-describe id (BE6C sugar inside)
    u64 hash;     // recomputed veto over (typeId, length, pointer)
    u64 length;   // payload byte count
    u64 pointer;  // redirect: the object's address, or a primitive's value address
} MemoryHeader;
_Static_assert(sizeof(MemoryHeader) == 32, "MemoryHeader must stay 32 Bytes");

bool Memory_init(bytes totalBytes);

void *Memory_alloc(u64 typeId, bytes numBytes);

void *Memory_realloc(void *userPtr, bytes newBytes);

void Memory_free(void *userPtr);

void Memory_freeAll(void);

bytes Memory_length(void *userPtr);
u64 Memory_type(void *userPtr);

bool Memory_similar(const void *a, const void *b);

bytes Memory_findAll(u64 typeId, void **outArray, bytes maxCount);

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

MemoryArena *Memory_defaultArena(void);

MemoryArena *MemoryArena_create(bytes totalBytes);
void MemoryArena_destroy(MemoryArena *a);
void *MemoryArena_alloc(MemoryArena *a, u64 typeId, bytes numBytes);
void *MemoryArena_realloc(MemoryArena *a, void *userPtr, bytes newBytes);
void MemoryArena_free(MemoryArena *a, void *userPtr);
void MemoryArena_freeAll(MemoryArena *a);
bytes MemoryArena_findAll(MemoryArena *a, u64 typeId, void **outArray, bytes maxCount);
bytes MemoryArena_activeBytes(MemoryArena *a);
bytes MemoryArena_capacity(MemoryArena *a);

u64 Memory_exhaustionCount(void);
u64 MemoryArena_exhaustionCount(const MemoryArena *a);

// Lifetime verification classification
typedef enum MemoryLifetime {
    MEMORY_LIFETIME_UNKNOWN = 0,
    MEMORY_LIFETIME_PERMANENT,
    MEMORY_LIFETIME_TRANSIENT,
} MemoryLifetime;

bool Memory_initTransient(bytes capacity);
void *Transient_alloc(u64 typeId, bytes numBytes);
void Transient_reset(void);
bool Transient_contains(const void *ptr);
u32 Transient_getGeneration(void);
u64 Transient_exhaustionCount(void);
#if defined(DEBUG_BORROW_CHECK)
;;VALIDATOR
const u8 *Transient_getBuffer(void);
#endif
MemoryLifetime Memory_getLifetime(const void *ptr);

#endif
