#define NIO_MEM_IMPLEMENTATION

#include "vex.h"
#include "nio/mem.h"

#if defined(VEX_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <sys/mman.h>
#if !defined(MAP_ANON) && defined(MAP_ANONYMOUS)
#define MAP_ANON MAP_ANONYMOUS
#endif
#endif

#include "atomic/atomic.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: MemoryPool
 * ============================================================================
 * Tier 1 of the relational memory architecture (Pool  to  Arena  to  any).
 * Owns growable OS virtual-memory super-chunks (1 MiB default chunks -- 64 x
 * 16 KiB pages on Apple Silicon -- via mmap / VirtualAlloc, growing
 * indefinitely until OS OOM with no hardcoded limit), hands out
 * architecture-native pages (16 KiB on Apple Silicon, 4 KiB elsewhere) and
 * contiguous multi-page spans to MemoryArena owners, and advises the OS
 * (madvise MADV_FREE_REUSABLE / MADV_FREE / MADV_DONTNEED / MEM_RESET) when
 * freed pages return so physical RAM footprint drops back down.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: MemoryPool (nio/pool)
 * ============================================================================
 * Growable OS page pool shared across all MemoryArena owners in the process.
 * Maintains a sorted super-chunk directory for O(log chunks) address-to-page
 * lookup regardless of how many arenas are live.
 * ============================================================================
 */

#define MEMORY_POOL_CHUNK_BYTES (1024u * 1024u)

struct PoolChunk {
    u8 *base;
    bytes byteCount;
    bytes pageCount;
    bytes freePages;
    bytes searchCursor;
    PageMeta *meta;
    bytes metaBytes;
};

struct MemoryPool {
    PoolChunk **chunks;
    bytes chunkCount;
    bytes chunkCap;
    bytes totalPages;
    bytes usedPages;
    SpinLock lock;
};

static MemoryPool s_pool = { 0 };

;;GETTER
static inline u8 *PoolChunk_base(const PoolChunk *c)
{
    return (*c).base;
}
;;SETTER
static inline fn PoolChunk_setBase(PoolChunk *c, u8 *b)
{
    (*c).base = b;
}
;;GETTER
static inline bytes PoolChunk_byteCount(const PoolChunk *c)
{
    return (*c).byteCount;
}
;;SETTER
static inline fn PoolChunk_setByteCount(PoolChunk *c, bytes n)
{
    (*c).byteCount = n;
}
;;GETTER
static inline bytes PoolChunk_pageCount(const PoolChunk *c)
{
    return (*c).pageCount;
}
;;SETTER
static inline fn PoolChunk_setPageCount(PoolChunk *c, bytes n)
{
    (*c).pageCount = n;
}
;;GETTER
static inline bytes PoolChunk_freePages(const PoolChunk *c)
{
    return (*c).freePages;
}
;;SETTER
static inline fn PoolChunk_setFreePages(PoolChunk *c, bytes n)
{
    (*c).freePages = n;
}
;;GETTER
static inline bytes PoolChunk_searchCursor(const PoolChunk *c)
{
    return (*c).searchCursor;
}
;;SETTER
static inline fn PoolChunk_setSearchCursor(PoolChunk *c, bytes n)
{
    (*c).searchCursor = n;
}
;;GETTER
static inline PageMeta *PoolChunk_meta(const PoolChunk *c)
{
    return (*c).meta;
}
;;SETTER
static inline fn PoolChunk_setMeta(PoolChunk *c, PageMeta *m)
{
    (*c).meta = m;
}
;;GETTER
static inline bytes PoolChunk_metaBytes(const PoolChunk *c)
{
    return (*c).metaBytes;
}
;;SETTER
static inline fn PoolChunk_setMetaBytes(PoolChunk *c, bytes n)
{
    (*c).metaBytes = n;
}

