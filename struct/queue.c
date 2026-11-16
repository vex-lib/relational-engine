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


// queue.c — Queue port (Legacy: struct/Queue.java).

static const bytes DEFAULT_CAPACITY = 1024;

static Collection *asCollection(Queue *queue) {
    return (Collection*) queue;
}

Queue *Queue_3(u32 elementClass, bytes capacity, bytes count) {
    bytes stride = Stride_get(elementClass);

    bytes cap = capacity < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : capacity;

    Queue *queue = (Queue*) Memory_alloc(TYPE_QUEUE, sizeof(Queue));

    if (!queue) {
        (discard) Struct_reportConstructionExhaustion("queue", sizeof(Queue));

        return nullptr;
    }

    Collection *c = asCollection(queue);

    (*c).typeId = TYPE_QUEUE;

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
        (discard) Struct_reportConstructionExhaustion("queue", Bytes);

        Memory_free(queue);

        return nullptr;
    }

    memset((*c).data, 0, Bytes);

    return queue;
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

Queue *Queue_2(u32 elementClass, bytes capacity) {
    return Queue_3(elementClass, capacity, 0);
}

fn Queue_free(Queue *queue) {
    if (!queue) 
       return;

    Collection *c = asCollection(queue);

    if ((*c).data) 
        Memory_free((*c).data);

    Memory_free(queue);
}

fn Queue_push(Queue *queue, u64 valueOrPointer) {
    if (!queue) 
       return;

    Collection *c = asCollection(queue);

    if (!ensureCapacity(c)) 
        return;

    bytes tail = ((*c).head + (*c).activeCount) % (*c).capacity;

    Collection_writeSlot(c, tail, valueOrPointer);

    (*c).activeCount++;
}

u64 Queue_pop(Queue *queue) {
    if (!queue) 
       return 0;

    Collection *c = asCollection(queue);

    if ((*c).activeCount == 0) 
        return 0;

    u64 value = Collection_readSlot(c, (*c).head);

    (*c).head = (u32)(((*c).head + 1) % (*c).capacity);

    (*c).activeCount--;

    return value;
}

u64 Queue_peek(Queue *queue) {
    if (!queue) 
       return 0;

    Collection *c = asCollection(queue);

    if ((*c).activeCount == 0) 
        return 0;

    return Collection_readSlot(c, (*c).head);
}

u8 *Queue_slot(Queue *queue, bytes index) {
    if (!queue) 
       return nullptr;

    Collection *c = asCollection(queue);

    if (index >= (*c).activeCount) 
        return nullptr;

    bytes phys = ((*c).head + index) % (*c).capacity;

    return (*c).data + phys * (*c).stride;
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
