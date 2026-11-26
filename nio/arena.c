#define NIO_MEM_IMPLEMENTATION

#include "vex.h"
#include "nio/mem.h"

#include <string.h>

#include "atomic/atomic.h"
#include "debug/throw.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: MemoryArena
 * ============================================================================
 * Tier 2 of the relational memory architecture (Pool to Arena to any).
 * Lightweight logical owner drawing arch-native pages and contiguous spans
 * from MemoryPool (pool.c):
 *   - maxPages == 0: grows unboundedly until OS OOM.
 *   - maxPages > 0: enforces a hard per-arena page budget.
 * Serves 7 slab size classes (64B..4096B) and multi-page spans with frozen
 * 32-byte MemoryHeader prefixes and 32-byte payload alignment, returning empty
 * pages immediately to MemoryPool on free/freeAll/destroy.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: MemoryArena (nio/arena)
 * ============================================================================
 * Logical arena allocator layered over MemoryPool. Holds zero OS VM calls
 * directly; all virtual memory mapping and page reclamation live in pool.c.
 * ============================================================================
 */

#define SLAB_COUNT 7u
#define ANTI_ARENA_DEFAULT_SIZE (64u * 1024u * 1024u)

struct MemoryArena {
    MemoryPool *pool;
    bytes maxPages;
    bytes usedPages;
    bytes activeBytes;
    PageMeta *slabHeads[SLAB_COUNT];
    PageMeta *spanHead;
    SpinLock lock;
    SpinLock initLock;
    bool live;
    u64 exhaustionCount;
    bool exhaustionReported;
};

static const u32 s_slabSizes[SLAB_COUNT] = { 64u, 128u, 256u, 512u, 1024u, 2048u, 4096u };

static MemoryArena s_default = { 0 };
static MemoryArena s_arenaTable = { 0 };

;;GETTER
static inline MemoryPool *MemoryArena_pool(const MemoryArena *a)
{
    return (*a).pool;
}
;;SETTER
static inline fn MemoryArena_setPool(MemoryArena *a, MemoryPool *p)
{
    (*a).pool = p;
}
;;GETTER
static inline bytes MemoryArena_maxPagesRaw(const MemoryArena *a)
{
    return (*a).maxPages;
}
;;SETTER
static inline fn MemoryArena_setMaxPages(MemoryArena *a, bytes n)
{
    (*a).maxPages = n;
}
;;GETTER
static inline bytes MemoryArena_usedPagesRaw(const MemoryArena *a)
{
    return (*a).usedPages;
}
;;SETTER
static inline fn MemoryArena_setUsedPages(MemoryArena *a, bytes n)
{
    (*a).usedPages = n;
}
;;GETTER
static inline bytes MemoryArena_activeBytesRaw(const MemoryArena *a)
{
    return (*a).activeBytes;
}
;;SETTER
static inline fn MemoryArena_setActiveBytes(MemoryArena *a, bytes n)
{
    (*a).activeBytes = n;
}
;;GETTER
static inline PageMeta *MemoryArena_slabHead(const MemoryArena *a, bytes cls)
{
    return (*a).slabHeads[cls];
}
;;SETTER
static inline fn MemoryArena_setSlabHead(MemoryArena *a, bytes cls, PageMeta *pm)
{
    (*a).slabHeads[cls] = pm;
}
;;GETTER
static inline PageMeta *MemoryArena_spanHead(const MemoryArena *a)
{
    return (*a).spanHead;
}
;;SETTER
static inline fn MemoryArena_setSpanHead(MemoryArena *a, PageMeta *pm)
{
    (*a).spanHead = pm;
}
;;GETTER
static inline SpinLock *MemoryArena_lockMut(MemoryArena *a)
{
    return &(*a).lock;
}
;;GETTER
static inline SpinLock *MemoryArena_initLockMut(MemoryArena *a)
{
    return &(*a).initLock;
}
;;SETTER
static inline fn MemoryArena_setLock(MemoryArena *a, SpinLock l)
{
    (*a).lock = l;
}
;;SETTER
static inline fn MemoryArena_setInitLock(MemoryArena *a, SpinLock l)
{
    (*a).initLock = l;
}
;;GETTER
static inline bool MemoryArena_live(const MemoryArena *a)
{
    return (*a).live;
}
;;SETTER
static inline fn MemoryArena_setLive(MemoryArena *a, bool v)
{
    (*a).live = v;
}
;;GETTER
static inline u64 MemoryArena_exhaustionCountRaw(const MemoryArena *a)
{
    return (*a).exhaustionCount;
}
;;SETTER
static inline fn MemoryArena_setExhaustionCount(MemoryArena *a, u64 n)
{
    (*a).exhaustionCount = n;
}
;;GETTER
static inline bool MemoryArena_exhaustionReported(const MemoryArena *a)
{
    return (*a).exhaustionReported;
}
;;SETTER
static inline fn MemoryArena_setExhaustionReported(MemoryArena *a, bool v)
{
    (*a).exhaustionReported = v;
}

