#ifndef STRUCT_DEQUE_H
#define STRUCT_DEQUE_H

#include "vex.h"
#include "struct/collection.h"

// struct/deque.h — the Deque class, ported from struct/Deque.java.
// d64-ended circular buffer over a stride-based buffer; the head field
// tracks the front so addFirst/removeFirst wrap around the ring.

typedef struct Deque {
    Collection collection;
} Deque;

Deque *Deque_2(u32 element_class, bytes capacity);

// Deque pre-filled to count slots (legacy allocate): active_count = count.
Deque *Deque_3(u32 element_class, bytes capacity, bytes count);

fn Deque_free(Deque *deque);

fn Deque_addFirst(Deque *deque, u64 value_or_pointer);
fn Deque_addLast(Deque *deque, u64 value_or_pointer);
u64 Deque_removeFirst(Deque *deque);
u64 Deque_removeLast(Deque *deque);
u64 Deque_peekFirst(Deque *deque);
u64 Deque_peekLast(Deque *deque);
u64 Deque_get(Deque *deque, bytes index);
u8 *Deque_slot(Deque *deque, bytes index);

bool Deque_isEmpty(Deque *deque);
bytes Deque_size(Deque *deque);
// Refused-grow count since construction (the Exhaustion Loudness Law).
;;TEST
u64 Deque_exhaustionCount(const Deque *deque);
bytes Deque_length(Deque *deque);
bytes Deque_capacity(Deque *deque);
u32 Deque_elementClassId(Deque *deque);
bytes Deque_stride(Deque *deque);
bytes Deque_head(Deque *deque);
u8 *Deque_dataBuffer(Deque *deque);


Deque *Deque_1(u32 element_class);

#define Deque(...) CONSTRUCTOR_DISPATCH(Deque, __VA_ARGS__)

#endif