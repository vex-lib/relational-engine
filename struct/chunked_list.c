// struct/chunked_list.c — never-moved radix-paged list implementation.
//
// A growable copy-on-write root, any number of fixed-radix internal levels, and
// a page-sized leaf. Every hop is shift/mask; a leaf is a contiguous run of
// rows. The 2-level byte-budget form is the degenerate case (root -> leaf).

#include "vex.h"
#include "atomic/atomic.h"

#include "struct/chunked_list.h"
#include "struct/exhaustion.h"
#include "nio/mem.h"
#include "struct/stride.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ChunkedList
 * ============================================================================
 * Never-moved radix-paged list: rows live in stable leaves reached through a
 * page table (a growable COW root, fixed-radix internal nodes, a page-sized
 * leaf), so a row address handed out once stays valid until free — the sibling
 * struct/List grows one contiguous buffer and therefore moves every element on
 * growth. Radices are powers of two, so every hop is shift/mask with no divide.
 *
 * The root is copy-on-write (old generations stay mapped and valid for racing
 * readers); internal nodes and leaves are published with an acquire/release
 * handoff and never move; the gate serializes claim-plus-growth-plus-commit with
 * a bounded 100ms try-lock per the Bounded Wait Law. Levels default to 2 (root
 * -> leaf) for the byte-budget constructors; the radix form builds any depth.
 * Lives at R2 as a leaf container behavior.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ChunkedList (struct/chunked_list.c)
 * ============================================================================
 * Never-moved radix-paged list: rows live in stable leaves, so a row address
 * handed out once stays valid until free.
 *
 * Concurrency: addSlot and reserve are safe from any number of writers — the
 * gate serializes the whole claim-plus-growth-plus-commit step, so every row is
 * handed out exactly once. slot, getChunk, the count getters and packInto are
 * lock-free and safe concurrently with writers: the committed count is stored
 * only after the row's leaf is published, so index < committed implies the row
 * resolves. setChunkBytes is setup-time only and free requires quiescence.
 *
 * Storage: radices[d] is the fan-out of a node at depth d (powers of two), and
 * the leaf radix is radices[levels-1] = rowsPerChunk. shifts[d] is the bit
 * offset of depth d's digit; masks[d] = radices[d]-1 (masks[0] is unused — the
 * root is growable). Lookup is digit = (index >> shifts[d]) & masks[d] with no
 * divide. The root generation is copy-on-write; internal nodes and leaves are
 * atomic pointers published only after initialization.
 *
 * STRUCT FIELDS (Mirroring struct/chunked_list.h):
 * ----------------------------------------------------------------------------
 *   ChunkedList {
 *     Collection collection;     // gate-maintained mirror; quiesced reads only under concurrency
 *     _Atomic(any) directory;  // current root generation (COW; generations never freed mid-run)
 *     _Atomic u32 chunkCount; // published leaves (lock-free reader bound)
 *     _Atomic u32 committed;  // published rows (lock-free reader bound)
 *     u32 rowsPerChunk;     // rows per leaf (power of two, >= 1; geometry)
 *     u32 rowShift;         // log2(rowsPerChunk)
 *     u32 rowMask;          // rowsPerChunk - 1
 *     u32 chunkBytes;       // byte budget of one leaf (>= 16)
 *     u32 levels;           // pointer hops root..leaf (>= 2)
 *     u32 *radices;         // [levels] fan-out per depth; [0] = initial root slots
 *     u32 *shifts;          // [levels] bit offset of each depth's digit
 *     u32 *masks;           // [levels] radix-1 per depth
 *     SpinLock lock;             // claim + growth + commit; try-locked, 100ms bounded
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure data + growth math only):
 * ----------------------------------------------------------------------------
 *   ChunkDir {                // one root generation (COW snapshot unit)
 *     struct ChunkDir *prev;  // previous generation, freed at teardown
 *     u32 capacity;      // depth-1 slots in THIS generation
 *     u32 pad;           // explicit padding so slots[] is 8-byte aligned
 *     _Atomic(u8*) slots[]; // depth-1 objects (nodes or leaves)
 *   }
 *   pow2Rows(stride, chunkBytes)      // largest power-of-two rows for a budget
 *   pow2Floor(n)                      // largest power of two <= n (min 1)
 *   shiftOf(rows)                     // log2 of a power-of-two row count
 *   pageLeafRows(stride)              // default page-sized leaf (pow2 <= 4096/stride)
 *   directoryOf(self)                 // lock-free root generation load
 *   digitOf(self, index, depth)       // shift/mask digit at a depth
 *   leafAtRow(self, index)            // lock-free leaf resolve by row index
 *   rowAt(self, index)                // lock-free stable row resolve
 *   leafLinkOffset(self)              // 8-byte-aligned tail-link offset in a leaf
 *   nodeAlloc(self, depth)            // allocate + zero one node or leaf
 *   dirGrow(self)                     // COW root doubling (gate held, cold)
 *   ensureRootSlotLocked(self, slot)  // grow the root until slot fits (gate held)
 *   chunkAddLocked(self)              // create + publish one leaf (gate held, cold)
 *   ensureChunksLocked(self, need)    // growth gate body (gate held, cold)
 *   ensureChunks(self, need)          // bounded-lock growth gate
 *   buildPaged(elementClass, stride, radices, levels, chunkBytes) // shared builder
 *   freeSubtree(node, self, depth)    // recursive teardown free
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - ChunkedList(elementClass, stride, r0, ..., leafRows) : radix form
 *   - ChunkedList_paged(elementClass, stride, radices, count)
 *   - ChunkedList_1(elementClass)                    : ChunkedList_1(elementClass)
 *   - ChunkedList_2(elementClass, chunkBytes)        : ChunkedList_2(elementClass, chunkBytes)
 *   - ChunkedList_3(elementClass, stride, chunkBytes): 2-level byte-budget form
 *
 * Core Functions:
 *   - ChunkedList_free(self)
 *   - ChunkedList_addSlot(self)
 *   - ChunkedList_reserve(self, rows)
 *   - ChunkedList_packInto(self, dest, destCap, outRows, outTruncated)
 *   - ChunkedList_slot(self, index)
 *   - ChunkedList_getChunk(self, chunkIndex)
 *   - ChunkedList_nextChunk(self, chunk)
 *   - ChunkedList_forEachChunk(self, fn, userdata)
 *
 * Setters:
 *   - ChunkedList_setChunkBytes(self, chunkBytes)
 *
 * Getters:
 *   - ChunkedList_size(self)
 *   - ChunkedList_length(self)
 *   - ChunkedList_capacity(self)
 *   - ChunkedList_isEmpty(self)
 *   - ChunkedList_elementClassId(self)
 *   - ChunkedList_stride(self)
 *   - ChunkedList_getChunkCount(self)
 *   - ChunkedList_getRowsPerChunk(self)
 *   - ChunkedList_getChunkBytes(self)
 *   - ChunkedList_getLevels(self)
 * ============================================================================
 */