/** Validate a header against its own stated fields. */
static bool header_valid(const MemoryHeader *h) {
    if (!h)
        return false;

    return MemoryHeader_hash(h) == Memory_headerHash(MemoryHeader_typeId(h), MemoryHeader_length(h), MemoryHeader_pointer(h));
}

/** Return whether a block header carries the canonical FREED tombstone stamp. */
static bool header_isFreed(const MemoryHeader *h)
{
    if (!h)
        return false;

    return MemoryHeader_typeId(h) == 0 &&
           MemoryHeader_hash(h) == 0 &&
           MemoryHeader_length(h) == 0 &&
           MemoryHeader_pointer(h) == (u64) (pointer) FREED;
}

/** Select the smallest slab class that fits header + payload, or -1 for multi-page spans. */
static i32 find_slab(bytes payloadBytes) {
    bytes needed = payloadBytes + sizeof(MemoryHeader);

    if (needed <= 64u)
        return 0;

    if (needed <= 128u)
        return 1;

    if (needed <= 256u)
        return 2;

    if (needed <= 512u)
        return 3;

    if (needed <= 1024u)
        return 4;

    if (needed <= 2048u)
        return 5;

    if (needed <= 4096u)
        return 6;

    return -1;
}

/** Link a slab or span page at the head of an arena's doubly-linked list. */
static fn list_push(PageMeta **headRef, PageMeta *pm) {
    PageMeta *old = *headRef;
    PageMeta_setArenaPrev(pm, nullptr);
    PageMeta_setArenaNext(pm, old);

    if (old)
        PageMeta_setArenaPrev(old, pm);

    *headRef = pm;
}

/** Unlink a slab or span page from an arena's doubly-linked list. */
static fn list_remove(PageMeta **headRef, PageMeta *pm) {
    PageMeta *prev = PageMeta_arenaPrev(pm);
    PageMeta *next = PageMeta_arenaNext(pm);

    if (prev)
        PageMeta_setArenaNext(prev, next);
    else
        *headRef = next;

    if (next)
        PageMeta_setArenaPrev(next, prev);

    PageMeta_setArenaPrev(pm, nullptr);
    PageMeta_setArenaNext(pm, nullptr);
}

/** Record an arena exhaustion event loudly once per epoch and increment its counter. */
;;INTENTION("exhaustion is a cold, terminal detection reached from a hot allocator: it reports once per epoch (never per call) and counts the rest")
static fn arena_recordExhaustion(MemoryArena *a, bytes numBytes, string reason) {
    MemoryArena_setExhaustionCount(a, MemoryArena_exhaustionCountRaw(a) + 1u);

    if (!MemoryArena_exhaustionReported(a)) {
        MemoryArena_setExhaustionReported(a, true);
        THROW("memory: %s, request %zu bytes (pages %zu/%zu)",
              reason, numBytes, MemoryArena_usedPagesRaw(a), MemoryArena_maxPagesRaw(a));
    }
}