;;GETTER
static inline PoolChunk **MemoryPool_chunks(const MemoryPool *p)
{
    return (*p).chunks;
}
;;SETTER
static inline fn MemoryPool_setChunks(MemoryPool *p, PoolChunk **cs)
{
    (*p).chunks = cs;
}
;;GETTER
static inline bytes MemoryPool_chunkCountRaw(const MemoryPool *p)
{
    return (*p).chunkCount;
}
;;SETTER
static inline fn MemoryPool_setChunkCount(MemoryPool *p, bytes n)
{
    (*p).chunkCount = n;
}
;;GETTER
static inline bytes MemoryPool_chunkCap(const MemoryPool *p)
{
    return (*p).chunkCap;
}
;;SETTER
static inline fn MemoryPool_setChunkCap(MemoryPool *p, bytes n)
{
    (*p).chunkCap = n;
}
;;GETTER
static inline bytes MemoryPool_totalPagesRaw(const MemoryPool *p)
{
    return (*p).totalPages;
}
;;SETTER
static inline fn MemoryPool_setTotalPages(MemoryPool *p, bytes n)
{
    (*p).totalPages = n;
}
;;GETTER
static inline bytes MemoryPool_usedPagesRaw(const MemoryPool *p)
{
    return (*p).usedPages;
}
;;SETTER
static inline fn MemoryPool_setUsedPages(MemoryPool *p, bytes n)
{
    (*p).usedPages = n;
}
;;GETTER
static inline SpinLock *MemoryPool_lockMut(MemoryPool *p)
{
    return &(*p).lock;
}

/** Map anonymous zero-initialized OS virtual memory pages. */
static u8 *os_map(bytes size) {
    if (size == 0)
        return nullptr;

#if defined(VEX_PLATFORM_WINDOWS)
    any p = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    return (u8*) p;
#else
    any p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

    if (p == MAP_FAILED)
        return nullptr;

    return (u8*) p;
#endif
}

/** Unmap an OS virtual memory region previously returned by os_map. */
static fn os_unmap(u8 *base, bytes size) {
    if (!base || size == 0)
        return;

#if defined(VEX_PLATFORM_WINDOWS)
    (discard) size;
    VirtualFree(base, 0, MEM_RELEASE);
#else
    munmap(base, size);
#endif
}

/** Advise the OS that freed physical pages may be reclaimed to lower RSS. */
static fn os_releasePages(u8 *base, bytes size) {
    if (!base || size == 0)
        return;

#if defined(VEX_PLATFORM_WINDOWS)
    VirtualAlloc(base, size, MEM_RESET, PAGE_READWRITE);
#elif defined(MADV_FREE_REUSABLE)
    madvise(base, size, MADV_FREE_REUSABLE);
#elif defined(MADV_FREE)
    madvise(base, size, MADV_FREE);
#elif defined(MADV_DONTNEED)
    madvise(base, size, MADV_DONTNEED);
#else
    (discard) base;
    (discard) size;
#endif
}

/** Advise the OS that recycled pages are being reused. */
static fn os_reusePages(u8 *base, bytes size) {
    if (!base || size == 0)
        return;

#if defined(MADV_FREE_REUSE)
    madvise(base, size, MADV_FREE_REUSE);
#else
    (discard) base;
    (discard) size;
#endif
}

/** Binary-search the sorted super-chunk list for the chunk containing ptr (caller holds pool lock). */
static PoolChunk *pool_findChunkLocked(const MemoryPool *pool, const_any ptr) {
    bytes lo = 0;
    bytes hi = MemoryPool_chunkCountRaw(pool);
    PoolChunk **chunks = MemoryPool_chunks(pool);
    const u8 *p = (const u8*) ptr;

    while (lo < hi) {
        bytes mid = lo + (hi - lo) / 2u;
        PoolChunk *c = chunks[mid];
        u8 *base = PoolChunk_base(c);

        if (p < base)
            hi = mid;
        else if (p >= base + PoolChunk_byteCount(c))
            lo = mid + 1u;
        else
            return c;
    }

    return nullptr;
}