// Growth gate bound (the Bounded Wait Law): 100ms is ~6 frames of slack and
// only expires when another thread is wedged, never on a healthy path.
#define VEX_CHUNKED_LOCK_NANOS 100000000ll

// PRIVATE HELPERS

typedef struct ChunkDir {
    struct ChunkDir *prev; // previous generation (freed last at teardown)
    u32 capacity;     // depth-1 slots in THIS generation
    u32 pad;          // explicit padding so slots[] is 8-byte aligned
    _Atomic(u8*) slots[]; // depth-1 objects (internal nodes or leaves)
} ChunkDir;

// Largest power of two rows that fits a byte budget (min 1). The loop test
// divides instead of shifting so it cannot wrap on large budgets.
static u32 pow2Rows(bytes stride, u32 chunkBytes) {
    if (stride == 0u) 
        return 1u;

    u32 rows = (u32)(chunkBytes / stride);

    if (rows == 0u) 
        return 1u;

    u32 p = 1u;

    while (p <= (rows >> 1)) 
        p <<= 1u;

    return p;
}

static u32 shiftOf(u32 rows) {
    if (rows == 0u) 
        return 0u;

    u32 shift = 0;

    while ((1u << shift) < rows) 
        shift++;

    return shift;
}

// Largest power of two <= n (min 1).
static u32 pow2Floor(u32 n) {
    if (n <= 1u) 
        return 1u;

    u32 p = 1u;

    while (p <= (n >> 1)) 
        p <<= 1u;

    return p;
}