/** Initialize an arena descriptor bound to pool with an optional page ceiling (0 = unbounded). */
static bool arena_initPages(MemoryArena *a, MemoryPool *pool, bytes maxPages) {
    if (!a || !pool)
        return false;

    MemoryArena_setPool(a, pool);
    MemoryArena_setMaxPages(a, maxPages);
    MemoryArena_setUsedPages(a, 0);
    MemoryArena_setActiveBytes(a, 0);

    for (bytes s = 0; s < SLAB_COUNT; s++)
        MemoryArena_setSlabHead(a, s, nullptr);

    MemoryArena_setSpanHead(a, nullptr);
    MemoryArena_setLock(a, SPIN_LOCK_INIT);
    MemoryArena_setInitLock(a, SPIN_LOCK_INIT);
    MemoryArena_setExhaustionCount(a, 0);
    MemoryArena_setExhaustionReported(a, false);
    MemoryArena_setLive(a, true);

    return true;
}

/** Allocate a 32-byte aligned typed payload from arena, pulling pages from its pool as needed. */
static any arena_alloc(MemoryArena *a, u64 typeId, bytes numBytes) {
    if (!a || !MemoryArena_live(a))
        return nullptr;

    if (numBytes > SIZE_MAX - sizeof(MemoryHeader) - MEMORY_PAGE_SIZE) {
        SpinLock_lock(MemoryArena_lockMut(a));
        arena_recordExhaustion(a, numBytes, "request overflows block size");
        SpinLock_unlock(MemoryArena_lockMut(a));
        return nullptr;
    }

    i32 sIdx = find_slab(numBytes);

    if (sIdx >= 0) {
        u32 slotSize = s_slabSizes[sIdx];
        u16 slotsPerPage = (u16) (MEMORY_PAGE_SIZE / slotSize);

        SpinLock_lock(MemoryArena_lockMut(a));

        PageMeta *pm = MemoryArena_slabHead(a, (bytes) sIdx);

        while (pm && !PageMeta_freeHead(pm) && PageMeta_nextSlot(pm) >= slotsPerPage)
            pm = PageMeta_arenaNext(pm);

        if (!pm) {
            bytes maxPages = MemoryArena_maxPagesRaw(a);
            bytes usedPages = MemoryArena_usedPagesRaw(a);

            if (maxPages > 0 && usedPages + 1u > maxPages) {
                arena_recordExhaustion(a, numBytes, "arena page budget exhausted");
                SpinLock_unlock(MemoryArena_lockMut(a));
                return nullptr;
            }

            pm = MemoryPool_allocPages(MemoryArena_pool(a), a, 1u, MEMORY_PAGE_SLAB, (u8) sIdx);

            if (!pm) {
                arena_recordExhaustion(a, numBytes, "pool out of memory");
                SpinLock_unlock(MemoryArena_lockMut(a));
                return nullptr;
            }

            PageMeta *head = MemoryArena_slabHead(a, (bytes) sIdx);
            list_push(&head, pm);
            MemoryArena_setSlabHead(a, (bytes) sIdx, head);
            MemoryArena_setUsedPages(a, usedPages + 1u);
        }

        u8 *slotPtr = nullptr;
        MemoryFreeNode *freeNode = PageMeta_freeHead(pm);

        if (freeNode) {
            PageMeta_setFreeHead(pm, MemoryFreeNode_next(freeNode));
            slotPtr = (u8*) freeNode - sizeof(MemoryHeader);
        } else {
            u16 nextSlot = PageMeta_nextSlot(pm);
            slotPtr = MemoryPool_pageBase(pm) + (bytes) nextSlot * slotSize;
            PageMeta_setNextSlot(pm, (u16) (nextSlot + 1u));
        }

        PageMeta_setLiveSlots(pm, (u16) (PageMeta_liveSlots(pm) + 1u));
        MemoryArena_setActiveBytes(a, MemoryArena_activeBytesRaw(a) + slotSize);

        SpinLock_unlock(MemoryArena_lockMut(a));

        MemoryHeader *h = (MemoryHeader*) slotPtr;
        Memory_headerStamp(h, typeId, numBytes);

        return (any) (slotPtr + sizeof(MemoryHeader));
    }

    bytes alignedLen = (numBytes + MEMORY_ALIGN_MASK) & ~(bytes) MEMORY_ALIGN_MASK;
    bytes total = sizeof(MemoryHeader) + alignedLen;
    bytes pagesNeeded = (total + MEMORY_PAGE_SIZE - 1u) >> MEMORY_PAGE_SHIFT;

    SpinLock_lock(MemoryArena_lockMut(a));

    bytes maxPages = MemoryArena_maxPagesRaw(a);
    bytes usedPages = MemoryArena_usedPagesRaw(a);

    if (maxPages > 0 && (pagesNeeded > maxPages || usedPages > maxPages - pagesNeeded)) {
        arena_recordExhaustion(a, numBytes, "arena page budget exhausted");
        SpinLock_unlock(MemoryArena_lockMut(a));
        return nullptr;
    }

    PageMeta *pm = MemoryPool_allocPages(MemoryArena_pool(a), a, pagesNeeded, MEMORY_PAGE_SPAN_HEAD, 0);

    if (!pm) {
        arena_recordExhaustion(a, numBytes, "pool out of memory");
        SpinLock_unlock(MemoryArena_lockMut(a));
        return nullptr;
    }

    PageMeta *spanHead = MemoryArena_spanHead(a);
    list_push(&spanHead, pm);
    MemoryArena_setSpanHead(a, spanHead);
    MemoryArena_setUsedPages(a, usedPages + pagesNeeded);
    MemoryArena_setActiveBytes(a, MemoryArena_activeBytesRaw(a) + total);

    SpinLock_unlock(MemoryArena_lockMut(a));

    u8 *base = MemoryPool_pageBase(pm);
    MemoryHeader *h = (MemoryHeader*) base;
    Memory_headerStamp(h, typeId, numBytes);

    return (any) (base + sizeof(MemoryHeader));
}