/** Map a new super-chunk of at least minPages pages and insert it sorted by base address (caller holds pool lock). */
static PoolChunk *pool_addChunkLocked(MemoryPool *pool, bytes minPages) {
    bytes defaultPages = MEMORY_POOL_CHUNK_BYTES >> MEMORY_PAGE_SHIFT;
    bytes pages = minPages > defaultPages ? minPages : defaultPages;

    if (pages > SIZE_MAX >> MEMORY_PAGE_SHIFT)
        return nullptr;

    bytes byteCount = pages << MEMORY_PAGE_SHIFT;
    bytes chunkHeaderBytes = (sizeof(PoolChunk) + MEMORY_ALIGN_MASK) & ~(bytes) MEMORY_ALIGN_MASK;
    bytes metaBytes = ((chunkHeaderBytes + pages * sizeof(PageMeta)) + MEMORY_PAGE_SIZE - 1u) & ~(bytes) (MEMORY_PAGE_SIZE - 1u);

    bytes count = MemoryPool_chunkCountRaw(pool);
    bytes cap = MemoryPool_chunkCap(pool);

    if (count == cap) {
        bytes minCap = MEMORY_PAGE_SIZE / sizeof(PoolChunk*);
        bytes newCap = cap == 0 ? minCap : cap * 2u;
        bytes newDirBytes = ((newCap * sizeof(PoolChunk*)) + MEMORY_PAGE_SIZE - 1u) & ~(bytes) (MEMORY_PAGE_SIZE - 1u);
        newCap = newDirBytes / sizeof(PoolChunk*);
        PoolChunk **oldChunks = MemoryPool_chunks(pool);
        PoolChunk **next = (PoolChunk**) os_map(newDirBytes);

        if (!next)
            return nullptr;

        for (bytes i = 0; i < count; i++)
            next[i] = oldChunks[i];

        if (oldChunks && cap > 0)
            os_unmap((u8*) oldChunks, cap * sizeof(PoolChunk*));

        MemoryPool_setChunks(pool, next);
        MemoryPool_setChunkCap(pool, newCap);
    }

    u8 *base = os_map(byteCount);

    if (!base)
        return nullptr;

    u8 *metaRegion = os_map(metaBytes);

    if (!metaRegion) {
        os_unmap(base, byteCount);
        return nullptr;
    }

    PoolChunk *chunk = (PoolChunk*) metaRegion;
    PageMeta *meta = (PageMeta*) (metaRegion + chunkHeaderBytes);

    PoolChunk_setBase(chunk, base);
    PoolChunk_setByteCount(chunk, byteCount);
    PoolChunk_setPageCount(chunk, pages);
    PoolChunk_setFreePages(chunk, pages);
    PoolChunk_setSearchCursor(chunk, 0);
    PoolChunk_setMeta(chunk, meta);
    PoolChunk_setMetaBytes(chunk, metaBytes);

    PoolChunk **chunks = MemoryPool_chunks(pool);
    bytes pos = count;

    while (pos > 0 && PoolChunk_base(chunks[pos - 1u]) > base) {
        chunks[pos] = chunks[pos - 1u];
        pos--;
    }

    chunks[pos] = chunk;
    MemoryPool_setChunkCount(pool, count + 1u);
    MemoryPool_setTotalPages(pool, MemoryPool_totalPagesRaw(pool) + pages);

    return chunk;
}

/** Find a contiguous run of needed free pages in a single chunk, or return pageCount on miss. */
static bytes chunk_findRun(const PoolChunk *c, bytes needed) {
    bytes total = PoolChunk_pageCount(c);

    if (PoolChunk_freePages(c) < needed || needed > total)
        return total;

    PageMeta *meta = PoolChunk_meta(c);
    bytes start = PoolChunk_searchCursor(c);
    bytes run = 0;
    bytes runStart = start;

    for (bytes i = start; i < total; i++) {
        if (PageMeta_kind(&meta[i]) == MEMORY_PAGE_FREE) {
            if (run == 0)
                runStart = i;

            run++;

            if (run == needed)
                return runStart;
        } else {
            run = 0;
        }
    }

    run = 0;
    runStart = 0;
    bytes limit = start + needed < total ? start + needed : total;

    for (bytes i = 0; i < limit; i++) {
        if (PageMeta_kind(&meta[i]) == MEMORY_PAGE_FREE) {
            if (run == 0)
                runStart = i;

            run++;

            if (run == needed)
                return runStart;
        } else {
            run = 0;
        }
    }

    return total;
}

/** Return the base virtual address of a page described by pm. */
u8 *MemoryPool_pageBase(const PageMeta *pm) {
    if (!pm)
        return nullptr;

    PoolChunk *c = PageMeta_chunk(pm);

    if (!c)
        return nullptr;

    return PoolChunk_base(c) + ((bytes) PageMeta_pageIndex(pm) << MEMORY_PAGE_SHIFT);
}