// Default radix-form leaf: one page of rows.
static u32 pageLeafRows(bytes stride) {
    if (stride == 0u) 
        stride = 1u;

    return pow2Floor((u32)(VEX_CHUNKED_PAGE_BYTES / stride));
}

static ChunkDir *directoryOf(const ChunkedList *self) {
    return (ChunkDir*) atomic_load_explicit(&(*self).directory, memory_order_acquire);
}

// Shift/mask digit at a depth. Depth 0 (the root) is unmasked: it is growable.
static inline u32 digitOf(const ChunkedList *self, u32 index, u32 depth) {
    u32 v = index >> (*self).shifts[depth];

    return depth == 0u ? v : (v & (*self).masks[depth]);
}

// Lock-free leaf resolve by row index (no offset): null when not published yet.
static u8 *leafAtRow(const ChunkedList *self, u32 index) {
    ChunkDir *dir = directoryOf(self);

    if (!dir) 
        return nullptr;

    u32 d0 = digitOf(self, index, 0u);

    if (d0 >= (*dir).capacity) 
        return nullptr;

    u8 *node = atomic_load_explicit(&(*dir).slots[d0], memory_order_acquire);

    if (!node) 
        return nullptr;

    u32 levels = (*self).levels;

    for (u32 depth = 1u; depth < levels - 1u; depth++) {
        _Atomic(u8*) *children = (_Atomic(u8*) *) node;

        u32 digit = digitOf(self, index, depth);

        node = atomic_load_explicit(&children[digit], memory_order_acquire);

        if (!node) 
            return nullptr;
    }

    return node;
}

// Lock-free stable row resolve: null when the row is not published yet.
// Callers bound index by the committed count first; the committed store lands
// only after the leaf publish, so index < committed implies non-null here.
static u8 *rowAt(const ChunkedList *self, u32 index) {
    u8 *leaf = leafAtRow(self, index);

    if (!leaf) 
        return nullptr;

    const Collection *coll = &(*self).collection;

    bytes stride = (*coll).stride;

    return leaf + (bytes)(index & (*self).rowMask) * stride;
}

// Byte offset of a leaf's tail link: the row Bytes rounded UP to 8 Bytes so the
// link is naturally aligned. The link is written once (when the next leaf is
// created) and read with acquire/release.
static bytes leafLinkOffset(const ChunkedList *self) {
    const Collection *coll = &(*self).collection;

    bytes rowsBytes = (bytes)(*self).rowsPerChunk * (bytes)(*coll).stride;

    return (rowsBytes + (bytes)(VEX_CHUNKED_LINK_BYTES - 1u)) & ~(bytes)(VEX_CHUNKED_LINK_BYTES - 1u);
}

// Allocate + zero one node at `depth`: an internal pointer array, or a leaf of
// rows at the deepest depth (plus the tail link).
static u8 *nodeAlloc(const ChunkedList *self, u32 depth) {
    u32 radix = (*self).radices[depth];

    if (depth == (*self).levels - 1u) {
        bytes Bytes = leafLinkOffset(self) + (bytes)VEX_CHUNKED_LINK_BYTES;

        const Collection *coll = &(*self).collection;

        u64 chunkType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, (*coll).elementClass);

        u8 *leaf = (u8*) Memory_alloc(chunkType, Bytes);

        if (leaf) 
            memset(leaf, 0, Bytes);

        return leaf;
    }

    bytes Bytes = (bytes)radix * sizeof(_Atomic(u8*));

    u8 *node = (u8*) Memory_alloc(TYPE_CHUNKED_LIST, Bytes);

    if (node) 
        memset(node, 0, Bytes);

    return node;
}

