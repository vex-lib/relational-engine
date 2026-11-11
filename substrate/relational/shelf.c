// substrate/relational/shelf.c — the global shelf: a node pool with u32 graph edges.
//
// Nodes are never-moved ChunkedList rows; edges are u32 indices; each node
// references one separately-allocated identity Cell. Cold path only.

#include "substrate/relational/shelf.h"

#include "vex.h"
#include "nio/mem.h"
#include "substrate/relational/cell.h"
#include "type/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Shelf
 * ============================================================================
 * The Relational Engine's global shelf: a pool of never-moved nodes plus a graph
 * of 32-bit index edges. Nodes live in a ChunkedList (radix page table + leaf
 * chain), so a node index resolves in O(1) and a node address never moves; the
 * edges are u32 indices rather than pointers, which keeps the shelf
 * data-oriented, relocatable, and packable. Each node references one identity
 * Cell, allocated separately so Cell_check reads its 32-byte header.
 *
 * Lifetime: an arena object (or embedded); Shelf_shutdown frees every referenced
 * cell and the node list. Ownership of cells added via Shelf_addCell/addNode
 * transfers to the shelf.
 *
 * Path temperature: cold. The shelf is walked by the reflective rendezvous
 * (search, debug, script, save/load, hot-swap), never on a frame (the Cold-Only
 * Reflection Law). Getters are null-safe.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Shelf (substrate/relational/shelf.c)
 * ============================================================================
 * the global shelf: a never-moved node pool with u32 graph edges.
 *
 * STRUCT FIELDS (Mirroring substrate/relational/shelf.h):
 * ----------------------------------------------------------------------------
 *   Shelf {
 *     bool active;        // runtime-active flag
 *     u32 count;     // live nodes (== row count)
 *     u32 head;      // first node index (SHELF_INDEX_NONE when empty)
 *     ChunkedList *nodes; // never-moved ShelfNode rows (u32 edges)
 *   }
 *
 * SLOT RECORD (owned by Shelf, behaviorless):
 * ----------------------------------------------------------------------------
 *   ShelfNode cell; // uintptr_t + the referenced identity cell
 *   ShelfNode next; // u32 + the outgoing graph edge index
 *   ShelfNode pad;  // u32 + explicit row padding
 *
 * PRIVATE HELPERS (kept file-local, pure logic, no behavior of their own):
 * ----------------------------------------------------------------------------
 *   (none — nodeAt is a static function below, not a record)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Shelf_0()                        : arena-allocated empty shelf
 *   - Shelf_init(shelf)                : embedded init
 *   - Shelf_free(shelf)                : shutdown + release an arena shelf
 *   - Shelf_shutdown(shelf)            : free cells + the node list
 *
 * Private Core Functions: (.c static)
 *   - nodeAt(shelf, index)             : bounds-checked row resolve
 *
 * Public Core Functions: (.h)
 *   - Shelf_addCell(shelf, typeId, value)
 *   - Shelf_addNode(shelf, cell)
 *   - Shelf_getCell(shelf, index)
 *   - Shelf_getNext(shelf, index)
 *   - Shelf_setNext(shelf, index, next)
 *   - Shelf_link(shelf, from, to)
 *
 * Public Setters: (.h)
 *   - Shelf_setHead(shelf, index)
 *
 * Public Getters: (.h)
 *   - Shelf_getHead(shelf)
 *   - Shelf_count(shelf)
 *   - Shelf_isEmpty(shelf)
 *
 * Public String Projections: (.h)
 *   - Shelf_toString(shelf, dest, cap, outTruncated)
 *   - Shelf_toStringStruct(shelf, dest, cap, outTruncated)
 * ============================================================================
 */

// Bounds-checked node resolve: null on a null shelf or an out-of-range index.
static ShelfNode *nodeAt(const Shelf *shelf, u32 index) {
    if (!shelf || index >= (*shelf).count)
        return nullptr;
    return (ShelfNode*) ChunkedList_slot((*shelf).nodes, index);
}

// CONSTRUCTORS

bool Shelf_init(Shelf *shelf) {
    if (!shelf)
        return false;
    ChunkedList *nodes = ChunkedList(ID_SHELF_NODE, (u32) sizeof(ShelfNode), VEX_CHUNKED_BYTES_DEFAULT);
    if (!nodes)
        return false;
    (*shelf).active = true;
    (*shelf).count = 0u;
    (*shelf).head = SHELF_INDEX_NONE;
    (*shelf).nodes = nodes;
    return true;
}

