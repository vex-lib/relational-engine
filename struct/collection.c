#include "vex.h"
#include "nio/mem.h"

#include <stdckdint.h>

#include "struct/collection.h"
#include "struct/exhaustion.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Collection
 * ============================================================================
 * The Collection metadata struct, ported from struct/Collection.java: a
 * header describing a typed element buffer — type id, active count, element
 * class, stride, capacity, circular head index, and the data pointer.
 * Provides stride-aware slot read/write (bounded and unsafe variants) plus
 * size/length/capacity/head/data introspection. Queue and Deque embed this
 * header as their first member and reuse its slot machinery. Lives at R2 as
 * a leaf container behavior.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Collection (struct/collection.c)
 * ============================================================================
 * the Collection metadata struct, ported from
 *
 * STRUCT FIELDS (Mirroring struct/collection.h):
 * ----------------------------------------------------------------------------
 *   Collection {
 *     u64 typeId; // mirror of the block-header type (for debug)
 *     u32 activeCount; // number of live elements
 *     u32 elementClass; // class of elements (Map: key class)
 *     u32 stride; // Bytes per element (Map: val class)
 *     u32 capacity; // element capacity (or slot capacity)
 *     u32 head; // circular head index (Deque/Queue); else 0
 *     u8 *data; // element / slot buffer
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - Collection_type(c)
 *   - Collection_size(c)
 *   - Collection_length(c)
 *   - Collection_elementClassId(c)
 *   - Collection_keyClassId(c)
 *   - Collection_stride(c)
 *   - Collection_valClassId(c)
 *   - Collection_capacity(c)
 *   - Collection_head(c)
 *   - Collection_dataBuffer(c)
 *   - Collection_reportExhaustion(c, requestedBytes)
 *   - Collection_exhaustionCount(c)
 *   - Collection_resetExhaustion(c)
 *   - Collection_readSlot(c, index)
 *   - Collection_writeSlot(c, index, value)
 *   - Collection_readSlotUnsafe(c, index)
 *   - Collection_writeSlotUnsafe(c, index, value)
 *
 * Getters:
 *   - Collection_isEmpty(c)
 * ============================================================================
 */


// collection.c — Collection port (Legacy: struct/Collection.java).

static u64 readSlotAt(const u8 *data, bytes stride) {
    switch (stride) {
        case 1:  return (u64)(*(const u8*) data);
        case 2:  return (u64)(*(const u16*) data);
        case 4:  return (u64)(*(const u32*) data);
        default: return *(const u64*) data;
    }
}

static fn writeSlotAt(u8 *data, bytes stride, u64 value) {
    switch (stride) {
        case 1:  *(u8*) data = (u8)value;   break;
        case 2:  *(u16*) data = (u16)value; break;
        case 4:  *(u32*) data = (u32)value; break;
        default: *(u64*) data = value;           break;
    }
}

u64 Collection_type(Collection *c) {
    if (!c) 
       return 0;

    return Memory_type(c);
}

u32 Collection_size(Collection *c) {
    if (!c) 
       return 0;

    return (*c).activeCount;
}

u32 Collection_length(Collection *c) {
    return Collection_size(c);
}

bool Collection_isEmpty(Collection *c) {
    return Collection_size(c) == 0;
}

u32 Collection_elementClassId(Collection *c) {
    if (!c) 
       return 0;

    return (*c).elementClass;
}

u32 Collection_keyClassId(Collection *c) {
    return Collection_elementClassId(c);
}

u32 Collection_stride(Collection *c) {
    if (!c) 
       return 0;

    return (*c).stride;
}

u32 Collection_valClassId(Collection *c) {
    return Collection_stride(c);
}

u32 Collection_capacity(Collection *c) {
    if (!c) 
       return 0;

    return (*c).capacity;
}

// --- Exhaustion observability (the Exhaustion Loudness Law) ---

/** Count a refused grow and emit the epoch's single diagnostic. Always false. */
;;VALIDATOR
bool Collection_reportExhaustion(Collection *c, bytes requestedBytes) {
    if (!c) {
        THROW("collection: grow refused on a null collection (%zu bytes)", requestedBytes);

        return false;
    }
    // One implementation for the whole ecosystem: the shared owner-agnostic seam.
    return Struct_reportExhaustion(&(*c).exhaustionCount, &(*c).exhaustionReported,
                                   "collection", requestedBytes,
                                   (bytes) (*c).capacity * (*c).stride);
}

/** Return the refused-grow count since construction or the last reset. */
;;TEST
u64 Collection_exhaustionCount(const Collection *c) {
    return c ? (*c).exhaustionCount : 0;
}

/** Start a fresh exhaustion epoch for this collection. */
;;TEST
fn Collection_resetExhaustion(Collection *c) {
    if (!c) 
       return;

    Struct_resetExhaustion(&(*c).exhaustionCount, &(*c).exhaustionReported);
}

u32 Collection_head(Collection *c) {
    if (!c) 
       return 0;

    return (*c).head;
}

u8 *Collection_dataBuffer(Collection *c) {
    if (!c) 
       return nullptr;

    return (*c).data;
}

// The BOUNDED read/write pair: an out-of-range index is refused with a safe
// default AND reported on stderr (the Failure Observability Law) — never a
// silent out-of-bounds dereference. index * stride is checked for overflow
// (the Overflow Guard Law) so a hostile index cannot wrap the pointer.
u64 Collection_readSlot(Collection *c, bytes index) {
    if (!c) 
        return 0;
    // Bound by CAPACITY (the physical memory bound), not activeCount: a
    // circular container (Queue/Deque) reads a PHYSICAL slot (head) that may
    // exceed the live count. Logical bounds are the callers' contract.
    if (index >= (*c).capacity) {
        THROW("collection readSlot: index %zu out of capacity (%u)",
              index, (*c).capacity);
        return 0;
    }

    bytes offset = 0;

    if (ckd_mul(&offset, index, (bytes) (*c).stride)) {
        THROW("collection readSlot: index %zu * stride %u overflows", index, (*c).stride);

        return 0;
    }

    return readSlotAt((*c).data + offset, (*c).stride);
}

fn Collection_writeSlot(Collection *c, bytes index, u64 value) {
    if (!c) 
        return;
    // Bound by CAPACITY, not activeCount: an append (List_add) writes the slot
    // at index == activeCount, a legal PHYSICAL slot. activeCount is the
    // caller's contract; capacity is the memory bound.
    if (index >= (*c).capacity) {
        THROW("collection writeSlot: index %zu out of capacity (%u)",
              index, (*c).capacity);
        return;
    }

    bytes offset = 0;

    if (ckd_mul(&offset, index, (bytes) (*c).stride)) {
        THROW("collection writeSlot: index %zu * stride %u overflows", index, (*c).stride);

        return;
    }

    writeSlotAt((*c).data + offset, (*c).stride, value);
}

// Deliberately unchecked (trust-the-handle): NO bounds check on index — the
// caller has validated it. Null-guarded so a hostile handle degrades instead of
// crashing (the Cold-Strict, Hot-Minimal Validation Law).
u64 Collection_readSlotUnsafe(Collection *c, bytes index) {
    if (!c) 
        return 0;

    return readSlotAt((*c).data + index * (*c).stride, (*c).stride);
}

fn Collection_writeSlotUnsafe(Collection *c, bytes index, u64 value) {
    if (!c) 
        return;

    writeSlotAt((*c).data + index * (*c).stride, (*c).stride, value);
}
