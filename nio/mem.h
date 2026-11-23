#ifndef NIO_MEM_H
#define NIO_MEM_H

#include "vex.h"

/**
 * Relational Engine owns this production ForeignMemory API and its implementation.
 * Canonical nio/mem.h includes preserve the existing C ABI for every consumer.
 *
 * 32-BYTE ARENA DOCTRINE (Vex Paradigm):
 * In standard C, programs malloc/free constantly. In vex, the engine carves
 * out one massive master arena at startup. memory is partitioned into
 * size-class slabs for O(1) slot claiming and cache-hot recycling.
 *
 * Every allocation returns a payload pointer whose 32-byte prefix holds four
 * 64-bit words:
 *
 * [ id with sugar ][ hash ][ length ]<*>[ pointer ]
 *
 * The id is the self-describing type word, form, project, modifier,
 * wrapper one, wrapper two, the BE6C sugar and the class number, so any
 * pointer names its own project and class without a registry lookup. `hash` is
 * the recomputed veto over (id, length, pointer); `length` is the payload's
 * byte count; `pointer` is the redirect this block carries, the object's own
 * address, or the address of its value when the id's form says the type is a
 * primitive (`Type_isPrimitive` on the id word decides which; the id's BE6C
 * sugar is what makes the id trustworthy enough to ask). A header therefore
 * validates two ways and fails closed on either: the BE6C sugar inside the id,
 * and the hash over the stated fields. Provenance beyond that is the
 * arena-range gate, never a magic cookie. Negative pointer math recovers the
 * header in 1 subtraction; Memory_type() and Memory_length() are free O(1)
 * reads with zero locking. The header is frozen: no growth room lives here
 * (struct evolution is detected per object via length + refused at the manifest
 * gate, never accommodated in-band).
 *
 * Blocks are immovable: `pointer` records an address, so a block must never be
 * relocated after allocation or that address goes stale. The master arena
 * mallocs its backing once and never moves a live block.
 */

#define MEMORY_HEADER_SIZE 32

/**
 * The ecosystem payload floor (Universal Alignment Law): every block the pool
 * and arenas hand out is 32-byte aligned on every supported platform. Consumers
 * may assert a type against it; a type that needs more than this is not
 * allocated directly here.
 */
#define MEMORY_PAYLOAD_ALIGN 32u
_Static_assert(MEMORY_PAYLOAD_ALIGN == 32, "the payload floor is 32 bytes");
#define MEMORY_ALIGN_MASK (MEMORY_PAYLOAD_ALIGN - 1u)

/**
 * Architecture-aware VM page quantum owned by MemoryPool:
 * 16 KiB on Apple Silicon (native VM & Metal zero-copy page), 4 KiB elsewhere.
 */
#if defined(VEX_PLATFORM_APPLE_SILICON)
#define MEMORY_PAGE_SIZE 16384u
#define MEMORY_PAGE_SHIFT 14u
#else
#define MEMORY_PAGE_SIZE 4096u
#define MEMORY_PAGE_SHIFT 12u
#endif
_Static_assert((1u << MEMORY_PAGE_SHIFT) == MEMORY_PAGE_SIZE, "MEMORY_PAGE_SHIFT must match MEMORY_PAGE_SIZE");

/**
 * 32-byte allocation prefix header.
 *
 * u64 typeId  - self-describing type id (BE6C sugar inside)
 * u64 hash    - recomputed veto over (typeId, length, pointer)
 * u64 length  - payload byte count
 * u64 pointer - redirect: the object's address, or a primitive's value address
 */
typedef struct MemoryHeader {
    u64 typeId;
    u64 hash;
    u64 length;
    u64 pointer;
} MemoryHeader;
_Static_assert(sizeof(MemoryHeader) == 32, "MemoryHeader must stay 32 Bytes");

;;GETTER
static inline u64 MemoryHeader_typeId(const MemoryHeader *h)
{
    return (*h).typeId;
}

;;GETTER
static inline u64 MemoryHeader_hash(const MemoryHeader *h)
{
    return (*h).hash;
}

