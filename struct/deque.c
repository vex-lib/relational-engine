#include "vex.h"
#include "nio/mem.h"

#include "struct/deque.h"
#include "struct/stride.h"
#include "type/re_type.h"
#include "struct/exhaustion.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Deque
 * ============================================================================
 * d64-ended queue over a circular buffer, ported from struct/Deque.java.
 * Logical index i maps to physical (head + i) % capacity; addFirst/addLast
 * and removeFirst/removeLast touch both ends in O(1), and the buffer grows
 * by DEFAULT_CAPACITY chunks through the memory arena when full. The
 * Collection base record (typeId, activeCount, elementClass, stride,
 * capacity, head, data) is embedded at offset zero so asCollection casts are
 * layout-safe. All storage is arena-backed; the deque never allocates in
 * steady state.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Deque (struct/deque.c)
 * ============================================================================
 * the Deque class, ported from struct/Deque.java.
 *
 * STRUCT FIELDS (Mirroring struct/deque.h):
 * ----------------------------------------------------------------------------
 *   Deque {
 *     Collection collection; // instance state
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Deque_2(element_class, capacity)
 *   - Deque_1(element_class)
 *   - Deque(element_class, capacity, count)
 *
 * Core Functions:
 *   - Deque_free(deque)
 *   - Deque_addFirst(deque, value_or_pointer)
 *   - Deque_addLast(deque, value_or_pointer)
 *   - Deque_removeFirst(deque)
 *   - Deque_removeLast(deque)
 *   - Deque_peekFirst(deque)
 *   - Deque_peekLast(deque)
 *   - Deque_slot(deque, index)
 *   - Deque_size(deque)
 *   - Deque_length(deque)
 *   - Deque_capacity(deque)
 *   - Deque_elementClassId(deque)
 *   - Deque_stride(deque)
 *   - Deque_head(deque)
 *   - Deque_dataBuffer(deque)
 *
 * Getters:
 *   - Deque_get(deque, index)
 *   - Deque_isEmpty(deque)
 * ============================================================================
 */


// deque.c — Deque port (Legacy: struct/Deque.java). Circular buffer: logical
// index i maps to physical (head + i) % capacity.

static const bytes DEFAULT_CAPACITY = 1024;

static Collection *asCollection(Deque *deque) {
    return (Collection*) deque;
}

Deque *Deque_3(u32 elementClass, bytes capacity, bytes count) {
    bytes stride = Stride_get(elementClass);

    bytes cap = capacity < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : capacity;

    Deque *deque = (Deque*) Memory_alloc(TYPE_DEQUE, sizeof(Deque));

    if (!deque) {
        (discard) Struct_reportConstructionExhaustion("deque", sizeof(Deque));

        return nullptr;
    }

    Collection *c = asCollection(deque);

    (*c).typeId = TYPE_DEQUE;

    (*c).activeCount = (u32)count;

    (*c).elementClass = elementClass;

    (*c).stride = (u32)stride;

    (*c).capacity = (u32)cap;

    (*c).head = 0;

    (*c).exhaustionCount = 0;

    (*c).exhaustionReported = false;

    bytes Bytes = cap * stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, elementClass);

    (*c).data = (u8*) Memory_alloc(bufType, Bytes);

    if (!(*c).data) {
        (discard) Struct_reportConstructionExhaustion("deque", Bytes);

        Memory_free(deque);

        return nullptr;
    }

    memset((*c).data, 0, Bytes);

    return deque;
}

static i32 ensureCapacity(Collection *c) {
    if ((*c).activeCount < (*c).capacity) 
        return 1;

    bytes newCap = (*c).capacity * 2;

    bytes Bytes = newCap * (*c).stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, (*c).elementClass);

    u8 *next = (u8*) Memory_alloc(bufType, Bytes);

    if (!next) {
        Collection_reportExhaustion(c, Bytes);

        return 0;
    }

    bytes count = (*c).activeCount;

    if (count > 0) {
        if ((*c).head == 0) {
            memcpy(next, (*c).data, count * (*c).stride);
        } else {
            bytes len1 = (*c).capacity - (*c).head;

            bytes len2 = (*c).head;

            memcpy(next, (*c).data + (*c).head * (*c).stride, len1 * (*c).stride);

            memcpy(next + len1 * (*c).stride, (*c).data, len2 * (*c).stride);
        }
    }

    Memory_free((*c).data);

    (*c).data = next;

    (*c).capacity = (u32)newCap;

    (*c).head = 0;

    return 1;
}

