#include "vex.h"
#include "nio/mem.h"

#include <stdlib.h>

#include "struct/list.h"
#include "struct/stride.h"
#include "type/re_type.h"
#include "struct/exhaustion.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: List
 * ============================================================================
 * The List container over the Collection base: a flat, stride-strided data
 * buffer with activeCount/capacity/elementClass metadata, so element access
 * is a single base-plus-stride hop. The buffer starts at DEFAULT_CAPACITY
 * (1024) slots and doubles when full, arena-backed; add writes through
 * Collection_writeSlot, remove memmoves the tail down, and compare is a
 * byte-wise memcmp over the active region. All accessors are null-guarded and
 * index-bounded.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: List (struct/list.c)
 * ============================================================================
 * the List class, ported from struct/List.java.
 *
 * STRUCT FIELDS (Mirroring struct/list.h):
 * ----------------------------------------------------------------------------
 *   List {
 *     Collection collection; // instance state
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - List_init(self, element_class, capacity)
 *   - List_2(element_class, capacity)
 *   - List_1(element_class)
 *   - List_3(element_class, capacity, count)
 *
 * Core Functions:
 *   - List_free(list)
 *   - List_add(list, value_or_pointer)
 *   - List_addSlot(list)
 *   - List_slot(list, index)
 *   - List_remove(list, index)
 *   - List_compare(a, b.sh)
 *   - List_size(list)
 *   - List_length(list)
 *   - List_capacity(list)
 *   - List_elementClassId(list)
 *   - List_stride(list)
 *   - List_dataBuffer(list)
 *
 * Setters:
 *   - List_set(list, index, value)
 *
 * Getters:
 *   - List_get(list, index)
 *   - List_isEmpty(list)
 * ============================================================================
 */


// list.c — List port (Legacy: struct/List.java).

static const bytes DEFAULT_CAPACITY = 1024;

static Collection *asCollection(List *list) {
    return (Collection*) list;
}

static u8 *bufferGrow(Collection *c, bytes needed) {
    // d64, so growth is amortized O(1). A fixed increment makes "support a
    // million" quadratic (the Dynamic Scalability & Anti-Hardcoding Law).
    bytes newCap = (*c).capacity ? (*c).capacity : DEFAULT_CAPACITY;

    while (newCap < needed) 
        newCap *= 2;

    bytes Bytes = newCap * (*c).stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, (*c).elementClass);

    u8 *next = (u8*) Memory_alloc(bufType, Bytes);

    if (!next) {
        // Loud, counted refusal (the Exhaustion Loudness Law): the element is
        // not admitted and the caller can query Collection_exhaustionCount.
        Collection_reportExhaustion(c, Bytes);

        return nullptr;
    }

    bytes oldBytes = (*c).activeCount * (*c).stride;

    memcpy(next, (*c).data, oldBytes);

    Memory_free((*c).data);

    (*c).data = next;

    (*c).capacity = (u32)newCap;

    return next;
}

bool List_initStride(List *self, u32 elementClass, bytes stride, bytes capacity) {
    if (!self) 
       return false;

    bytes cap = capacity < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : capacity;

    Collection *c = asCollection(self);

    (*c).typeId = TYPE_LIST;

    (*c).activeCount = 0;

    (*c).elementClass = elementClass;

    (*c).stride = (u32)stride;

    (*c).capacity = (u32)cap;

    (*c).head = 0;

    (*c).exhaustionCount = 0;

    (*c).exhaustionReported = false;
    // Set before the allocation so a refused buffer still leaves a List_free-able
    // object: data == nullptr is the empty-buffer state.
    (*c).data = nullptr;

    bytes Bytes = cap * stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, elementClass);

    u8 *data = (u8*) Memory_alloc(bufType, Bytes);

    if (!data) {
        (discard) Struct_reportConstructionExhaustion("list", Bytes);

        return false;
    }

    memset(data, 0, Bytes);

    (*c).data = data;

    return true;
}

bool List_init(List *self, u32 elementClass, bytes capacity) {
    return List_initStride(self, elementClass, Stride_get(elementClass), capacity);
}

