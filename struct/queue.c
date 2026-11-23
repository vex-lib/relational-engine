#include "vex.h"
#include "nio/mem.h"

#include "struct/queue.h"
#include "struct/stride.h"
#include "type/re_type.h"
#include "struct/exhaustion.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Queue
 * ============================================================================
 * The Queue class, ported from struct/Queue.java: a circular FIFO over a
 * Collection header, with a 1024-slot default capacity floor and growth by
 * 1024-slot increments when full. Push appends at the logical tail,
 * pop/peek read the head; slot access maps logical indices through the
 * circular head. Free releases the data buffer before the header. Lives at
 * R2 as a leaf container behavior.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Queue (struct/queue.c)
 * ============================================================================
 * the Queue class, ported from struct/Queue.java.
 *
 * STRUCT FIELDS (Mirroring struct/queue.h):
 * ----------------------------------------------------------------------------
 *   Queue {
 *     Collection collection; // instance state
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Queue_2(element_class, capacity)
 *   - Queue_1(element_class)
 *   - Queue(element_class, capacity, count)
 *
 * Core Functions:
 *   - Queue_free(queue)
 *   - Queue_push(queue, value_or_pointer)
 *   - Queue_pop(queue)
 *   - Queue_peek(queue)
 *   - Queue_slot(queue, index)
 *   - Queue_size(queue)
 *   - Queue_length(queue)
 *   - Queue_capacity(queue)
 *   - Queue_elementClassId(queue)
 *   - Queue_stride(queue)
 *   - Queue_head(queue)
 *   - Queue_dataBuffer(queue)
 *
 * Getters:
 *   - Queue_isEmpty(queue)
 * ============================================================================
 */


/** queue.c, Queue port (Legacy: struct/Queue.java). */

static const bytes DEFAULT_CAPACITY = 1024;

static Collection *asCollection(Queue *queue) {
    return (Collection*) queue;
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
;;GETTER
static inline u32 Collection_headField(const Collection *c)
{
    return (*c).head;
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

Queue *Queue_3(u32 elementClass, bytes capacity, bytes count) {
    bytes stride = Stride_get(elementClass);

    bytes cap = capacity < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : capacity;

    Queue *queue = (Queue*) Memory_alloc(TYPE_QUEUE, Re_arena(), sizeof(Queue));

    if (!queue) {
        (discard) Struct_reportConstructionExhaustion("queue", sizeof(Queue));

        return nullptr;
    }

    Collection *c = asCollection(queue);

    Collection_setTypeId(c, TYPE_QUEUE);

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
        (discard) Struct_reportConstructionExhaustion("queue", Bytes);

        Memory_free(queue);

        return nullptr;
    }

    memset(data, 0, Bytes);

    return queue;
}

static i32 ensureCapacity(Collection *c) {
    u32 cap = Collection_capacityField(c);
    u32 activeCount = Collection_activeCount(c);
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

Queue *Queue_2(u32 elementClass, bytes capacity) {
    return Queue_3(elementClass, capacity, 0);
}

fn Queue_free(Queue *queue) {
    if (!queue) 
       return;

    Collection *c = asCollection(queue);
    u8 *data = Collection_data(c);
    if (data) 
        Memory_free(data);

    Memory_free(queue);
}

fn Queue_push(Queue *queue, u64 valueOrPointer) {
    if (!queue) 
       return;

    Collection *c = asCollection(queue);

    if (!ensureCapacity(c)) 
        return;

    u32 head = Collection_headField(c);
    u32 activeCount = Collection_activeCount(c);
    u32 cap = Collection_capacityField(c);
    bytes tail = (head + activeCount) % cap;

    Collection_writeSlot(c, tail, valueOrPointer);

    Collection_setActiveCount(c, activeCount + 1u);
}

u64 Queue_pop(Queue *queue) {
    if (!queue) 
       return 0;

    Collection *c = asCollection(queue);
    u32 activeCount = Collection_activeCount(c);

    if (activeCount == 0) 
        return 0;

    u32 head = Collection_headField(c);
    u64 value = Collection_readSlot(c, head);

    Collection_setHead(c, (u32)((head + 1) % Collection_capacityField(c)));

    Collection_setActiveCount(c, activeCount - 1u);

    return value;
}

u64 Queue_peek(Queue *queue) {
    if (!queue) 
       return 0;

    Collection *c = asCollection(queue);

    if (Collection_activeCount(c) == 0) 
        return 0;

    return Collection_readSlot(c, Collection_headField(c));
}

u8 *Queue_slot(Queue *queue, bytes index) {
    if (!queue) 
       return nullptr;

    Collection *c = asCollection(queue);

    if (index >= Collection_activeCount(c)) 
        return nullptr;

    bytes phys = (Collection_headField(c) + index) % Collection_capacityField(c);

    return Collection_data(c) + phys * Collection_strideField(c);
}

bool Queue_isEmpty(Queue *queue) {
    return Collection_isEmpty(asCollection(queue));
}

bytes Queue_size(Queue *queue) {
    return Collection_size(asCollection(queue));
}

;;TEST
u64 Queue_exhaustionCount(const Queue *queue) {
    return Collection_exhaustionCount((const Collection*) queue);
}

bytes Queue_length(Queue *queue) {
    return Collection_length(asCollection(queue));
}

bytes Queue_capacity(Queue *queue) {
    return Collection_capacity(asCollection(queue));
}

u32 Queue_elementClassId(Queue *queue) {
    return Collection_elementClassId(asCollection(queue));
}

bytes Queue_stride(Queue *queue) {
    return Collection_stride(asCollection(queue));
}

bytes Queue_head(Queue *queue) {
    return Collection_head(asCollection(queue));
}

u8 *Queue_dataBuffer(Queue *queue) {
    return Collection_dataBuffer(asCollection(queue));
}

Queue *Queue_1(u32 element_class) {
    return Queue_2(element_class, 16);
}
