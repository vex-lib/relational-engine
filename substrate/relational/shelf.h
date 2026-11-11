#ifndef RELATIONAL_SHELF_H
#define RELATIONAL_SHELF_H

#include "vex.h"

#include "lang/constructor.h"
#include "struct/chunked_list.h"

// substrate/relational/shelf.h — the global shelf: a node pool with u32 graph edges.
//
// The shelf is the Relational Engine's "linked list inside an arraylist": nodes
// live in a never-moved ChunkedList (so a node's address is stable for life),
// and the graph edges are 32-bit INDICES into that list — data-oriented and
// relocatable, not invisible pointer chases. Each node references one identity
// Cell, allocated separately so it carries its own 32-byte header; the shelf is
// the indexable pool the reflective walk traverses.
//
// COLD PATH ONLY (the Cold-Only Reflection Law): the shelf is the rendezvous
// walked by search/debug/script/hot-swap, never a per-frame structure. See the
// `;;INTENTION` marker on the walk in the resolver.

#define SHELF_INDEX_NONE UINT32_MAX

// SLOT RECORD (owned by Shelf, behaviorless): one shelf node.
typedef struct ShelfNode {
    pointer cell; // the identity cell this node references (0 when bare)
    u32 next;  // graph edge: u32 index of the next node (SHELF_INDEX_NONE = end)
    u32 pad;   // explicit padding (keeps the row 16 Bytes)
} ShelfNode;

typedef struct Shelf {
    bool active;        // runtime-active flag
    u32 count;     // live nodes (== row count)
    u32 head;      // first node index (SHELF_INDEX_NONE when empty)
    ChunkedList *nodes; // never-moved ShelfNode rows (u32 edges)
} Shelf;

bool Shelf_init(Shelf *shelf);
void Shelf_shutdown(Shelf *shelf);

Shelf *Shelf_0(void);
#define Shelf(...) CONSTRUCTOR_DISPATCH(Shelf, __VA_ARGS__)
void Shelf_free(Shelf *shelf);

u32 Shelf_addCell(Shelf *shelf, u64 typeId, pointer value);
u32 Shelf_addNode(Shelf *shelf, pointer cell);

pointer Shelf_getCell(const Shelf *shelf, u32 index);
u32 Shelf_getNext(const Shelf *shelf, u32 index);
bool Shelf_setNext(Shelf *shelf, u32 index, u32 next);
bool Shelf_link(Shelf *shelf, u32 from, u32 to);

bool Shelf_setHead(Shelf *shelf, u32 index);

u32 Shelf_getHead(const Shelf *shelf);
u32 Shelf_count(const Shelf *shelf);
bool Shelf_isEmpty(const Shelf *shelf);

void Shelf_toString(const Shelf *shelf, char *dest, bytes cap, bool *outTruncated);
void Shelf_toStringStruct(const Shelf *shelf, char *dest, bytes cap, bool *outTruncated);

#endif