List *List_3(u32 elementClass, bytes capacity, bytes count) {
    List *list = (List*) Memory_alloc(TYPE_LIST, sizeof(List));

    if (!list) {
        (discard) Struct_reportConstructionExhaustion("list", sizeof(List));

        return nullptr;
    }

    if (!List_init(list, elementClass, capacity)) {
        Memory_free(list);

        return nullptr;
    }
    // Pre-filled means the slots exist and read as zero; the zeroing is
    // List_init's, so the count is the only extra step.
    Collection *c = asCollection(list);

    (*c).activeCount = (u32)count;

    return list;
}

List *List_2(u32 elementClass, bytes capacity) {
    return List_3(elementClass, capacity, 0);
}

fn List_free(List *list) {
    if (!list) 
       return;

    Collection *c = asCollection(list);

    if ((*c).data) 
        Memory_free((*c).data);

    Memory_free(list);
}

fn List_add(List *list, u64 valueOrPointer) {
    if (!list) 
       return;

    Collection *c = asCollection(list);

    if ((*c).activeCount >= (*c).capacity) {
        if (!bufferGrow(c, (*c).activeCount + 1)) 
            return;
    }

    Collection_writeSlot(c, (*c).activeCount, valueOrPointer);

    (*c).activeCount++;
}

u8 *List_addSlot(List *list) {
    if (!list) 
       return nullptr;

    Collection *c = asCollection(list);

    if ((*c).activeCount >= (*c).capacity) {
        if (!bufferGrow(c, (*c).activeCount + 1)) 
            return nullptr;
    }

    u8 *slot = (*c).data + (*c).activeCount * (*c).stride;

    memset(slot, 0, (*c).stride);

    (*c).activeCount++;

    return slot;
}

u64 List_get(List *list, bytes index) {
    if (!list) 
       return 0;

    Collection *c = asCollection(list);

    if (index >= (*c).activeCount) 
        return 0;

    return Collection_readSlot(c, index);
}

fn List_set(List *list, bytes index, u64 value) {
    if (!list) 
       return;

    Collection *c = asCollection(list);

    if (index >= (*c).activeCount) 
        return;

    Collection_writeSlot(c, index, value);
}

u8 *List_slot(List *list, bytes index) {
    if (!list) 
       return nullptr;

    Collection *c = asCollection(list);

    if (index >= (*c).activeCount) 
        return nullptr;

    return (*c).data + index * (*c).stride;
}

fn List_remove(List *list, bytes index) {
    if (!list) 
       return;

    Collection *c = asCollection(list);

    if (index >= (*c).activeCount) 
        return;

    bytes tail = (*c).activeCount - index - 1;

    if (tail > 0) {
        memmove((*c).data + index * (*c).stride,
                (*c).data + (index + 1) * (*c).stride,
                tail * (*c).stride);
    }

    (*c).activeCount--;
}

bool List_compare(List *a, List *b) {
    if (a == b) 
       return true;

    if (!a || !b) 
       return false;

    Collection *ca = asCollection(a);

    Collection *cb = asCollection(b);

    if ((*ca).activeCount != (*cb).activeCount) 
        return false;

    if ((*ca).stride != (*cb).stride) 
        return false;

    bytes Bytes = (*ca).activeCount * (*ca).stride;

    return memcmp((*ca).data, (*cb).data, Bytes) == 0;
}

bool List_isEmpty(List *list) {
    return Collection_isEmpty(asCollection(list));
}

bytes List_size(List *list) {
    return Collection_size(asCollection(list));
}

;;TEST
u64 List_exhaustionCount(const List *list) {
    return Collection_exhaustionCount((const Collection*) list);
}

bytes List_length(List *list) {
    return Collection_length(asCollection(list));
}

bytes List_capacity(List *list) {
    return Collection_capacity(asCollection(list));
}

u32 List_elementClassId(List *list) {
    return Collection_elementClassId(asCollection(list));
}

bytes List_stride(List *list) {
    return Collection_stride(asCollection(list));
}

u8 *List_dataBuffer(List *list) {
    return Collection_dataBuffer(asCollection(list));
}

List *List_1(u32 element_class) {
    return List_2(element_class, 16);
}