// Copy-on-write root doubling: publish a bigger generation, keep the old one
// valid forever (the arraycopy of the cold path — node pointers only). Gate
// held by the caller: exactly one grower, so chunkCount only moves here.
static bool dirGrow(ChunkedList *self) {
    ChunkDir *old = directoryOf(self);

    u32 oldCap = old ? (*old).capacity : 0u;

    if (oldCap > (UINT32_MAX / 2u)) 
        return false;

    u32 newCap = oldCap == 0u ? VEX_CHUNKED_DIR_INIT : oldCap * 2u;

    bytes Bytes = sizeof(ChunkDir) + (bytes)newCap * sizeof(_Atomic(u8*));

    ChunkDir *next = (ChunkDir*) Memory_alloc(TYPE_CHUNKED_LIST, Bytes);

    if (!next) {
        // Loud, counted refusal through the embedded Collection mirror.
        Collection_reportExhaustion(&(*self).collection, Bytes);

        return false;
    }

    (*next).prev = old;

    (*next).capacity = newCap;

    (*next).pad = 0u;

    memset(&(*next).slots[0], 0, (bytes)newCap * sizeof(_Atomic(u8*)));

    u32 have = old ? (*old).capacity : 0u;

    for (u32 i = 0; i < have; i++) 
        atomic_store_explicit(&(*next).slots[i], atomic_load_explicit(&(*old).slots[i], memory_order_relaxed), memory_order_relaxed);

    atomic_store_explicit(&(*self).directory, next, memory_order_release);

    return true;
}

// Grow the root until depth-1 slot `slot` exists. Gate held.
static bool ensureRootSlotLocked(ChunkedList *self, u32 slot) {
    ChunkDir *dir = directoryOf(self);

    while (!dir || slot >= (*dir).capacity) {
        if (!dirGrow(self)) 
            return false;

        dir = directoryOf(self);

        if (!dir) 
            return false;
    }

    return true;
}

// Create + publish one leaf, walking (and creating) its internal path. Gate
// held by the caller. Rows are zeroed before publish (acquire/release handoff).
static bool chunkAddLocked(ChunkedList *self) {
    u32 leafIndex = atomic_load(&(*self).chunkCount);

    if (leafIndex == UINT32_MAX) 
        return false;

    u32 index = leafIndex << (*self).rowShift;

    u32 levels = (*self).levels;

    u32 d0 = digitOf(self, index, 0u);

    if (!ensureRootSlotLocked(self, d0)) 
        return false;

    ChunkDir *dir = directoryOf(self);

    if (!dir) 
        return false;

    _Atomic(u8*) *slot = &(*dir).slots[d0];

    u8 *newLeaf = nullptr;

    for (u32 depth = 1u; depth < levels; depth++) {
        if (depth == levels - 1u) {
            u8 *leaf = nodeAlloc(self, depth);

            if (!leaf) 
                return false;

            atomic_store_explicit(slot, leaf, memory_order_release);

            newLeaf = leaf;

            break;
        }

        u8 *node = atomic_load_explicit(slot, memory_order_acquire);

        if (!node) {
            node = nodeAlloc(self, depth);

            if (!node) 
                return false;

            atomic_store_explicit(slot, node, memory_order_release);
        }

        _Atomic(u8*) *children = (_Atomic(u8*) *) node;

        u32 digit = digitOf(self, index, depth);

        slot = &children[digit];
    }

    // Extend the chunk -> chunk chain: the previous leaf's tail link points at
    // the new leaf (published release, after the new leaf is fully zeroed).
    if (newLeaf && leafIndex > 0u) {
        u8 *prev = leafAtRow(self, (leafIndex - 1u) << (*self).rowShift);

        if (prev) {
            _Atomic(any) *link = (_Atomic(any) *) (prev + leafLinkOffset(self));

            atomic_store_explicit(link, newLeaf, memory_order_release);
        }
    }

    atomic_store(&(*self).chunkCount, leafIndex + 1u);

    u64 cap = ((u64)leafIndex + 1u) * (u64)(*self).rowsPerChunk;

    Collection *c = (Collection*) self;

    (*c).capacity = cap > (u64)UINT32_MAX ? UINT32_MAX : (u32)cap;

    return true;
}