/** Validate an in-arena payload header and recycle its slab slot or return its pages to the pool. */
static bool arena_free(MemoryArena *a, any userPtr)
{
    if (!a || !MemoryArena_live(a) || !userPtr)
        return false;

    if (userPtr == FREED)
        return Memory_reportFault(
            "double-free on FREED sentinel",
            userPtr,
            a,
            "this handle already holds FREED -- clear pointers to nullptr on release and fix the duplicate owner, do not silence free"
        );

    pointer u = (pointer) userPtr;

    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0)
        return Memory_reportFault(
            "misaligned or interior pointer on MemoryArena_free",
            userPtr,
            a,
            "only pass the exact 32-byte aligned base pointer returned by MemoryArena_alloc -- do not pass interior or unaligned offsets"
        );

    PageMeta *pm = MemoryPool_lookupPage(MemoryArena_pool(a), userPtr);

    if (!pm || PageMeta_owner(pm) != a) {
        if (Memory_isFreed(userPtr))
            return Memory_reportFault(
                "double-free",
                userPtr,
                a,
                "block was already freed and its page returned -- fix the duplicate owner, do not silence free"
            );

        return Memory_reportFault(
            "foreign or wrong-arena pointer on MemoryArena_free",
            userPtr,
            a,
            "pointer does not belong to this MemoryArena -- free it through its owning arena or Memory_free"
        );
    }

    SpinLock_lock(MemoryArena_lockMut(a));

    if (PageMeta_owner(pm) != a) {
        SpinLock_unlock(MemoryArena_lockMut(a));
        return Memory_reportFault(
            "double-free race (page released concurrently)",
            userPtr,
            a,
            "another thread freed this block and released its page -- synchronize teardown so one owner frees each block once"
        );
    }

    u8 kind = PageMeta_kind(pm);
    u8 *pBase = MemoryPool_pageBase(pm);
    MemoryHeader *h = (MemoryHeader*) ((u8*) userPtr - sizeof(MemoryHeader));

    if (header_isFreed(h)) {
        SpinLock_unlock(MemoryArena_lockMut(a));
        return Memory_reportFault(
            "double-free",
            userPtr,
            a,
            "block header is already stamped FREED -- remove the duplicate free and keep a single owner, do not silence free"
        );
    }

    if (!header_valid(h)) {
        SpinLock_unlock(MemoryArena_lockMut(a));
        return Memory_reportFault(
            "corrupted or invalid header on MemoryArena_free",
            userPtr,
            a,
            "32-byte block header hash failed -- check for buffer underrun, interior pointer, or memory corruption"
        );
    }

    bytes origLen = MemoryHeader_length(h);

    if (kind == MEMORY_PAGE_SLAB) {
        u8 cls = PageMeta_slabClass(pm);

        if (cls >= SLAB_COUNT) {
            SpinLock_unlock(MemoryArena_lockMut(a));
            return false;
        }

        u32 slotSize = s_slabSizes[cls];
        bytes offset = (bytes) ((u8*) userPtr - pBase);

        if (offset % slotSize != sizeof(MemoryHeader) || origLen > slotSize - sizeof(MemoryHeader)) {
            SpinLock_unlock(MemoryArena_lockMut(a));
            return false;
        }

        Memory_headerStampFreed(h);

        MemoryFreeNode *node = (MemoryFreeNode*) userPtr;
        MemoryFreeNode_setNext(node, PageMeta_freeHead(pm));
        PageMeta_setFreeHead(pm, node);

        u16 live = PageMeta_liveSlots(pm);
        u16 nextLive = live > 0 ? (u16) (live - 1u) : 0;
        PageMeta_setLiveSlots(pm, nextLive);

        bytes active = MemoryArena_activeBytesRaw(a);
        MemoryArena_setActiveBytes(a, active >= slotSize ? active - slotSize : 0);

        bool releasePage = (nextLive == 0);

        if (releasePage) {
            PageMeta *head = MemoryArena_slabHead(a, cls);
            list_remove(&head, pm);
            MemoryArena_setSlabHead(a, cls, head);
            bytes used = MemoryArena_usedPagesRaw(a);
            MemoryArena_setUsedPages(a, used > 0 ? used - 1u : 0);
        }

        SpinLock_unlock(MemoryArena_lockMut(a));

        if (releasePage)
            MemoryPool_freePages(MemoryArena_pool(a), pm);

        return true;
    }

    if (kind == MEMORY_PAGE_SPAN_HEAD) {
        if ((u8*) userPtr != pBase + sizeof(MemoryHeader)) {
            SpinLock_unlock(MemoryArena_lockMut(a));
            return false;
        }

        bytes alignedLen = (origLen + MEMORY_ALIGN_MASK) & ~(bytes) MEMORY_ALIGN_MASK;
        bytes total = sizeof(MemoryHeader) + alignedLen;

        Memory_headerStampFreed(h);

        PageMeta *spanHead = MemoryArena_spanHead(a);
        list_remove(&spanHead, pm);
        MemoryArena_setSpanHead(a, spanHead);

        bytes active = MemoryArena_activeBytesRaw(a);
        MemoryArena_setActiveBytes(a, active >= total ? active - total : 0);

        bytes spanPages = PageMeta_spanPages(pm);
        bytes used = MemoryArena_usedPagesRaw(a);
        MemoryArena_setUsedPages(a, used >= spanPages ? used - spanPages : 0);

        SpinLock_unlock(MemoryArena_lockMut(a));

        MemoryPool_freePages(MemoryArena_pool(a), pm);
        return true;
    }

    SpinLock_unlock(MemoryArena_lockMut(a));
    return false;
}

