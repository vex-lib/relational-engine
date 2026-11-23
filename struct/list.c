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


/** list.c, List port (Legacy: struct/List.java). */

static const bytes DEFAULT_CAPACITY = 1024;

static Collection *asCollection(List *list) {
    return (Collection*) list;
}

;;SETTER
static inline fn Collection_setTypeId(Collection *c, u64 typeId)
{
    (*c).typeId = typeId;
}
;;GETTER
static inline u32 Collection_activeCount(const Collection *c)
{
    return (*c).activeCount;
}
;;SETTER
static inline fn Collection_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}
;;GETTER
static inline u32 Collection_elementClass(const Collection *c)
{
    return (*c).elementClass;
}
;;SETTER
static inline fn Collection_setElementClass(Collection *c, u32 cls)
{
    (*c).elementClass = cls;
}
;;GETTER
static inline u32 Collection_strideField(const Collection *c)
{
    return (*c).stride;
}
;;SETTER
static inline fn Collection_setStride(Collection *c, u32 stride)
{
    (*c).stride = stride;
}
;;GETTER
static inline u32 Collection_capacityField(const Collection *c)
{
    return (*c).capacity;
}
;;SETTER
static inline fn Collection_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}
;;SETTER
static inline fn Collection_setHead(Collection *c, u32 head)
{
    (*c).head = head;
}
;;SETTER
static inline fn Collection_setExhaustionCount(Collection *c, u64 count)
{
    (*c).exhaustionCount = count;
}
;;SETTER
static inline fn Collection_setExhaustionReported(Collection *c, bool rep)
{
    (*c).exhaustionReported = rep;
}
;;GETTER
static inline u8 *Collection_data(const Collection *c)
{
    return (*c).data;
}
;;SETTER
static inline fn Collection_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

static u8 *bufferGrow(Collection *c, bytes needed) {
    u32 curCap = Collection_capacityField(c);
    bytes newCap = curCap ? curCap : DEFAULT_CAPACITY;

    while (newCap < needed) 
        newCap *= 2;

    u32 stride = Collection_strideField(c);
    bytes Bytes = newCap * stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, Collection_elementClass(c));

    u8 *next = (u8*) Memory_alloc(bufType, Re_arena(), Bytes);

    if (!next) {
        Collection_reportExhaustion(c, Bytes);

        return nullptr;
    }

    u8 *oldData = Collection_data(c);
    bytes oldBytes = Collection_activeCount(c) * stride;

    memcpy(next, oldData, oldBytes);

    Memory_free(oldData);

    Collection_setData(c, next);

    Collection_setCapacity(c, (u32)newCap);

    return next;
}

bool List_initStride(List *self, u32 elementClass, bytes stride, bytes capacity) {
    if (!self) 
       return false;

    bytes cap = capacity < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : capacity;

    Collection *c = asCollection(self);

    Collection_setTypeId(c, TYPE_LIST);

    Collection_setActiveCount(c, 0);

    Collection_setElementClass(c, elementClass);

    Collection_setStride(c, (u32)stride);

    Collection_setCapacity(c, (u32)cap);

    Collection_setHead(c, 0);

    Collection_setExhaustionCount(c, 0);

    Collection_setExhaustionReported(c, false);
    Collection_setData(c, nullptr);

    bytes Bytes = cap * stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, elementClass);

    u8 *data = (u8*) Memory_alloc(bufType, Re_arena(), Bytes);

    if (!data) {
        (discard) Struct_reportConstructionExhaustion("list", Bytes);

        return false;
    }

    memset(data, 0, Bytes);

    Collection_setData(c, data);

    return true;
}

bool List_init(List *self, u32 elementClass, bytes capacity) {
    return List_initStride(self, elementClass, Stride_get(elementClass), capacity);
}

List *List_3(u32 elementClass, bytes capacity, bytes count) {
    List *list = (List*) Memory_alloc(TYPE_LIST, Re_arena(), sizeof(List));

    if (!list) {
        (discard) Struct_reportConstructionExhaustion("list", sizeof(List));

        return nullptr;
    }

    if (!List_init(list, elementClass, capacity)) {
        Memory_free(list);

        return nullptr;
    }
    Collection *c = asCollection(list);

    Collection_setActiveCount(c, (u32)count);

    return list;
}

List *List_2(u32 elementClass, bytes capacity) {
    return List_3(elementClass, capacity, 0);
}

fn List_free(List *list) {
    if (!list) 
       return;

    Collection *c = asCollection(list);
    u8 *data = Collection_data(c);
    if (data) 
        Memory_free(data);

    Memory_free(list);
}

fn List_add(List *list, u64 valueOrPointer) {
    if (!list) 
       return;

    Collection *c = asCollection(list);
    u32 activeCount = Collection_activeCount(c);

    if (activeCount >= Collection_capacityField(c)) {
        if (!bufferGrow(c, activeCount + 1)) 
            return;
    }

    Collection_writeSlot(c, activeCount, valueOrPointer);

    Collection_setActiveCount(c, activeCount + 1u);
}

u8 *List_addSlot(List *list) {
    if (!list) 
       return nullptr;

    Collection *c = asCollection(list);
    u32 activeCount = Collection_activeCount(c);

    if (activeCount >= Collection_capacityField(c)) {
        if (!bufferGrow(c, activeCount + 1)) 
            return nullptr;
    }

    u32 stride = Collection_strideField(c);
    u8 *slot = Collection_data(c) + activeCount * stride;

    memset(slot, 0, stride);

    Collection_setActiveCount(c, activeCount + 1u);

    return slot;
}

u64 List_get(List *list, bytes index) {
    if (!list) 
       return 0;

    Collection *c = asCollection(list);

    if (index >= Collection_activeCount(c)) 
        return 0;

    return Collection_readSlot(c, index);
}

fn List_set(List *list, bytes index, u64 value) {
    if (!list) 
       return;

    Collection *c = asCollection(list);

    if (index >= Collection_activeCount(c)) 
        return;

    Collection_writeSlot(c, index, value);
}

u8 *List_slot(List *list, bytes index) {
    if (!list) 
       return nullptr;

    Collection *c = asCollection(list);

    if (index >= Collection_activeCount(c)) 
        return nullptr;

    return Collection_data(c) + index * Collection_strideField(c);
}

fn List_remove(List *list, bytes index) {
    if (!list) 
       return;

    Collection *c = asCollection(list);
    u32 activeCount = Collection_activeCount(c);

    if (index >= activeCount) 
        return;

    bytes tail = activeCount - index - 1;
    u32 stride = Collection_strideField(c);
    u8 *data = Collection_data(c);

    if (tail > 0) {
        memmove(data + index * stride,
                data + (index + 1) * stride,
                tail * stride);
    }

    Collection_setActiveCount(c, activeCount - 1u);
}

bool List_compare(List *a, List *b) {
    if (a == b) 
       return true;

    if (!a || !b) 
       return false;

    Collection *ca = asCollection(a);

    Collection *cb = asCollection(b);

    u32 activeA = Collection_activeCount(ca);
    if (activeA != Collection_activeCount(cb)) 
        return false;

    u32 strideA = Collection_strideField(ca);
    if (strideA != Collection_strideField(cb)) 
        return false;

    bytes Bytes = activeA * strideA;

    return memcmp(Collection_data(ca), Collection_data(cb), Bytes) == 0;
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