;;GETTER
static inline bytes MemoryHeader_length(const MemoryHeader *h)
{
    return (bytes) (*h).length;
}

;;GETTER
static inline u64 MemoryHeader_pointer(const MemoryHeader *h)
{
    return (*h).pointer;
}

typedef struct MemoryPool MemoryPool;
typedef struct MemoryArena MemoryArena;
typedef struct PageMeta PageMeta;

bool Memory_init(bytes totalBytes);

any Memory_alloc(u64 typeId, MemoryArena *arena, bytes numBytes);

any Memory_realloc(any userPtr, bytes newBytes);

fn Memory_free(any userPtr);

fn Memory_freeAll();

bytes Memory_length(any userPtr);
u64 Memory_type(any userPtr);
bool Memory_isFreed(const_any userPtr);

bool Memory_similar(const_any a, const_any b);

bytes Memory_findAll(u64 typeId, any *outArray, bytes maxCount);

/**
 * Phase-4 instancing (Arena-B): the process-global allocator above is the
 * DEFAULT arena. Secondary arenas are fully isolated slab sets carved from
 * their own malloc, allocate in B, verify, free B, default untouched.
 * Headers are unchanged (32B, no arena tag), so this is ABI-stable: free
 * routes by address-range lookup across a growable registry.
 * Registration happens pre-threads; the hot path takes no extra locks.
 * Initialized disjoint alloc/free use per-region locks. Init/create/destroy,
 * reset/enumeration and shared-block access require caller exclusion. Freed raw
 * addresses have no generations: reuse may alias them; not a stale-handle API.
 * Slab blocks recycle individually in O(1). Large and spill blocks in the bump
 * region invalidate on free, coalesce with adjacent free neighbors, and reclaim
 * capacity individually (retracting the bump cursor when the tail clears).
 */

const MemoryHeader *Memory_safeHeader(const_any userPtr);
MemoryArena *Memory_arenaFor(any userPtr);

#ifdef NIO_MEM_IMPLEMENTATION

#include "atomic/atomic.h"

/**
 * Frame-scoped transient bump arena state.
 *
 * u8 *buffer              - 32-aligned scratch base
 * u8 *raw                 - allocation to release (buffer is rounded forward)
 * bytes capacity          - aligned scratch capacity in bytes
 * bytes bumpOffset        - current bump cursor offset
 * u32 generation          - incremented on every Transient_reset
 * SpinLock lock           - guards bump cursor and reset
 * bool live               - true once initialized
 * u64 exhaustionCount     - rejected scratch requests in this epoch
 * bool exhaustionReported - bounded loudness flag (reports first rejection once)
 */
typedef struct TransientArena {
    u8 *buffer;
    u8 *raw;
    bytes capacity;
    bytes bumpOffset;
    u32 generation;
    SpinLock lock;
    bool live;
    u64 exhaustionCount;
    bool exhaustionReported;
} TransientArena;

typedef enum MemoryPageKind {
    MEMORY_PAGE_FREE = 0,
    MEMORY_PAGE_SLAB = 1,
    MEMORY_PAGE_SPAN_HEAD = 2,
    MEMORY_PAGE_SPAN_CONT = 3,
} MemoryPageKind;

typedef struct MemoryFreeNode {
    struct MemoryFreeNode *next;
} MemoryFreeNode;

typedef struct PoolChunk PoolChunk;

typedef struct PageMeta {
    PoolChunk *chunk;
    MemoryArena *owner;
    MemoryFreeNode *freeHead;
    struct PageMeta *arenaPrev;
    struct PageMeta *arenaNext;
    u32 pageIndex;
    u32 spanPages;
    u16 liveSlots;
    u16 nextSlot;
    u8 kind;
    u8 slabClass;
    u8 pad0;
    u8 pad1;
} PageMeta;

;;GETTER
static inline MemoryFreeNode *MemoryFreeNode_next(const MemoryFreeNode *node)
{
    return (*node).next;
}
;;SETTER
static inline fn MemoryFreeNode_setNext(MemoryFreeNode *node, MemoryFreeNode *next)
{
    (*node).next = next;
}