/** Return all pages owned by arena back to its pool, resetting active bytes and exhaustion state. */
static fn arena_freeAll(MemoryArena *a) {
    if (!a || !MemoryArena_live(a))
        return;

    MemoryPool *pool = MemoryArena_pool(a);

    SpinLock_lock(MemoryArena_lockMut(a));

    PageMeta *slabLists[SLAB_COUNT];

    for (bytes s = 0; s < SLAB_COUNT; s++) {
        slabLists[s] = MemoryArena_slabHead(a, s);
        MemoryArena_setSlabHead(a, s, nullptr);
    }

    PageMeta *spans = MemoryArena_spanHead(a);
    MemoryArena_setSpanHead(a, nullptr);

    MemoryArena_setUsedPages(a, 0);
    MemoryArena_setActiveBytes(a, 0);
    MemoryArena_setExhaustionCount(a, 0);
    MemoryArena_setExhaustionReported(a, false);

    SpinLock_unlock(MemoryArena_lockMut(a));

    for (bytes s = 0; s < SLAB_COUNT; s++) {
        u32 slotSize = s_slabSizes[s];
        PageMeta *pm = slabLists[s];

        while (pm) {
            PageMeta *next = PageMeta_arenaNext(pm);
            u8 *base = MemoryPool_pageBase(pm);
            u16 limit = PageMeta_nextSlot(pm);

            if (base) {
                for (u16 i = 0; i < limit; i++) {
                    MemoryHeader *h = (MemoryHeader*) (base + (bytes) i * slotSize);
                    Memory_headerStampFreed(h);
                }
            }

            MemoryPool_freePages(pool, pm);
            pm = next;
        }
    }

    while (spans) {
        PageMeta *next = PageMeta_arenaNext(spans);
        u8 *base = MemoryPool_pageBase(spans);

        if (base)
            Memory_headerStampFreed((MemoryHeader*) base);

        MemoryPool_freePages(pool, spans);
        spans = next;
    }
}