/** Claim a contiguous run of count pages from pool for owner, growing chunks on demand. */
PageMeta *MemoryPool_allocPages(MemoryPool *pool, MemoryArena *owner, bytes count, u8 kind, u8 slabClass) {
    if (!pool || count == 0 || count > UINT32_MAX)
        return nullptr;

    SpinLock_lock(MemoryPool_lockMut(pool));

    PoolChunk *chosen = nullptr;
    bytes idx = 0;
    bytes chunkCount = MemoryPool_chunkCountRaw(pool);
    PoolChunk **chunks = MemoryPool_chunks(pool);

    for (bytes i = 0; i < chunkCount; i++) {
        bytes found = chunk_findRun(chunks[i], count);

        if (found < PoolChunk_pageCount(chunks[i])) {
            chosen = chunks[i];
            idx = found;
            break;
        }
    }

    if (!chosen) {
        chosen = pool_addChunkLocked(pool, count);

        if (!chosen) {
            SpinLock_unlock(MemoryPool_lockMut(pool));
            return nullptr;
        }

        idx = 0;
    }

    PageMeta *meta = PoolChunk_meta(chosen);
    PageMeta *head = &meta[idx];

    PageMeta_setChunk(head, chosen);
    PageMeta_setOwner(head, owner);
    PageMeta_setFreeHead(head, nullptr);
    PageMeta_setArenaPrev(head, nullptr);
    PageMeta_setArenaNext(head, nullptr);
    PageMeta_setPageIndex(head, (u32) idx);
    PageMeta_setSpanPages(head, (u32) count);
    PageMeta_setLiveSlots(head, 0);
    PageMeta_setNextSlot(head, 0);
    PageMeta_setKind(head, kind);
    PageMeta_setSlabClass(head, slabClass);

    for (bytes j = 1u; j < count; j++) {
        PageMeta *cont = &meta[idx + j];
        PageMeta_setChunk(cont, chosen);
        PageMeta_setOwner(cont, owner);
        PageMeta_setFreeHead(cont, nullptr);
        PageMeta_setArenaPrev(cont, nullptr);
        PageMeta_setArenaNext(cont, nullptr);
        PageMeta_setPageIndex(cont, (u32) (idx + j));
        PageMeta_setSpanPages(cont, 0);
        PageMeta_setLiveSlots(cont, 0);
        PageMeta_setNextSlot(cont, 0);
        PageMeta_setKind(cont, MEMORY_PAGE_SPAN_CONT);
        PageMeta_setSlabClass(cont, 0);
    }

    PoolChunk_setFreePages(chosen, PoolChunk_freePages(chosen) - count);
    bytes nextCursor = idx + count;
    PoolChunk_setSearchCursor(chosen, nextCursor < PoolChunk_pageCount(chosen) ? nextCursor : 0);
    MemoryPool_setUsedPages(pool, MemoryPool_usedPagesRaw(pool) + count);
    os_reusePages(PoolChunk_base(chosen) + (idx << MEMORY_PAGE_SHIFT), count << MEMORY_PAGE_SHIFT);

    SpinLock_unlock(MemoryPool_lockMut(pool));

    return head;
}

/** Remove an empty super-chunk from the sorted directory and unmap it (caller holds pool lock). */
static fn pool_removeChunkLocked(MemoryPool *pool, PoolChunk *c) {
    bytes chunkCount = MemoryPool_chunkCountRaw(pool);
    PoolChunk **chunks = MemoryPool_chunks(pool);
    bytes pos = chunkCount;

    for (bytes i = 0; i < chunkCount; i++) {
        if (chunks[i] == c) {
            pos = i;
            break;
        }
    }

    if (pos == chunkCount)
        return;

    for (bytes i = pos + 1u; i < chunkCount; i++)
        chunks[i - 1u] = chunks[i];

    MemoryPool_setChunkCount(pool, chunkCount - 1u);

    bytes total = MemoryPool_totalPagesRaw(pool);
    bytes chunkPages = PoolChunk_pageCount(c);
    MemoryPool_setTotalPages(pool, total >= chunkPages ? total - chunkPages : 0);

    u8 *chunkBase = PoolChunk_base(c);
    bytes chunkByteCount = PoolChunk_byteCount(c);
    bytes chunkMetaBytes = PoolChunk_metaBytes(c);
    os_unmap(chunkBase, chunkByteCount);
    os_unmap((u8*) c, chunkMetaBytes);
}