// Growth gate body: publish leaves until `needChunks` exist. Gate held.
static bool ensureChunksLocked(ChunkedList *self, u32 needChunks) {
    while (atomic_load(&(*self).chunkCount) < needChunks) 
        if (!chunkAddLocked(self)) 
            return false;
    return true;
}

// Growth gate: bounded 100ms try-lock, drop-degrade false on contention or OOM.
static bool ensureChunks(ChunkedList *self, u32 needChunks) {
    if (needChunks <= atomic_load(&(*self).chunkCount)) 
        return true;

    if (!SpinLock_tryLockTimeout(&(*self).lock, VEX_CHUNKED_LOCK_NANOS)) 
        return false;

    bool ok = ensureChunksLocked(self, needChunks);

    SpinLock_unlock(&(*self).lock);

    return ok;
}

// Recompute the shift/mask tables from radices (geometry change, gate held).
static fn recomputeGeometry(ChunkedList *self) {
    u32 levels = (*self).levels;

    u32 *radices = (*self).radices;

    u32 *shifts = (*self).shifts;

    u32 *masks = (*self).masks;

    u32 rows = radices[levels - 1u];

    (*self).rowsPerChunk = rows;

    (*self).rowShift = shiftOf(rows);

    (*self).rowMask = rows - 1u;

    shifts[levels - 1u] = 0u;

    masks[levels - 1u] = rows - 1u;

    for (u32 d = levels - 1u; d > 0u; d--) {
        shifts[d - 1u] = shifts[d] + shiftOf(radices[d]);

        masks[d - 1u] = (d - 1u) == 0u ? 0u : (radices[d - 1u] - 1u);
    }
}

// Shared builder: allocate the struct + geometry tables, zero the counters.
// `chunkBytes` 0 derives the leaf byte size from rows*stride.
static ChunkedList *buildPaged(u32 elementClass, bytes stride, const u32 *radixIn, u32 levels, u32 chunkBytes) {
    if (levels < 2u) 
        levels = 2u;

    if (stride == 0u) 
        stride = Stride_get(elementClass);

    if (stride == 0u) 
        stride = sizeof(any);

    if (stride > (bytes)UINT32_MAX) 
        return nullptr;

    ChunkedList *self = (ChunkedList*) Memory_alloc(TYPE_CHUNKED_LIST, sizeof(ChunkedList));

    if (!self) {
        (discard) Struct_reportConstructionExhaustion("chunked_list", sizeof(ChunkedList));

        return nullptr;
    }

    memset(self, 0, sizeof(*self));

    u32 *radices = (u32*) Memory_alloc(TYPE_CHUNKED_LIST, (bytes)levels * sizeof(u32));

    u32 *shifts = (u32*) Memory_alloc(TYPE_CHUNKED_LIST, (bytes)levels * sizeof(u32));

    u32 *masks = (u32*) Memory_alloc(TYPE_CHUNKED_LIST, (bytes)levels * sizeof(u32));

    if (!radices || !shifts || !masks) {
        (discard) Struct_reportConstructionExhaustion("chunked_list", (bytes)levels * sizeof(u32) * 3u);

        Memory_free(radices);

        Memory_free(shifts);

        Memory_free(masks);

        Memory_free(self);

        return nullptr;
    }

    for (u32 d = 0u; d < levels; d++) {
        u32 r = radixIn[d];

        radices[d] = r == 0u ? 1u : pow2Floor(r);
    }

    (*self).levels = levels;

    (*self).radices = radices;

    (*self).shifts = shifts;

    (*self).masks = masks;

    recomputeGeometry(self);

    if (chunkBytes == 0u) {
        u64 b = (u64)radices[levels - 1u] * (u64)stride;

        chunkBytes = b > (u64)UINT32_MAX ? UINT32_MAX : (u32)b;
    }

    if (chunkBytes < 16u) 
        chunkBytes = 16u;

    (*self).chunkBytes = chunkBytes;

    Collection *c = (Collection*) self;

    (*c).typeId = TYPE_CHUNKED_LIST;

    (*c).activeCount = 0;

    (*c).elementClass = elementClass;

    (*c).stride = (u32)stride;

    (*c).capacity = 0;

    (*c).head = 0;

    (*c).exhaustionCount = 0;

    (*c).exhaustionReported = false;

    (*c).data = nullptr;

    atomic_store_explicit(&(*self).directory, nullptr, memory_order_relaxed);

    atomic_store(&(*self).chunkCount, 0u);

    atomic_store(&(*self).committed, 0u);

    (*self).lock = SPIN_LOCK_INIT;

    return self;
}