/** Return whether userPtr is the FREED sentinel or points to a payload header stamped FREED. */
bool Memory_isFreed(const_any userPtr)
{
    if (!userPtr)
        return false;

    if (userPtr == FREED)
        return true;

    pointer u = (pointer) userPtr;

    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0)
        return false;

    if (!MemoryPool_containsPayload(MemoryPool_default(), userPtr))
        return false;

    const MemoryHeader *h = (const MemoryHeader*) ((const u8*) userPtr - sizeof(MemoryHeader));

    return header_isFreed(h);
}

/** Find the live arena owning a validated payload via O(log chunks) pool lookup. */
MemoryArena *Memory_arenaFor(any userPtr) {
    if (!userPtr)
        return nullptr;

    pointer u = (pointer) userPtr;

    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0)
        return nullptr;

    PageMeta *pm = MemoryPool_lookupPage(MemoryPool_default(), userPtr);

    if (!pm)
        return nullptr;

    MemoryArena *owner = PageMeta_owner(pm);

    if (!owner || owner == &s_arenaTable || !MemoryArena_live(owner))
        return nullptr;

    MemoryHeader *h = (MemoryHeader*) ((u8*) userPtr - sizeof(MemoryHeader));

    if (!header_valid(h))
        return nullptr;

    return owner;
}

/** Resolve a validated header only after checking transient scratch and the pool's sorted chunks. */
const MemoryHeader *Memory_safeHeader(const_any userPtr) {
    if (!userPtr)
        return nullptr;

    if (userPtr == FREED) {
        THROW("memory: use-after-free on FREED sentinel %p", userPtr);
        return nullptr;
    }

    pointer u = (pointer) userPtr;

    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0)
        return nullptr;

    if (Transient_contains(userPtr)) {
        const MemoryHeader *h = (const MemoryHeader*) ((const u8*) userPtr - sizeof(MemoryHeader));

        if (header_valid(h))
            return h;
    }

    PageMeta *pm = MemoryPool_lookupPage(MemoryPool_default(), userPtr);

    if (!pm) {
        if (Memory_isFreed(userPtr))
            THROW("memory: use-after-free at %p", userPtr);

        return nullptr;
    }

    MemoryArena *owner = PageMeta_owner(pm);

    if (!owner || !MemoryArena_live(owner)) {
        if (Memory_isFreed(userPtr))
            THROW("memory: use-after-free at %p", userPtr);

        return nullptr;
    }

    const MemoryHeader *h = (const MemoryHeader*) ((const u8*) userPtr - sizeof(MemoryHeader));

    if (header_isFreed(h)) {
        THROW("memory: use-after-free at %p", userPtr);
        return nullptr;
    }

    if (!header_valid(h))
        return nullptr;

    return h;
}

/** Lazily initialize the default arena with its named default byte budget. */
static fn ensure_initialized() {
    if (!MemoryArena_live(&s_default))
        Memory_init(ANTI_ARENA_DEFAULT_SIZE);
}

