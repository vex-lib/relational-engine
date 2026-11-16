#include "vex.h"
#include "nio/mem.h"

#include "struct/stack.h"
#include "struct/stride.h"
#include "type/re_type.h"
#include "struct/exhaustion.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Stack
 * ============================================================================
 * LIFO stack ported from struct/Stack.java: embeds the Collection mirror as
 * its first member (a Stack* is a Collection*) and grows its single data
 * buffer by doubling when full, copying live entries into the new arena block.
 * Push/pop/peek/slot ride Collection_readSlot / Collection_writeSlot; capacity
 * floors at 1024. Doubling matches the sibling containers and keeps the total
 * bump allocated across growths proportional to the final size, because freed
 * bump storage is reclaimed only wholesale. Lives at R2 as a leaf container
 * behavior.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Stack (struct/stack.c)
 * ============================================================================
 * the Stack class, ported from struct/Stack.java. LIFO over a
 *
 * STRUCT FIELDS (Mirroring struct/stack.h):
 * ----------------------------------------------------------------------------
 *   Stack {
 *     Collection collection; // instance state
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Stack_2(element_class, capacity)
 *   - Stack_1(element_class)
 *   - Stack(element_class, capacity, count)
 *
 * Core Functions:
 *   - Stack_free(stack)
 *   - Stack_push(stack, value_or_pointer)
 *   - Stack_pushTry(stack, value_or_pointer)
 *   - Stack_pop(stack)
 *   - Stack_peek(stack)
 *   - Stack_slot(stack, index)
 *   - Stack_size(stack)
 *   - Stack_length(stack)
 *   - Stack_capacity(stack)
 *   - Stack_elementClassId(stack)
 *   - Stack_stride(stack)
 *   - Stack_dataBuffer(stack)
 *
 * Getters:
 *   - Stack_isEmpty(stack)
 * ============================================================================
 */


// stack.c — Stack port (Legacy: struct/Stack.java).

static const bytes DEFAULT_CAPACITY = 1024;

static Collection *asCollection(Stack *stack) {
    return (Collection*) stack;
}

Stack *Stack_3(u32 elementClass, bytes capacity, bytes count) {
    bytes stride = Stride_get(elementClass);

    bytes cap = capacity < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : capacity;

    Stack *stack = (Stack*) Memory_alloc(TYPE_STACK, sizeof(Stack));

    if (!stack) {
        (discard) Struct_reportConstructionExhaustion("stack", sizeof(Stack));

        return nullptr;
    }

    Collection *c = asCollection(stack);

    (*c).typeId = TYPE_STACK;

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
        (discard) Struct_reportConstructionExhaustion("stack", Bytes);

        Memory_free(stack);

        return nullptr;
    }

    memset((*c).data, 0, Bytes);

    return stack;
}

Stack *Stack_2(u32 elementClass, bytes capacity) {
    return Stack_3(elementClass, capacity, 0);
}

fn Stack_free(Stack *stack) {
    if (!stack) 
       return;

    Collection *c = asCollection(stack);

    if ((*c).data) 
        Memory_free((*c).data);

    Memory_free(stack);
}

fn Stack_push(Stack *stack, u64 valueOrPointer) {
    // Family-consistent fn entry point, but not silent: a refused element is
    // reported by Stack_pushTry (THROW + a named TryCode).
    (discard) Stack_pushTry(stack, valueOrPointer);
}

TryValue Stack_pushTry(Stack *stack, u64 valueOrPointer) {
    if (!stack) {
        THROW("stack pushTry: null stack");

        return TryValue_error(TRY_NULL_ARG);
    }

    Collection *c = asCollection(stack);

    if ((*c).activeCount >= (*c).capacity) {
        // d64, like the sibling containers (Deque/Queue/List). Growing by a
        // fixed step allocates a fresh buffer on every growth, and freed bump
        // storage is reclaimed only wholesale -- so a large push run exhausts
        // the arena i64 before the buffer fits (the No Hardcoding Law).
        bytes newCap = (*c).capacity ? (bytes)(*c).capacity * 2 : DEFAULT_CAPACITY;

        if (newCap > UINT32_MAX) {
            THROW("stack pushTry: capacity %zu would exceed uint32", newCap);

            return TryValue_error(TRY_OVERFLOW);
        }

        bytes Bytes = newCap * (*c).stride;

        u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, (*c).elementClass);

        u8 *next = (u8*) Memory_alloc(bufType, Bytes);

        if (!next) {
            // Shared loud, counted refusal (the Exhaustion Loudness Law); the
            // Try carries the machine-readable reason.
            (discard) Collection_reportExhaustion(c, Bytes);

            return TryValue_error(TRY_NO_MEMORY);
        }

        memcpy(next, (*c).data, (*c).activeCount * (*c).stride);

        Memory_free((*c).data);

        (*c).data = next;

        (*c).capacity = (u32)newCap;
    }

    Collection_writeSlot(c, (*c).activeCount, valueOrPointer);

    (*c).activeCount++;

    return TryValue_ok(valueOrPointer);
}

u64 Stack_pop(Stack *stack) {
    if (!stack) 
       return 0;

    Collection *c = asCollection(stack);

    if ((*c).activeCount == 0) 
        return 0;

    (*c).activeCount--;

    return Collection_readSlot(c, (*c).activeCount);
}

u64 Stack_peek(Stack *stack) {
    if (!stack) 
       return 0;

    Collection *c = asCollection(stack);

    if ((*c).activeCount == 0) 
        return 0;

    return Collection_readSlot(c, (*c).activeCount - 1);
}

u8 *Stack_slot(Stack *stack, bytes index) {
    if (!stack) 
       return nullptr;

    Collection *c = asCollection(stack);

    if (index >= (*c).activeCount) 
        return nullptr;

    return (*c).data + index * (*c).stride;
}

bool Stack_isEmpty(Stack *stack) {
    return Collection_isEmpty(asCollection(stack));
}

bytes Stack_size(Stack *stack) {
    return Collection_size(asCollection(stack));
}

;;TEST
u64 Stack_exhaustionCount(const Stack *stack) {
    return Collection_exhaustionCount((const Collection*) stack);
}

bytes Stack_length(Stack *stack) {
    return Collection_length(asCollection(stack));
}

bytes Stack_capacity(Stack *stack) {
    return Collection_capacity(asCollection(stack));
}

u32 Stack_elementClassId(Stack *stack) {
    return Collection_elementClassId(asCollection(stack));
}

bytes Stack_stride(Stack *stack) {
    return Collection_stride(asCollection(stack));
}

;;TEST
u8 *Stack_dataBuffer(Stack *stack) {
    return Collection_dataBuffer(asCollection(stack));
}

Stack *Stack_1(u32 element_class) {
    return Stack_2(element_class, 16);
}