// The 2-level byte-budget form (the historical shape).
ChunkedList *ChunkedList_3(u32 elementClass, u32 stride, u32 chunkBytes) {
    if (stride == 0u) 
        stride = Stride_get(elementClass);

    if (stride == 0u) 
        stride = sizeof(any);

    if (stride > (bytes)UINT32_MAX) 
        return nullptr;

    if (chunkBytes < 16u) 
        chunkBytes = 16u;

    u32 rows = pow2Rows(stride, chunkBytes);

    u32 radixIn[2] = { VEX_CHUNKED_DIR_INIT, rows };

    return buildPaged(elementClass, stride, radixIn, 2u, chunkBytes);
}

// Recursive teardown: free a depth-`depth` object and everything under it.
static fn freeSubtree(u8 *node, const ChunkedList *self, u32 depth) {
    if (!node) 
        return;

    if (depth == (*self).levels - 1u) {
        Memory_free(node);

        return;
    }

    _Atomic(u8*) *children = (_Atomic(u8*) *) node;

    u32 radix = (*self).radices[depth];

    for (u32 i = 0u; i < radix; i++) 
        freeSubtree(atomic_load_explicit(&children[i], memory_order_relaxed), self, depth + 1u);

    Memory_free(node);
}

// CONSTRUCTORS

ChunkedList *ChunkedList_1(u32 elementClass) {
    return ChunkedList_3(elementClass, Stride_get(elementClass), VEX_CHUNKED_BYTES_DEFAULT);
}

ChunkedList *ChunkedList_2(u32 elementClass, u32 chunkBytes) {
    return ChunkedList_3(elementClass, Stride_get(elementClass), chunkBytes);
}

