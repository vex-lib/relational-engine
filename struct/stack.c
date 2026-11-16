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

    Collection_setTypeId(c, TYPE_STACK);

    Collection_setActiveCount(c, (u32)count);

    Collection_setElementClass(c, elementClass);

    Collection_setStride(c, (u32)stride);

    Collection_setCapacity(c, (u32)cap);

    Collection_setHead(c, 0);

    Collection_setExhaustionCount(c, 0);

    Collection_setExhaustionReported(c, false);

    bytes Bytes = cap * stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, elementClass);

    u8 *data = (u8*) Memory_alloc(bufType, Bytes);
    Collection_setData(c, data);

    if (!data) {
        (discard) Struct_reportConstructionExhaustion("stack", Bytes);

        Memory_free(stack);

        return nullptr;
    }

    memset(data, 0, Bytes);

    return stack;
}

Stack *Stack_2(u32 elementClass, bytes capacity) {
    return Stack_3(elementClass, capacity, 0);
}

fn Stack_free(Stack *stack) {
    if (!stack) 
       return;

    Collection *c = asCollection(stack);
    u8 *data = Collection_data(c);
    if (data) 
        Memory_free(data);

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
    u32 activeCount = Collection_activeCount(c);
    u32 cap = Collection_capacityField(c);
    u32 stride = Collection_strideField(c);

    if (activeCount >= cap) {
        // d64, like the sibling containers (Deque/Queue/List). Growing by a
        // fixed step allocates a fresh buffer on every growth, and freed bump
        // storage is reclaimed only wholesale -- so a large push run exhausts
        // the arena i64 before the buffer fits (the No Hardcoding Law).
        bytes newCap = cap ? (bytes)cap * 2 : DEFAULT_CAPACITY;

        if (newCap > UINT32_MAX) {
            THROW("stack pushTry: capacity %zu would exceed uint32", newCap);

            return TryValue_error(TRY_OVERFLOW);
        }

        bytes Bytes = newCap * stride;

        u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, Collection_elementClass(c));

        u8 *next = (u8*) Memory_alloc(bufType, Bytes);

        if (!next) {
            // Shared loud, counted refusal (the Exhaustion Loudness Law); the
            // Try carries the machine-readable reason.
            (discard) Collection_reportExhaustion(c, Bytes);

            return TryValue_error(TRY_NO_MEMORY);
        }

        u8 *oldData = Collection_data(c);
        memcpy(next, oldData, activeCount * stride);

        Memory_free(oldData);

        Collection_setData(c, next);

        Collection_setCapacity(c, (u32)newCap);
    }

    Collection_writeSlot(c, activeCount, valueOrPointer);

    Collection_setActiveCount(c, activeCount + 1u);

    return TryValue_ok(valueOrPointer);
}

u64 Stack_pop(Stack *stack) {
    if (!stack) 
       return 0;

    Collection *c = asCollection(stack);
    u32 activeCount = Collection_activeCount(c);

    if (activeCount == 0) 
        return 0;

    activeCount--;
    Collection_setActiveCount(c, activeCount);

    return Collection_readSlot(c, activeCount);
}

u64 Stack_peek(Stack *stack) {
    if (!stack) 
       return 0;

    Collection *c = asCollection(stack);
    u32 activeCount = Collection_activeCount(c);

    if (activeCount == 0) 
        return 0;

    return Collection_readSlot(c, activeCount - 1);
}

u8 *Stack_slot(Stack *stack, bytes index) {
    if (!stack) 
       return nullptr;

    Collection *c = asCollection(stack);

    if (index >= Collection_activeCount(c)) 
        return nullptr;

    return Collection_data(c) + index * Collection_strideField(c);
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