/** Initialize the default arena; repeated calls succeed without reinitializing it. */
bool Memory_init(bytes totalBytes) {
    SpinLock_lock(MemoryArena_initLockMut(&s_default));

    if (MemoryArena_live(&s_default)) {
        SpinLock_unlock(MemoryArena_initLockMut(&s_default));
        return true;
    }

    bytes cap = totalBytes > 0 ? totalBytes : ANTI_ARENA_DEFAULT_SIZE;
    bytes maxPages = (cap + MEMORY_PAGE_SIZE - 1u) >> MEMORY_PAGE_SHIFT;
    bool ok = arena_initPages(&s_default, MemoryPool_default(), maxPages);

    SpinLock_unlock(MemoryArena_initLockMut(&s_default));

    return ok;
}

/** Return Relational Engine's default arena, initializing it on first access. */
MemoryArena *Memory_defaultArena() {
    ensure_initialized();

    return &s_default;
}

/** Reset all allocations in the default arena, returning its pages to the pool. */
fn Memory_freeAll() {
    arena_freeAll(&s_default);
}

/** Enumerate live blocks in the default arena matching typeId. */
bytes Memory_findAll(u64 typeId, any *outArray, bytes maxCount) {
    return MemoryArena_findAll(&s_default, typeId, outArray, maxCount);
}

/** Return the page-rounded byte capacity an arena needs to serve a single payload of payloadBytes. */
bytes MemoryArena_capacityFor(bytes payloadBytes) {
    if (payloadBytes > SIZE_MAX - sizeof(MemoryHeader) - MEMORY_PAGE_SIZE)
        return 0;

    bytes aligned = (payloadBytes + MEMORY_ALIGN_MASK) & ~(bytes) MEMORY_ALIGN_MASK;
    bytes total = sizeof(MemoryHeader) + aligned;
    bytes pages = (total + MEMORY_PAGE_SIZE - 1u) >> MEMORY_PAGE_SHIFT;

    return pages << MEMORY_PAGE_SHIFT;
}

/** Lazily initialize the internal descriptor arena that allocates MemoryArena instances. */
static fn ensure_arenaTable_initialized()
{
    if (!MemoryArena_live(&s_arenaTable)) {
        SpinLock_lock(MemoryArena_initLockMut(&s_arenaTable));

        if (!MemoryArena_live(&s_arenaTable))
            (discard) arena_initPages(&s_arenaTable, MemoryPool_default(), 0);

        SpinLock_unlock(MemoryArena_initLockMut(&s_arenaTable));
    }
}

;;CONSTRUCTOR
/**
 * Creates a lightweight arena backed by the default MemoryPool with an explicit
 * page cap (0 selects unbounded growth).
 * @param maxPages maximum pages the arena may claim (0 = unbounded)
 * @return newly allocated MemoryArena, or nullptr on OOM
 */
MemoryArena *MemoryArena_pages(bytes maxPages)
{
    ensure_arenaTable_initialized();

    MemoryArena *a = (MemoryArena*) arena_alloc(&s_arenaTable, 0, sizeof(MemoryArena));

    if (!a)
        return nullptr;

    memset(a, 0, sizeof(MemoryArena));

    if (!arena_initPages(a, MemoryPool_default(), maxPages)) {
        arena_free(&s_arenaTable, a);
        return nullptr;
    }

    return a;
}

;;CONSTRUCTOR
/**
 * Creates an unbounded arena backed by the default MemoryPool.
 * @return newly allocated MemoryArena, or nullptr on OOM
 */
MemoryArena *MemoryArena_0()
{
    return MemoryArena_pages(0);
}

;;CONSTRUCTOR
/**
 * Creates an arena with a byte budget rounded up to MEMORY_PAGE_SIZE pages.
 * @param totalBytes byte budget (0 selects unbounded growth)
 * @return newly allocated MemoryArena, or nullptr on overflow or OOM
 */
MemoryArena *MemoryArena_1(bytes totalBytes)
{
    if (totalBytes == 0)
        return MemoryArena_pages(0);

    if (totalBytes > SIZE_MAX - MEMORY_PAGE_SIZE)
        return nullptr;

    bytes pages = (totalBytes + MEMORY_PAGE_SIZE - 1u) >> MEMORY_PAGE_SHIFT;

    return MemoryArena_pages(pages);
}