ChunkedList *ChunkedList_paged(u32 elementClass, u32 stride, const i32 *radices, bytes count) {
    if (stride == 0u) 
        stride = (u32) Stride_get(elementClass);

    if (stride == 0u) 
        stride = (u32) sizeof(any);

    bytes cap = count > 3u ? count : 3u;

    u32 *shape = (u32*) Memory_alloc(TYPE_CHUNKED_LIST, cap * sizeof(u32));

    if (!shape) 
        return nullptr;

    u32 levels = 2u;

    bool preset = count >= 1u && radices[0] < 0;

    if (preset) {
        // A *_LAYER_DEFAULT sentinel selects the shape; explicit numbers after
        // it override the corresponding level (index 1 upward).
        if (radices[0] == CHUNKED_LIST_THREE_LAYER_DEFAULT) 
            levels = 3u;
        else 
            levels = 2u;

        if (levels > cap) {
            Memory_free(shape);

            return nullptr;
        }

        shape[0] = VEX_CHUNKED_DIR_INIT;

        for (u32 d = 1u; d + 1u < levels; d++) 
            shape[d] = VEX_CHUNKED_INTERNAL_RADIX_DEFAULT;

        shape[levels - 1u] = pageLeafRows(stride);

        for (bytes k = 1u; k < count; k++) {
            if (radices[k] > 0 && k < (bytes)levels) 
                shape[k] = (u32) radices[k];
        }
    } else {
        levels = count < 2u ? 2u : (u32) count;

        if (levels > cap) {
            Memory_free(shape);

            return nullptr;
        }

        for (u32 d = 0u; d < levels; d++) {
            i32 v = d < (u32) count ? radices[d] : 0;

            if (v > 0) 
                shape[d] = (u32) v;
            else 
                shape[d] = d == 0u ? VEX_CHUNKED_DIR_INIT : 1u;
        }
    }

    ChunkedList *self = buildPaged(elementClass, stride, shape, levels, 0u);

    Memory_free(shape);

    return self;
}

// CORE FUNCTIONS

fn ChunkedList_free(ChunkedList *self) {
    if (!self) 
        return;

    ChunkDir *dir = directoryOf(self);

    if (dir) {
        for (u32 i = 0u; i < (*dir).capacity; i++) 
            freeSubtree(atomic_load_explicit(&(*dir).slots[i], memory_order_relaxed), self, 1u);
    }

    while (dir) {
        ChunkDir *prev = (*dir).prev;

        Memory_free(dir);

        dir = prev;
    }

    Memory_free((*self).radices);

    Memory_free((*self).shifts);

    Memory_free((*self).masks);

    Memory_free(self);
}

u8 *ChunkedList_addSlot(ChunkedList *self) {
    if (!self) 
        return nullptr;

    if (!SpinLock_tryLockTimeout(&(*self).lock, VEX_CHUNKED_LOCK_NANOS)) 
        return nullptr;

    u8 *row = nullptr;

    u32 index = atomic_load(&(*self).committed);

    if (index != UINT32_MAX) {
        u32 needChunks = (index >> (*self).rowShift) + 1u;

        if (ensureChunksLocked(self, needChunks)) {
            row = rowAt(self, index);

            if (row) {
                Collection *c = (Collection*) self;

                memset(row, 0, (*c).stride);

                atomic_store(&(*self).committed, index + 1u);

                (*c).activeCount = index + 1u;
            }
        }
    }

    SpinLock_unlock(&(*self).lock);

    return row;
}

bool ChunkedList_reserve(ChunkedList *self, u32 rows) {
    if (!self) 
        return false;

    u32 needChunks = (u32)(((u64)rows + (*self).rowsPerChunk - 1u) >> (*self).rowShift);

    return ensureChunks(self, needChunks);
}

bool ChunkedList_packInto(const ChunkedList *self, u8 *dest, bytes destCap, u32 *outRows, bool *outTruncated) {
    if (outRows) 
        *outRows = 0u;

    if (outTruncated) 
        *outTruncated = false;

    if (!self || !dest) 
        return false;

    u32 rows = atomic_load(&(*self).committed);

    const Collection *coll = &(*self).collection;

    bytes stride = (*coll).stride;

    bytes fit = stride == 0 ? 0 : destCap / stride;

    bool truncated = (bytes)rows > fit;

    u32 copy = truncated ? (u32)fit : rows;

    u8 *out = dest;

    for (u32 i = 0; i < copy; i++) {
        u8 *row = rowAt(self, i);

        if (!row) {
            copy = i;

            truncated = true;

            break;
        }

        memcpy(out, row, stride);

        out += stride;
    }

    if (outRows) 
        *outRows = copy;

    if (outTruncated) 
        *outTruncated = truncated;

    return !truncated;
}

