#ifndef STRUCT_COLLECTION_H
#define STRUCT_COLLECTION_H



// struct/collection.h — the Collection metadata struct, ported from
// struct/Collection.java.
//
// Every collection (List, Array, Stack, Deque, Queue, Map, Set) embeds a
// Collection as its first member, so a pointer to any of them is also a
// Collection pointer and the accessors below read the shared header. The type
// itself lives in the block header (Memory_type), so Collection_type() walks
// back to it — one subtract, nothing more.

typedef struct Collection {
    u64 typeId;        // mirror of the block-header type (for debug)
    u32 activeCount;   // number of live elements
    u32 elementClass;  // class of elements (Map: key class)
    u32 stride;        // Bytes per element (Map: val class)
    u32 capacity;      // element capacity (or slot capacity)
    u32 head;          // circular head index (Deque/Queue); else 0
    u8 *data;          // element / slot buffer
    // Exhaustion observability (the Exhaustion Loudness Law): every refused
    // grow is counted; the epoch's first refusal also emits one diagnostic.
    u64 exhaustionCount;   // refusals since construction/reset
    bool exhaustionReported;    // the epoch's one diagnostic was emitted
} Collection;

u64 Collection_type(Collection *c);
u32 Collection_size(Collection *c);
u32 Collection_length(Collection *c);
bool Collection_isEmpty(Collection *c);
u32 Collection_elementClassId(Collection *c);
u32 Collection_keyClassId(Collection *c);
u32 Collection_stride(Collection *c);
u32 Collection_valClassId(Collection *c);
u32 Collection_capacity(Collection *c);
u32 Collection_head(Collection *c);
u8 *Collection_dataBuffer(Collection *c);

// --- Exhaustion observability (the Exhaustion Loudness Law) ---
// Called by a container when its buffer could not grow: counts the refusal and
// emits the epoch's single THROW naming the requested bytes. Always returns
// false so a caller can `return Collection_reportExhaustion(...)`. A collection
// constructed (or reset) starts a fresh epoch.
#include "vex.h"

;;VALIDATOR
bool Collection_reportExhaustion(Collection *c, bytes requestedBytes);
// The number of refused grows since the collection was constructed or reset.
;;TEST
u64 Collection_exhaustionCount(const Collection *c);
// Start a fresh exhaustion epoch (count zeroed, diagnostic armed again).
;;TEST
fn Collection_resetExhaustion(Collection *c);

// Generic slot read/write for the scalar/pointer collections. Reads the slot at
// index with the collection's stride, widening small ints (legacy readSlot).
u64 Collection_readSlot(Collection *c, bytes index);
fn Collection_writeSlot(Collection *c, bytes index, u64 value);

// Slot read/write without bounds checks (legacy unsafeGet/setUnsafe).
u64 Collection_readSlotUnsafe(Collection *c, bytes index);
fn Collection_writeSlotUnsafe(Collection *c, bytes index, u64 value);

#endif