#ifndef STRUCT_QUEUE_H
#define STRUCT_QUEUE_H

#include "vex.h"
#include "struct/collection.h"

// struct/queue.h — the Queue class, ported from struct/Queue.java.
// FIFO circular buffer; push appends at the tail, pop removes from the head.

typedef struct Queue {
    Collection collection;
} Queue;

Queue *Queue_2(u32 element_class, bytes capacity);

// Queue pre-filled to count slots (legacy allocate): active_count = count.
Queue *Queue_3(u32 element_class, bytes capacity, bytes count);

fn Queue_free(Queue *queue);

fn Queue_push(Queue *queue, u64 value_or_pointer);
u64 Queue_pop(Queue *queue);
u64 Queue_peek(Queue *queue);
u8 *Queue_slot(Queue *queue, bytes index);

bool Queue_isEmpty(Queue *queue);
bytes Queue_size(Queue *queue);
// Refused-grow count since construction (the Exhaustion Loudness Law).
;;TEST
u64 Queue_exhaustionCount(const Queue *queue);
bytes Queue_length(Queue *queue);
bytes Queue_capacity(Queue *queue);
u32 Queue_elementClassId(Queue *queue);
bytes Queue_stride(Queue *queue);
bytes Queue_head(Queue *queue);
u8 *Queue_dataBuffer(Queue *queue);


Queue *Queue_1(u32 element_class);

#define Queue(...) CONSTRUCTOR_DISPATCH(Queue, __VA_ARGS__)

#endif