/** Return a page or multi-page span to the pool, advising OS reclamation and unmapping empty extra chunks. */
fn MemoryPool_freePages(MemoryPool *pool, PageMeta *head) {
    if (!pool || !head)
        return;

    SpinLock_lock(MemoryPool_lockMut(pool));

    PoolChunk *c = PageMeta_chunk(head);
    bytes idx = PageMeta_pageIndex(head);
    bytes count = PageMeta_spanPages(head);

    if (c && count > 0 && idx + count <= PoolChunk_pageCount(c)) {
        PageMeta *meta = PoolChunk_meta(c);

        for (bytes j = 0; j < count; j++) {
            PageMeta *pm = &meta[idx + j];
            PageMeta_setOwner(pm, nullptr);
            PageMeta_setFreeHead(pm, nullptr);
            PageMeta_setArenaPrev(pm, nullptr);
            PageMeta_setArenaNext(pm, nullptr);
            PageMeta_setSpanPages(pm, 0);
            PageMeta_setLiveSlots(pm, 0);
            PageMeta_setNextSlot(pm, 0);
            PageMeta_setKind(pm, MEMORY_PAGE_FREE);
            PageMeta_setSlabClass(pm, 0);
        }

        bytes freeAfter = PoolChunk_freePages(c) + count;
        PoolChunk_setFreePages(c, freeAfter);

        bytes used = MemoryPool_usedPagesRaw(pool);
        MemoryPool_setUsedPages(pool, used >= count ? used - count : 0);

        if (freeAfter == PoolChunk_pageCount(c) && MemoryPool_chunkCountRaw(pool) > 1u) {
            pool_removeChunkLocked(pool, c);
        } else {
            u8 *addr = PoolChunk_base(c) + (idx << MEMORY_PAGE_SHIFT);
            os_releasePages(addr, count << MEMORY_PAGE_SHIFT);

            if (idx < PoolChunk_searchCursor(c))
                PoolChunk_setSearchCursor(c, idx);
        }
    }

    SpinLock_unlock(MemoryPool_lockMut(pool));
}

/** Resolve ptr to its head PageMeta under the pool lock, or nullptr if unowned. */
PageMeta *MemoryPool_lookupPage(MemoryPool *pool, const_any ptr) {
    if (!pool || !ptr)
        return nullptr;

    SpinLock_lock(MemoryPool_lockMut(pool));

    PoolChunk *c = pool_findChunkLocked(pool, ptr);
    PageMeta *result = nullptr;

    if (c) {
        bytes offset = (bytes) ((const u8*) ptr - PoolChunk_base(c));
        bytes idx = offset >> MEMORY_PAGE_SHIFT;

        if (idx < PoolChunk_pageCount(c)) {
            PageMeta *pm = &PoolChunk_meta(c)[idx];
            u8 kind = PageMeta_kind(pm);

            if (kind == MEMORY_PAGE_SLAB || kind == MEMORY_PAGE_SPAN_HEAD)
                result = pm;
        }
    }

    SpinLock_unlock(MemoryPool_lockMut(pool));

    return result;
}

/** Return whether ptr and its preceding 32-byte MemoryHeader lie inside a mapped pool chunk. */
bool MemoryPool_containsPayload(MemoryPool *pool, const_any ptr)
{
    if (!pool || !ptr)
        return false;

    SpinLock_lock(MemoryPool_lockMut(pool));

    PoolChunk *c = pool_findChunkLocked(pool, ptr);
    bool inside = false;

    if (c) {
        const u8 *p = (const u8*) ptr;
        const u8 *base = PoolChunk_base(c);

        if (p >= base + sizeof(MemoryHeader) && p < base + PoolChunk_byteCount(c))
            inside = true;
    }

    SpinLock_unlock(MemoryPool_lockMut(pool));

    return inside;
}

/** Return the process-wide default MemoryPool owned by Relational Engine. */
MemoryPool *MemoryPool_default() {
    return &s_pool;
}

/** Return the architecture-native page quantum used by MemoryPool. */
bytes MemoryPool_pageSize() {
    return MEMORY_PAGE_SIZE;
}

/** Return the number of OS super-chunks currently mapped by pool. */
bytes MemoryPool_chunkCount(const MemoryPool *pool) {
    return pool ? MemoryPool_chunkCountRaw(pool) : 0;
}

/** Return the total virtual pages managed across all chunks in pool. */
bytes MemoryPool_totalPages(const MemoryPool *pool) {
    return pool ? MemoryPool_totalPagesRaw(pool) : 0;
}

/** Return the number of pages currently claimed by live arenas in pool. */
bytes MemoryPool_usedPages(const MemoryPool *pool) {
    return pool ? MemoryPool_usedPagesRaw(pool) : 0;
}