u8 *ChunkedList_slot(const ChunkedList *self, u32 index) {
    if (!self) 
        return nullptr;

    if (index >= atomic_load(&(*self).committed)) 
        return nullptr;

    return rowAt(self, index);
}

u8 *ChunkedList_getChunk(const ChunkedList *self, u32 chunkIndex) {
    if (!self) 
        return nullptr;

    if (chunkIndex > (UINT32_MAX >> (*self).rowShift)) 
        return nullptr;

    return leafAtRow(self, chunkIndex << (*self).rowShift);
}

u8 *ChunkedList_nextChunk(const ChunkedList *self, const u8 *chunk) {
    if (!self || !chunk) 
        return nullptr;

    const _Atomic(any) *link = (const _Atomic(any) *) (chunk + leafLinkOffset(self));

    return (u8*) atomic_load_explicit(link, memory_order_acquire);
}

fn ChunkedList_forEachChunk(const ChunkedList *self, ChunkedListChunkFn fn, any userdata) {
    if (!self || !fn) 
        return;

    u8 *chunk = ChunkedList_getChunk(self, 0u);

    u32 index = 0u;

    while (chunk != nullptr) {
        fn(chunk, index, userdata);

        chunk = ChunkedList_nextChunk(self, chunk);

        index++;
    }
}

// SETTERS

fn ChunkedList_setChunkBytes(ChunkedList *self, u32 chunkBytes) {
    if (!self) 
        return;

    if (!SpinLock_tryLockTimeout(&(*self).lock, VEX_CHUNKED_LOCK_NANOS)) 
        return;

    if (atomic_load(&(*self).chunkCount) == 0u) {
        if (chunkBytes < 16u) 
            chunkBytes = 16u;

        const Collection *coll = &(*self).collection;

        u32 rows = pow2Rows((bytes)(*coll).stride, chunkBytes);

        (*self).chunkBytes = chunkBytes;

        (*self).radices[(*self).levels - 1u] = rows;

        recomputeGeometry(self);
    }

    SpinLock_unlock(&(*self).lock);
}

// GETTERS

u32 ChunkedList_size(const ChunkedList *self) {
    if (!self) 
        return 0u;

    return atomic_load(&(*self).committed);
}

u32 ChunkedList_length(const ChunkedList *self) {
    return ChunkedList_size(self);
}

u32 ChunkedList_capacity(const ChunkedList *self) {
    if (!self) 
        return 0u;

    u64 cap = (u64)atomic_load(&(*self).chunkCount) * (u64)(*self).rowsPerChunk;

    if (cap > (u64)UINT32_MAX) 
        return UINT32_MAX;

    return (u32)cap;
}

bool ChunkedList_isEmpty(const ChunkedList *self) {
    if (!self) 
        return true;

    return atomic_load(&(*self).committed) == 0u;
}

;;TEST
u64 ChunkedList_exhaustionCount(const ChunkedList *self) {
    return self ? Collection_exhaustionCount(&(*self).collection) : 0;
}

u32 ChunkedList_elementClassId(const ChunkedList *self) {
    if (self == nullptr) 
       return 0u;

    const Collection *coll = &(*self).collection;

    return (*coll).elementClass;
}

u32 ChunkedList_stride(const ChunkedList *self) {
    if (self == nullptr) 
       return 0u;

    const Collection *coll = &(*self).collection;

    return (*coll).stride;
}

u32 ChunkedList_getChunkCount(const ChunkedList *self) {
    if (!self) 
        return 0u;

    return atomic_load(&(*self).chunkCount);
}

u32 ChunkedList_getRowsPerChunk(const ChunkedList *self) {
    return self ? (*self).rowsPerChunk : 0u;
}

u32 ChunkedList_getChunkBytes(const ChunkedList *self) {
    return self ? (*self).chunkBytes : 0u;
}

u32 ChunkedList_getLevels(const ChunkedList *self) {
    return self ? (*self).levels : 0u;
}
