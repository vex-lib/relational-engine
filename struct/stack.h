#ifndef STRUCT_STACK_H
#define STRUCT_STACK_H

#include "vex.h"
#include "struct/collection.h"
#include "exception/try_value.h"

// struct/stack.h — the Stack class, ported from struct/Stack.java. LIFO over a
// stride-based buffer that grows in chunks.

typedef struct Stack {
    Collection collection;
} Stack;

Stack *Stack_2(u32 element_class, bytes capacity);

// Stack pre-filled to count slots (legacy allocate): active_count = count.
Stack *Stack_3(u32 element_class, bytes capacity, bytes count);

fn Stack_free(Stack *stack);

// fn for family consistency, but never silent: a growth whose arena
// allocation fails emits its THROW diagnostic and refuses the element.
// Callers that need the reason use Stack_pushTry.
fn Stack_push(Stack *stack, u64 value_or_pointer);

// `;;CHECKER` half of the pair; Stack_push is the `;;HOTCODE` half. Returns the
// pushed value on TRY_OK, or the named reason: TRY_NULL_ARG, TRY_OVERFLOW
// (capacity would exceed uint32) or TRY_NO_MEMORY (arena could not grow). A
// rejected push leaves the stack unchanged and still THROWs.
TryValue Stack_pushTry(Stack *stack, u64 value_or_pointer);

u64 Stack_pop(Stack *stack);
u64 Stack_peek(Stack *stack);
u8 *Stack_slot(Stack *stack, bytes index);
bool Stack_isEmpty(Stack *stack);
bytes Stack_size(Stack *stack);
// Refused-grow count since construction (the Exhaustion Loudness Law).
;;TEST
u64 Stack_exhaustionCount(const Stack *stack);
bytes Stack_length(Stack *stack);
bytes Stack_capacity(Stack *stack);
u32 Stack_elementClassId(Stack *stack);
bytes Stack_stride(Stack *stack);
// Borrowed view of the backing buffer; used by tests to verify the block, not
// needed by production code.
;;TEST
u8 *Stack_dataBuffer(Stack *stack);


Stack *Stack_1(u32 element_class);

#define Stack(...) CONSTRUCTOR_DISPATCH(Stack, __VA_ARGS__)

#endif