;;GETTER
static inline PoolChunk *PageMeta_chunk(const PageMeta *pm)
{
    return (*pm).chunk;
}
;;SETTER
static inline fn PageMeta_setChunk(PageMeta *pm, PoolChunk *c)
{
    (*pm).chunk = c;
}
;;GETTER
static inline MemoryArena *PageMeta_owner(const PageMeta *pm)
{
    return (*pm).owner;
}
;;SETTER
static inline fn PageMeta_setOwner(PageMeta *pm, MemoryArena *a)
{
    (*pm).owner = a;
}
;;GETTER
static inline MemoryFreeNode *PageMeta_freeHead(const PageMeta *pm)
{
    return (*pm).freeHead;
}
;;SETTER
static inline fn PageMeta_setFreeHead(PageMeta *pm, MemoryFreeNode *n)
{
    (*pm).freeHead = n;
}
;;GETTER
static inline PageMeta *PageMeta_arenaPrev(const PageMeta *pm)
{
    return (*pm).arenaPrev;
}
;;SETTER
static inline fn PageMeta_setArenaPrev(PageMeta *pm, PageMeta *p)
{
    (*pm).arenaPrev = p;
}
;;GETTER
static inline PageMeta *PageMeta_arenaNext(const PageMeta *pm)
{
    return (*pm).arenaNext;
}
;;SETTER
static inline fn PageMeta_setArenaNext(PageMeta *pm, PageMeta *n)
{
    (*pm).arenaNext = n;
}
;;GETTER
static inline u32 PageMeta_pageIndex(const PageMeta *pm)
{
    return (*pm).pageIndex;
}
;;SETTER
static inline fn PageMeta_setPageIndex(PageMeta *pm, u32 idx)
{
    (*pm).pageIndex = idx;
}
;;GETTER
static inline u32 PageMeta_spanPages(const PageMeta *pm)
{
    return (*pm).spanPages;
}
;;SETTER
static inline fn PageMeta_setSpanPages(PageMeta *pm, u32 n)
{
    (*pm).spanPages = n;
}
;;GETTER
static inline u16 PageMeta_liveSlots(const PageMeta *pm)
{
    return (*pm).liveSlots;
}
;;SETTER
static inline fn PageMeta_setLiveSlots(PageMeta *pm, u16 n)
{
    (*pm).liveSlots = n;
}
;;GETTER
static inline u16 PageMeta_nextSlot(const PageMeta *pm)
{
    return (*pm).nextSlot;
}
;;SETTER
static inline fn PageMeta_setNextSlot(PageMeta *pm, u16 n)
{
    (*pm).nextSlot = n;
}
;;GETTER
static inline u8 PageMeta_kind(const PageMeta *pm)
{
    return (*pm).kind;
}
;;SETTER
static inline fn PageMeta_setKind(PageMeta *pm, u8 k)
{
    (*pm).kind = k;
}
;;GETTER
static inline u8 PageMeta_slabClass(const PageMeta *pm)
{
    return (*pm).slabClass;
}
;;SETTER
static inline fn PageMeta_setSlabClass(PageMeta *pm, u8 c)
{
    (*pm).slabClass = c;
}

PageMeta *MemoryPool_allocPages(MemoryPool *pool, MemoryArena *owner, bytes count, u8 kind, u8 slabClass);
fn MemoryPool_freePages(MemoryPool *pool, PageMeta *head);
PageMeta *MemoryPool_lookupPage(MemoryPool *pool, const_any ptr);
bool MemoryPool_containsPayload(MemoryPool *pool, const_any ptr);
u8 *MemoryPool_pageBase(const PageMeta *pm);

/** Round a freshly allocated base forward to the payload floor. */
static inline u8 *Memory_alignUp(const u8 *raw) {
    return (u8*) (((pointer) raw + MEMORY_ALIGN_MASK) & ~(pointer) MEMORY_ALIGN_MASK);
}