/** Return all owned pages to the pool and destroy a non-default arena. */
fn MemoryArena_destroy(MemoryArena *a) {
    if (!a || a == &s_default || a == &s_arenaTable)
        return;

    arena_freeAll(a);
    MemoryArena_setLive(a, false);
    arena_free(&s_arenaTable, a);
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
bool MemoryArena_free(MemoryArena *a, any userPtr) {
    if (!a)
        return false;

    return arena_free(a, userPtr);
}

/** Reset every slab and span allocation in the specified arena, returning pages to the pool. */
fn MemoryArena_freeAll(MemoryArena *a) {
    if (!a)
        return;

    arena_freeAll(a);
}

/** Enumerate live slab and span blocks owned by arena; total matches may exceed output capacity. */
bytes MemoryArena_findAll(MemoryArena *a, u64 typeId, any *outArray, bytes maxCount) {
    if (!a || !MemoryArena_live(a))
        return 0;

    bytes count = 0;

    SpinLock_lock(MemoryArena_lockMut(a));

    for (bytes s = 0; s < SLAB_COUNT; s++) {
        u32 slotSize = s_slabSizes[s];
        PageMeta *pm = MemoryArena_slabHead(a, s);

        while (pm) {
            u8 *base = MemoryPool_pageBase(pm);
            u16 limit = PageMeta_nextSlot(pm);

            for (u16 i = 0; i < limit; i++) {
                u8 *slotPtr = base + (bytes) i * slotSize;
                MemoryHeader *h = (MemoryHeader*) slotPtr;

                if (header_valid(h) && (typeId == 0 || MemoryHeader_typeId(h) == typeId)) {
                    if (outArray && count < maxCount)
                        outArray[count] = (any) (slotPtr + sizeof(MemoryHeader));

                    count++;
                }
            }

            pm = PageMeta_arenaNext(pm);
        }
    }

    PageMeta *span = MemoryArena_spanHead(a);

    while (span) {
        u8 *base = MemoryPool_pageBase(span);
        MemoryHeader *h = (MemoryHeader*) base;

        if (header_valid(h) && (typeId == 0 || MemoryHeader_typeId(h) == typeId)) {
            if (outArray && count < maxCount)
                outArray[count] = (any) (base + sizeof(MemoryHeader));

            count++;
        }

        span = PageMeta_arenaNext(span);
    }

    SpinLock_unlock(MemoryArena_lockMut(a));

    return count;
}

/** Return occupied slab and span bytes currently live in arena. */
bytes MemoryArena_activeBytes(MemoryArena *a) {
    if (!a || !MemoryArena_live(a))
        return 0;

    SpinLock_lock(MemoryArena_lockMut(a));
    bytes active = MemoryArena_activeBytesRaw(a);
    SpinLock_unlock(MemoryArena_lockMut(a));

    return active;
}

/** Return the arena's byte budget (or currently claimed bytes when unbounded). */
bytes MemoryArena_capacity(MemoryArena *a) {
    if (!a || !MemoryArena_live(a))
        return 0;

    bytes maxPages = MemoryArena_maxPagesRaw(a);

    if (maxPages > 0)
        return maxPages << MEMORY_PAGE_SHIFT;

    return MemoryArena_usedPagesRaw(a) << MEMORY_PAGE_SHIFT;
}

/** Return the number of pool pages currently held by arena. */
bytes MemoryArena_usedPages(const MemoryArena *a) {
    return (a && MemoryArena_live(a)) ? MemoryArena_usedPagesRaw(a) : 0;
}

/** Return the arena's page ceiling (0 means unbounded growth until OS OOM). */
bytes MemoryArena_maxPages(const MemoryArena *a) {
    return (a && MemoryArena_live(a)) ? MemoryArena_maxPagesRaw(a) : 0;
}

/** Return the number of rejected requests since the arena's last init/freeAll. */
u64 MemoryArena_exhaustionCount(const MemoryArena *a) {
    return a ? MemoryArena_exhaustionCountRaw(a) : 0;
}

/** Return the default arena's rejected-request count since the last freeAll. */
u64 Memory_exhaustionCount() {
    return MemoryArena_live(&s_default) ? MemoryArena_exhaustionCountRaw(&s_default) : 0;
}