void Shelf_shutdown(Shelf *shelf) {
    if (!shelf)
        return;
    ChunkedList *nodes = (*shelf).nodes;
    if (nodes) {
        u32 rows = ChunkedList_size(nodes);
        for (u32 i = 0; i < rows; i++) {
            ShelfNode *row = (ShelfNode*) ChunkedList_slot(nodes, i);
            if (row && (*row).cell)
                Cell_free((Cell*) (*row).cell);
        }
        ChunkedList_free(nodes);
    }
    (*shelf).nodes = nullptr;
    (*shelf).count = 0u;
    (*shelf).head = SHELF_INDEX_NONE;
    (*shelf).active = false;
}

Shelf *Shelf_0(void) {
    Shelf *shelf = Memory_alloc(TYPE_SHELF, sizeof(Shelf));
    if (!shelf)
        return nullptr;
    if (!Shelf_init(shelf)) {
        Memory_free(shelf);
        return nullptr;
    }
    return shelf;
}

void Shelf_free(Shelf *shelf) {
    if (!shelf)
        return;
    Shelf_shutdown(shelf);
    Memory_free(shelf);
}

// CORE FUNCTIONS

u32 Shelf_addNode(Shelf *shelf, uintptr_t cell) {
    if (!shelf || !(*shelf).active)
        return SHELF_INDEX_NONE;
    ShelfNode *row = (ShelfNode*) ChunkedList_addSlot((*shelf).nodes);
    if (!row)
        return SHELF_INDEX_NONE;
    (*row).cell = cell;
    (*row).next = SHELF_INDEX_NONE;
    (*row).pad = 0u;
    u32 index = ChunkedList_size((*shelf).nodes) - 1u;
    (*shelf).count = index + 1u;
    return index;
}

u32 Shelf_addCell(Shelf *shelf, u64 typeId, uintptr_t value) {
    Cell *cell = Cell_2(typeId, value);
    if (!cell)
        return SHELF_INDEX_NONE;
    u32 index = Shelf_addNode(shelf, (uintptr_t) cell);
    if (index == SHELF_INDEX_NONE)
        Cell_free(cell);
    return index;
}

uintptr_t Shelf_getCell(const Shelf *shelf, u32 index) {
    ShelfNode *row = nodeAt(shelf, index);
    if (!row)
        return 0u;
    return (*row).cell;
}

u32 Shelf_getNext(const Shelf *shelf, u32 index) {
    ShelfNode *row = nodeAt(shelf, index);
    if (!row)
        return SHELF_INDEX_NONE;
    return (*row).next;
}

bool Shelf_setNext(Shelf *shelf, u32 index, u32 next) {
    ShelfNode *row = nodeAt(shelf, index);
    if (!row)
        return false;
    (*row).next = next;
    return true;
}

bool Shelf_link(Shelf *shelf, u32 from, u32 to) {
    return Shelf_setNext(shelf, from, to);
}

// SETTERS

bool Shelf_setHead(Shelf *shelf, u32 index) {
    if (!shelf)
        return false;
    if (index != SHELF_INDEX_NONE && index >= (*shelf).count)
        return false;
    (*shelf).head = index;
    return true;
}

// GETTERS

u32 Shelf_getHead(const Shelf *shelf) {
    if (!shelf)
        return SHELF_INDEX_NONE;
    return (*shelf).head;
}

u32 Shelf_count(const Shelf *shelf) {
    return shelf ? (*shelf).count : 0u;
}

bool Shelf_isEmpty(const Shelf *shelf) {
    if (!shelf)
        return true;
    return (*shelf).count == 0u;
}

// STRING PROJECTIONS (the toString Law)

void Shelf_toString(const Shelf *shelf, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!shelf) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Shelf(count=%u)", (*shelf).count);
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void Shelf_toStringStruct(const Shelf *shelf, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!shelf) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Shelf { active=%s, count=%u, head=%d, nodes=0x%llx }",
                           (*shelf).active ? "true" : "false", (*shelf).count,
                           (int) (*shelf).head, (unsigned long long) (uintptr_t) (*shelf).nodes);
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