/**
 * Compute the nonzero header veto word from type id, length and payload address.
 * Hash-clarification veto over the stated identity, fed little-endian (never
 * serialized, but explicit anyway). LSB forced so a stored hash is never 0:
 * cleared (zeroed) headers always fail verification with no special case.
 */
static inline u64 Memory_headerHash(u64 typeId, u64 length, u64 pointer) {
    u64 hash = 1469598103934665603ull;

    const u64 words[3] = { typeId, length, pointer };

    for (i32 w = 0; w < 3; w++) {
        for (i32 i = 0; i < 8; i++) {
            hash ^= (words[w] >> (i * 8)) & 0xFFull;

            hash *= 1099511628211ull;
        }
    }

    return hash | 1ull;
}

;;SETTER
static inline fn MemoryHeader_setTypeId(MemoryHeader *h, u64 typeId)
{
    (*h).typeId = typeId;
}
;;SETTER
static inline fn MemoryHeader_setLength(MemoryHeader *h, u64 length)
{
    (*h).length = length;
}
;;SETTER
static inline fn MemoryHeader_setPointer(MemoryHeader *h, u64 ptr)
{
    (*h).pointer = ptr;
}
;;SETTER
static inline fn MemoryHeader_setHash(MemoryHeader *h, u64 hash)
{
    (*h).hash = hash;
}

/** Stamp a header: identity, length, the block's redirect, and the veto.
 * The redirect is the object's own address here; a primitive form redirects
 * to its value cell instead, which the id's form bits decide (Type_isPrimitive).
 */
static inline fn Memory_headerStamp(MemoryHeader *h, u64 typeId, bytes numBytes) {
    u64 ptr = (u64) (pointer) ((u8*) h + sizeof(MemoryHeader));
    MemoryHeader_setTypeId(h, typeId);

    MemoryHeader_setLength(h, (u64) numBytes);

    MemoryHeader_setPointer(h, ptr);

    MemoryHeader_setHash(h, Memory_headerHash(typeId, (u64) numBytes, ptr));
}

/**
 * Stamp a released header with the canonical FREED sentinel redirect and zero
 * hash so validation fails closed while double-free and UAF checks can detect
 * that this slot was previously live and freed.
 */
static inline fn Memory_headerStampFreed(MemoryHeader *h)
{
    MemoryHeader_setTypeId(h, 0);

    MemoryHeader_setHash(h, 0);

    MemoryHeader_setLength(h, 0);

    MemoryHeader_setPointer(h, (u64) (pointer) FREED);
}

#endif

MemoryPool *MemoryPool_default();
bytes MemoryPool_pageSize();
bytes MemoryPool_chunkCount(const MemoryPool *pool);
bytes MemoryPool_totalPages(const MemoryPool *pool);
bytes MemoryPool_usedPages(const MemoryPool *pool);

MemoryArena *Memory_defaultArena();
MemoryArena *Re_arena();

MemoryArena *MemoryArena_0();
MemoryArena *MemoryArena_1(bytes totalBytes);
#define MemoryArena(...) CONSTRUCTOR_DISPATCH(MemoryArena, __VA_ARGS__)
MemoryArena *MemoryArena_pages(bytes maxPages);
bytes MemoryArena_capacityFor(bytes payloadBytes);
fn MemoryArena_destroy(MemoryArena *a);
any MemoryArena_alloc(MemoryArena *a, u64 typeId, bytes numBytes);
any MemoryArena_realloc(MemoryArena *a, any userPtr, bytes newBytes);
fn MemoryArena_free(MemoryArena *a, any userPtr);
fn MemoryArena_freeAll(MemoryArena *a);
bytes MemoryArena_findAll(MemoryArena *a, u64 typeId, any *outArray, bytes maxCount);
bytes MemoryArena_activeBytes(MemoryArena *a);
bytes MemoryArena_capacity(MemoryArena *a);
bytes MemoryArena_usedPages(const MemoryArena *a);
bytes MemoryArena_maxPages(const MemoryArena *a);

u64 Memory_exhaustionCount();
u64 MemoryArena_exhaustionCount(const MemoryArena *a);

/** Lifetime verification classification */
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
