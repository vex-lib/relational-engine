#ifndef STRUCT_STACK_H
#define STRUCT_STACK_H

#include "vex.h"
#include "exception/try_value.h"
#include "nio/mem.h"
#include "struct/collection.h"
#include "struct/layout.h"

/**
 * struct/stack.h, the polymorphic LIFO Stack interface with ArrayStack (default
 * contiguous buffer) and LinkedStack (32-byte-aligned arena node chain) backings.
 *
 * Stack *s = ArrayStack(Class);
 * Stack *l = LinkedStack(u32);
 * Stack_push(s, item);
 * Class *top = Stack_peek(s);
 * Class popped = Stack_popAs(s, Class);
 * Stack_free(s);
 */

#define ARRAY_STACK_DEFAULT_CAPACITY 16u

/**
 * Concrete Stack backing discriminator.
 *
 * STACK_KIND_UNKNOWN - uninitialized or invalid backing
 * STACK_KIND_ARRAY   - growable contiguous array buffer (ArrayStack)
 * STACK_KIND_LINKED  - singly-linked 32B arena nodes (LinkedStack)
 */
;;CLASSNAME("StackKind")
typedef enum StackKind {
    STACK_KIND_UNKNOWN = 0,
    STACK_KIND_ARRAY = 1,
    STACK_KIND_LINKED = 2,
} StackKind;

/**
 * 32-byte singly-linked node header preceding inline element payload bytes.
 *
 * struct StackNode *below - element immediately below this top node
 * u64 pad0                - alignment padding
 * u64 pad1                - alignment padding
 * u64 pad2                - alignment padding
 */
;;CLASSNAME("StackNode")
typedef struct StackNode {
    struct StackNode *below;
    u64 pad0;
    u64 pad1;
    u64 pad2;
} StackNode;

_Static_assert(sizeof(StackNode) == 32u, "StackNode header must be 32 bytes");

/**
 * Polymorphic LIFO Stack supporting both ArrayStack and LinkedStack backings.
 *
 * Collection collection - embedded Collection header (offset 0)
 * StackKind kind        - concrete backing discriminator
 * MemoryArena *arena    - borrowed MemoryArena owning allocations
 * ElementLayout layout  - compile-time-captured element layout
 * StackNode *topNode    - top node for LinkedStack (nullptr on ArrayStack)
 */
;;CLASSNAME("Stack")
typedef struct Stack {
    Collection collection;
    StackKind kind;
    MemoryArena *arena;
    ElementLayout layout;
    StackNode *topNode;
} Stack;

Stack *ArrayStack_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena);
Stack *LinkedStack_newLayout(ElementLayout layout, MemoryArena *arena);

#define ArrayStack_ctor_1(T)             ArrayStack_newLayout(ElementLayout_of(T), ARRAY_STACK_DEFAULT_CAPACITY, Re_arena())
#define ArrayStack_ctor_2(T, cap)        ArrayStack_newLayout(ElementLayout_of(T), (cap), Re_arena())
#define ArrayStack_ctor_3(T, cap, arena) ArrayStack_newLayout(ElementLayout_of(T), (cap), (arena))
#define ArrayStack(...)                  CONSTRUCTOR_DISPATCH(ArrayStack_ctor, __VA_ARGS__)

#define LinkedStack_ctor_1(T)            LinkedStack_newLayout(ElementLayout_of(T), Re_arena())
#define LinkedStack_ctor_2(T, arena)     LinkedStack_newLayout(ElementLayout_of(T), (arena))
#define LinkedStack(...)                 CONSTRUCTOR_DISPATCH(LinkedStack_ctor, __VA_ARGS__)

#define Stack(...)                       ArrayStack(__VA_ARGS__)

bool ArrayStack_isArrayStack(const Stack *self);
bool LinkedStack_isLinkedStack(const Stack *self);

bool Stack_pushBytes(Stack *self, const_any elem, bytes elemBytes);
TryValue Stack_pushBytesTry(Stack *self, const_any elem, bytes elemBytes);
any Stack_emplace(Stack *self);
any Stack_peek(const Stack *self);
bool Stack_peekCopyOut(const Stack *self, any dest, bytes elemBytes, string typeName);
bool Stack_pop(Stack *self);
bool Stack_popCopyOut(Stack *self, any dest, bytes elemBytes, string typeName);
any Stack_get(const Stack *self, bytes index);
bool Stack_copyOut(const Stack *self, bytes index, any dest, bytes elemBytes, string typeName);
fn Stack_clear(Stack *self);
bool Stack_reserve(Stack *self, bytes minCapacity);
fn Stack_free(Stack *self);

bytes Stack_count(const Stack *self);
bytes Stack_size(const Stack *self);
bytes Stack_length(const Stack *self);
bytes Stack_capacity(const Stack *self);
bool Stack_isEmpty(const Stack *self);
bytes Stack_stride(const Stack *self);
string Stack_typeName(const Stack *self);
StackKind Stack_kind(const Stack *self);
string Stack_kindName(const Stack *self);
MemoryArena *Stack_arena(const Stack *self);
ElementLayout Stack_layout(const Stack *self);

;;TEST
u64 Stack_exhaustionCount(const Stack *self);

fn Stack_toString(const Stack *self, utf8 *dest, bytes cap, bool *outTruncated);
fn Stack_toStringStruct(const Stack *self, utf8 *dest, bytes cap, bool *outTruncated);

#define Stack_push(stack, val)                                               \
    ({                                                                       \
        auto _vex_st_push_v = (val);                                         \
        Stack_pushBytes((stack), &_vex_st_push_v, sizeof(_vex_st_push_v));   \
    })

#define Stack_pushTry(stack, val)                                            \
    ({                                                                       \
        auto _vex_st_pt_v = (val);                                           \
        Stack_pushBytesTry((stack), &_vex_st_pt_v, sizeof(_vex_st_pt_v));   \
    })

#define Stack_peekAs(stack, T)                                               \
    ({                                                                       \
        typeof_unqual(T) _vex_st_pk_out;                                     \
        memset(&_vex_st_pk_out, 0, sizeof(_vex_st_pk_out));                  \
        (discard) Stack_peekCopyOut((stack), &_vex_st_pk_out,                \
                                    sizeof(_vex_st_pk_out), #T);             \
        _vex_st_pk_out;                                                      \
    })

#define Stack_popAs(stack, T)                                                \
    ({                                                                       \
        typeof_unqual(T) _vex_st_pop_out;                                    \
        memset(&_vex_st_pop_out, 0, sizeof(_vex_st_pop_out));                \
        (discard) Stack_popCopyOut((stack), &_vex_st_pop_out,                \
                                   sizeof(_vex_st_pop_out), #T);             \
        _vex_st_pop_out;                                                     \
    })

#define Stack_getAs(stack, index, T)                                         \
    ({                                                                       \
        typeof_unqual(T) _vex_st_ga_out;                                     \
        memset(&_vex_st_ga_out, 0, sizeof(_vex_st_ga_out));                  \
        (discard) Stack_copyOut((stack), (index), &_vex_st_ga_out,           \
                                sizeof(_vex_st_ga_out), #T);                 \
        _vex_st_ga_out;                                                      \
    })

#endif
