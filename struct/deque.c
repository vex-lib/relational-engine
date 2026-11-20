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

;;SETTER
static inline fn Collection_setTypeId(Collection *c, u64 typeId) { (*c).typeId = typeId; }
;;GETTER
static inline u32 Collection_activeCount(const Collection *c) { return (*c).activeCount; }
;;SETTER
static inline fn Collection_setActiveCount(Collection *c, u32 count) { (*c).activeCount = count; }
;;GETTER
static inline u32 Collection_elementClass(const Collection *c) { return (*c).elementClass; }
;;SETTER
static inline fn Collection_setElementClass(Collection *c, u32 cls) { (*c).elementClass = cls; }
;;GETTER
static inline u32 Collection_strideField(const Collection *c) { return (*c).stride; }
;;SETTER
static inline fn Collection_setStride(Collection *c, u32 stride) { (*c).stride = stride; }
;;GETTER
static inline u32 Collection_capacityField(const Collection *c) { return (*c).capacity; }
;;SETTER
static inline fn Collection_setCapacity(Collection *c, u32 cap) { (*c).capacity = cap; }
;;GETTER
static inline u32 Collection_headField(const Collection *c) { return (*c).head; }
;;SETTER
static inline fn Collection_setHead(Collection *c, u32 head) { (*c).head = head; }
;;SETTER
static inline fn Collection_setExhaustionCount(Collection *c, u64 count) { (*c).exhaustionCount = count; }
;;SETTER
static inline fn Collection_setExhaustionReported(Collection *c, bool rep) { (*c).exhaustionReported = rep; }
;;GETTER
static inline u8 *Collection_data(const Collection *c) { return (*c).data; }
;;SETTER
static inline fn Collection_setData(Collection *c, u8 *data) { (*c).data = data; }

Deque *Deque_3(u32 elementClass, bytes capacity, bytes count) {
    bytes stride = Stride_get(elementClass);

    bytes cap = capacity < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : capacity;

    Deque *deque = (Deque*) Memory_alloc(TYPE_DEQUE, Re_arena(), sizeof(Deque));

    if (!deque) {
        (discard) Struct_reportConstructionExhaustion("deque", sizeof(Deque));

        return nullptr;
    }

    Collection *c = asCollection(deque);

    Collection_setTypeId(c, TYPE_DEQUE);

    Collection_setActiveCount(c, (u32)count);

    Collection_setElementClass(c, elementClass);

    Collection_setStride(c, (u32)stride);

    Collection_setCapacity(c, (u32)cap);

    Collection_setHead(c, 0);

    Collection_setExhaustionCount(c, 0);

    Collection_setExhaustionReported(c, false);

    bytes Bytes = cap * stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, elementClass);

    u8 *data = (u8*) Memory_alloc(bufType, Re_arena(), Bytes);
    Collection_setData(c, data);

    if (!data) {
        (discard) Struct_reportConstructionExhaustion("deque", Bytes);

        Memory_free(deque);

        return nullptr;
    }

    memset(data, 0, Bytes);

    return deque;
}

static i32 ensureCapacity(Collection *c) {
    u32 activeCount = Collection_activeCount(c);
    u32 cap = Collection_capacityField(c);
    if (activeCount < cap) 
        return 1;

    bytes newCap = (bytes) cap * 2;
    u32 stride = Collection_strideField(c);

    bytes Bytes = newCap * stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, Collection_elementClass(c));

    u8 *next = (u8*) Memory_alloc(bufType, Re_arena(), Bytes);

    if (!next) {
        Collection_reportExhaustion(c, Bytes);

        return 0;
    }

    u8 *oldData = Collection_data(c);
    u32 head = Collection_headField(c);

    if (activeCount > 0) {
        if (head == 0) {
            memcpy(next, oldData, activeCount * stride);
        } else {
            bytes len1 = cap - head;

            bytes len2 = head;

            memcpy(next, oldData + head * stride, len1 * stride);

            memcpy(next + len1 * stride, oldData, len2 * stride);
        }
    }

    Memory_free(oldData);

    Collection_setData(c, next);

    Collection_setCapacity(c, (u32)newCap);

    Collection_setHead(c, 0);

    return 1;
}