Deque *Deque_2(u32 elementClass, bytes capacity) {
    return Deque_3(elementClass, capacity, 0);
}

fn Deque_free(Deque *deque) {
    if (!deque) 
       return;

    Collection *c = asCollection(deque);

    if ((*c).data) 
        Memory_free((*c).data);

    Memory_free(deque);
}

fn Deque_addFirst(Deque *deque, u64 valueOrPointer) {
    if (!deque) 
       return;

    Collection *c = asCollection(deque);

    if (!ensureCapacity(c)) 
        return;

    bytes newHead = ((*c).head - 1 + (*c).capacity) % (*c).capacity;

    u8 *slot = (*c).data + newHead * (*c).stride;

    switch ((*c).stride) {
        case 1:  *(u8*) slot = (u8)valueOrPointer;  break;
        case 2:  *(u16*) slot = (u16)valueOrPointer; break;
        case 4:  *(u32*) slot = (u32)valueOrPointer; break;
        default: *(u64*) slot = valueOrPointer;          break;
    }

    (*c).head = (u32)newHead;

    (*c).activeCount++;
}

fn Deque_addLast(Deque *deque, u64 valueOrPointer) {
    if (!deque) 
       return;

    Collection *c = asCollection(deque);

    if (!ensureCapacity(c)) 
        return;

    bytes tail = ((*c).head + (*c).activeCount) % (*c).capacity;

    Collection_writeSlot(c, tail, valueOrPointer);

    (*c).activeCount++;
}

u64 Deque_removeFirst(Deque *deque) {
    if (!deque) 
       return 0;

    Collection *c = asCollection(deque);

    if ((*c).activeCount == 0) 
        return 0;

    u64 value = Collection_readSlot(c, (*c).head);

    (*c).head = (u32)(((*c).head + 1) % (*c).capacity);

    (*c).activeCount--;

    return value;
}

u64 Deque_removeLast(Deque *deque) {
    if (!deque) 
       return 0;

    Collection *c = asCollection(deque);

    if ((*c).activeCount == 0) 
        return 0;

    bytes tail = ((*c).head + (*c).activeCount - 1) % (*c).capacity;

    u64 value = Collection_readSlot(c, tail);

    (*c).activeCount--;

    return value;
}

u64 Deque_peekFirst(Deque *deque) {
    if (!deque) 
       return 0;

    Collection *c = asCollection(deque);

    if ((*c).activeCount == 0) 
        return 0;

    return Collection_readSlot(c, (*c).head);
}

u64 Deque_peekLast(Deque *deque) {
    if (!deque) 
       return 0;

    Collection *c = asCollection(deque);

    if ((*c).activeCount == 0) 
        return 0;

    bytes tail = ((*c).head + (*c).activeCount - 1) % (*c).capacity;

    return Collection_readSlot(c, tail);
}

u64 Deque_get(Deque *deque, bytes index) {
    if (!deque) 
       return 0;

    Collection *c = asCollection(deque);

    if (index >= (*c).activeCount) 
        return 0;

    bytes phys = ((*c).head + index) % (*c).capacity;

    return Collection_readSlot(c, phys);
}

u8 *Deque_slot(Deque *deque, bytes index) {
    if (!deque) 
       return nullptr;

    Collection *c = asCollection(deque);

    if (index >= (*c).activeCount) 
        return nullptr;

    bytes phys = ((*c).head + index) % (*c).capacity;

    return (*c).data + phys * (*c).stride;
}

bool Deque_isEmpty(Deque *deque) {
    return Collection_isEmpty(asCollection(deque));
}

bytes Deque_size(Deque *deque) {
    return Collection_size(asCollection(deque));
}

;;TEST
u64 Deque_exhaustionCount(const Deque *deque) {
    return Collection_exhaustionCount((const Collection*) deque);
}

bytes Deque_length(Deque *deque) {
    return Collection_length(asCollection(deque));
}

bytes Deque_capacity(Deque *deque) {
    return Collection_capacity(asCollection(deque));
}

u32 Deque_elementClassId(Deque *deque) {
    return Collection_elementClassId(asCollection(deque));
}

bytes Deque_stride(Deque *deque) {
    return Collection_stride(asCollection(deque));
}

bytes Deque_head(Deque *deque) {
    return Collection_head(asCollection(deque));
}

u8 *Deque_dataBuffer(Deque *deque) {
    return Collection_dataBuffer(asCollection(deque));
}

Deque *Deque_1(u32 element_class) {
    return Deque_2(element_class, 16);
}