Deque *Deque_2(u32 elementClass, bytes capacity) {
    return Deque_3(elementClass, capacity, 0);
}

fn Deque_free(Deque *deque) {
    if (!deque) 
       return;

    Collection *c = asCollection(deque);
    u8 *data = Collection_data(c);
    if (data) 
        Memory_free(data);

    Memory_free(deque);
}

fn Deque_addFirst(Deque *deque, u64 valueOrPointer) {
    if (!deque) 
       return;

    Collection *c = asCollection(deque);

    if (!ensureCapacity(c)) 
        return;

    u32 head = Collection_headField(c);
    u32 cap = Collection_capacityField(c);
    u32 stride = Collection_strideField(c);
    bytes newHead = (head - 1 + cap) % cap;

    u8 *slot = Collection_data(c) + newHead * stride;

    switch (stride) {
        case 1:  *(u8*) slot = (u8)valueOrPointer;  break;
        case 2:  *(u16*) slot = (u16)valueOrPointer; break;
        case 4:  *(u32*) slot = (u32)valueOrPointer; break;
        default: *(u64*) slot = valueOrPointer;          break;
    }

    Collection_setHead(c, (u32)newHead);

    Collection_setActiveCount(c, Collection_activeCount(c) + 1u);
}

fn Deque_addLast(Deque *deque, u64 valueOrPointer) {
    if (!deque) 
       return;

    Collection *c = asCollection(deque);

    if (!ensureCapacity(c)) 
        return;

    u32 activeCount = Collection_activeCount(c);
    bytes tail = (Collection_headField(c) + activeCount) % Collection_capacityField(c);

    Collection_writeSlot(c, tail, valueOrPointer);

    Collection_setActiveCount(c, activeCount + 1u);
}

u64 Deque_removeFirst(Deque *deque) {
    if (!deque) 
       return 0;

    Collection *c = asCollection(deque);
    u32 activeCount = Collection_activeCount(c);

    if (activeCount == 0) 
        return 0;

    u32 head = Collection_headField(c);
    u64 value = Collection_readSlot(c, head);

    Collection_setHead(c, (u32)((head + 1) % Collection_capacityField(c)));

    Collection_setActiveCount(c, activeCount - 1u);

    return value;
}

u64 Deque_removeLast(Deque *deque) {
    if (!deque) 
       return 0;

    Collection *c = asCollection(deque);
    u32 activeCount = Collection_activeCount(c);

    if (activeCount == 0) 
        return 0;

    bytes tail = (Collection_headField(c) + activeCount - 1) % Collection_capacityField(c);

    u64 value = Collection_readSlot(c, tail);

    Collection_setActiveCount(c, activeCount - 1u);

    return value;
}

u64 Deque_peekFirst(Deque *deque) {
    if (!deque) 
       return 0;

    Collection *c = asCollection(deque);

    if (Collection_activeCount(c) == 0) 
        return 0;

    return Collection_readSlot(c, Collection_headField(c));
}

u64 Deque_peekLast(Deque *deque) {
    if (!deque) 
       return 0;

    Collection *c = asCollection(deque);
    u32 activeCount = Collection_activeCount(c);

    if (activeCount == 0) 
        return 0;

    bytes tail = (Collection_headField(c) + activeCount - 1) % Collection_capacityField(c);

    return Collection_readSlot(c, tail);
}

u64 Deque_get(Deque *deque, bytes index) {
    if (!deque) 
       return 0;

    Collection *c = asCollection(deque);

    if (index >= Collection_activeCount(c)) 
        return 0;

    bytes phys = (Collection_headField(c) + index) % Collection_capacityField(c);

    return Collection_readSlot(c, phys);
}

u8 *Deque_slot(Deque *deque, bytes index) {
    if (!deque) 
       return nullptr;

    Collection *c = asCollection(deque);

    if (index >= Collection_activeCount(c)) 
        return nullptr;

    bytes phys = (Collection_headField(c) + index) % Collection_capacityField(c);

    return Collection_data(c) + phys * Collection_strideField(c);
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
