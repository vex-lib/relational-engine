#ifndef STRUCT_DEQUE_H
#define STRUCT_DEQUE_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/collection.h"
#include "struct/layout.h"

/**
 * struct/deque.h, the polymorphic double-ended Deque interface with ArrayDeque
 * (default circular ring buffer) and LinkedDeque (32-byte doubly-linked arena
 * nodes) backings.
 *
 * Deque *d = ArrayDeque(Class);
 * Deque *l = LinkedDeque(u32);
 * Deque_addFirst(d, a);
 * Deque_addLast(d, b);
 * Class first = Deque_popFirstAs(d, Class);
 * Deque_free(d);
 */

#define ARRAY_DEQUE_DEFAULT_CAPACITY 16u

/**
 * Concrete Deque backing discriminator.
 *
 * DEQUE_KIND_UNKNOWN - uninitialized or invalid backing
 * DEQUE_KIND_ARRAY   - growable circular array ring (ArrayDeque)
 * DEQUE_KIND_LINKED  - doubly-linked 32B arena nodes (LinkedDeque)
 */
;;CLASSNAME("DequeKind")
typedef enum DequeKind {
    DEQUE_KIND_UNKNOWN = 0,
    DEQUE_KIND_ARRAY = 1,
    DEQUE_KIND_LINKED = 2,
} DequeKind;

/**
 * 32-byte doubly-linked node header preceding inline element payload bytes.
 *
 * struct DequeNode *prev - previous element toward the front
 * struct DequeNode *next - next element toward the back
 * u64 pad0               - alignment padding
 * u64 pad1               - alignment padding
 */
;;CLASSNAME("DequeNode")
typedef struct DequeNode {
    struct DequeNode *prev;
    struct DequeNode *next;
    u64 pad0;
    u64 pad1;
} DequeNode;

_Static_assert(sizeof(DequeNode) == 32u, "DequeNode header must be 32 bytes");

/**
 * Polymorphic double-ended queue supporting ArrayDeque and LinkedDeque.
 *
 * Collection collection - embedded Collection header (offset 0)
 * DequeKind kind        - concrete backing discriminator
 * MemoryArena *arena    - borrowed MemoryArena owning allocations
 * ElementLayout layout  - compile-time-captured element layout
 * DequeNode *headNode   - front node for LinkedDeque (nullptr on ArrayDeque)
 * DequeNode *tailNode   - back node for LinkedDeque (nullptr on ArrayDeque)
 */
;;CLASSNAME("Deque")
typedef struct Deque {
    Collection collection;
    DequeKind kind;
    MemoryArena *arena;
    ElementLayout layout;
    DequeNode *headNode;
    DequeNode *tailNode;
} Deque;

Deque *ArrayDeque_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena);
Deque *LinkedDeque_newLayout(ElementLayout layout, MemoryArena *arena);

#define ArrayDeque_ctor_1(T)             ArrayDeque_newLayout(ElementLayout_of(T), ARRAY_DEQUE_DEFAULT_CAPACITY, Re_arena())
#define ArrayDeque_ctor_2(T, cap)        ArrayDeque_newLayout(ElementLayout_of(T), (cap), Re_arena())
#define ArrayDeque_ctor_3(T, cap, arena) ArrayDeque_newLayout(ElementLayout_of(T), (cap), (arena))
#define ArrayDeque(...)                  CONSTRUCTOR_DISPATCH(ArrayDeque_ctor, __VA_ARGS__)

#define LinkedDeque_ctor_1(T)            LinkedDeque_newLayout(ElementLayout_of(T), Re_arena())
#define LinkedDeque_ctor_2(T, arena)     LinkedDeque_newLayout(ElementLayout_of(T), (arena))
#define LinkedDeque(...)                 CONSTRUCTOR_DISPATCH(LinkedDeque_ctor, __VA_ARGS__)

#define Deque(...)                       ArrayDeque(__VA_ARGS__)

bool ArrayDeque_isArrayDeque(const Deque *self);
bool LinkedDeque_isLinkedDeque(const Deque *self);

bool Deque_addFirstBytes(Deque *self, const_any elem, bytes elemBytes);
bool Deque_addLastBytes(Deque *self, const_any elem, bytes elemBytes);
any Deque_emplaceFirst(Deque *self);
any Deque_emplaceLast(Deque *self);
any Deque_peekFirst(const Deque *self);
any Deque_peekLast(const Deque *self);
bool Deque_peekFirstCopyOut(const Deque *self, any dest, bytes elemBytes, string typeName);
bool Deque_peekLastCopyOut(const Deque *self, any dest, bytes elemBytes, string typeName);
bool Deque_removeFirst(Deque *self);
bool Deque_removeLast(Deque *self);
bool Deque_popFirstCopyOut(Deque *self, any dest, bytes elemBytes, string typeName);
bool Deque_popLastCopyOut(Deque *self, any dest, bytes elemBytes, string typeName);
any Deque_get(const Deque *self, bytes index);
bool Deque_copyOut(const Deque *self, bytes index, any dest, bytes elemBytes, string typeName);
fn Deque_clear(Deque *self);
bool Deque_reserve(Deque *self, bytes minCapacity);
fn Deque_free(Deque *self);

bytes Deque_count(const Deque *self);
bytes Deque_size(const Deque *self);
bytes Deque_length(const Deque *self);
bytes Deque_capacity(const Deque *self);
bool Deque_isEmpty(const Deque *self);
bytes Deque_stride(const Deque *self);
string Deque_typeName(const Deque *self);
DequeKind Deque_kind(const Deque *self);
string Deque_kindName(const Deque *self);
MemoryArena *Deque_arena(const Deque *self);
ElementLayout Deque_layout(const Deque *self);

;;TEST
u64 Deque_exhaustionCount(const Deque *self);

fn Deque_toString(const Deque *self, utf8 *dest, bytes cap, bool *outTruncated);
fn Deque_toStringStruct(const Deque *self, utf8 *dest, bytes cap, bool *outTruncated);

#define Deque_addFirst(deque, val)                                           \
    ({                                                                       \
        auto _vex_dq_af_v = (val);                                           \
        Deque_addFirstBytes((deque), &_vex_dq_af_v, sizeof(_vex_dq_af_v));   \
    })

#define Deque_addLast(deque, val)                                            \
    ({                                                                       \
        auto _vex_dq_al_v = (val);                                           \
        Deque_addLastBytes((deque), &_vex_dq_al_v, sizeof(_vex_dq_al_v));    \
    })

#define Deque_peekFirstAs(deque, T)                                          \
    ({                                                                       \
        typeof_unqual(T) _vex_dq_pf_out;                                     \
        memset(&_vex_dq_pf_out, 0, sizeof(_vex_dq_pf_out));                  \
        (discard) Deque_peekFirstCopyOut((deque), &_vex_dq_pf_out,           \
                                         sizeof(_vex_dq_pf_out), #T);        \
        _vex_dq_pf_out;                                                      \
    })

#define Deque_peekLastAs(deque, T)                                           \
    ({                                                                       \
        typeof_unqual(T) _vex_dq_pl_out;                                     \
        memset(&_vex_dq_pl_out, 0, sizeof(_vex_dq_pl_out));                  \
        (discard) Deque_peekLastCopyOut((deque), &_vex_dq_pl_out,            \
                                        sizeof(_vex_dq_pl_out), #T);         \
        _vex_dq_pl_out;                                                      \
    })

#define Deque_popFirstAs(deque, T)                                           \
    ({                                                                       \
        typeof_unqual(T) _vex_dq_rf_out;                                     \
        memset(&_vex_dq_rf_out, 0, sizeof(_vex_dq_rf_out));                  \
        (discard) Deque_popFirstCopyOut((deque), &_vex_dq_rf_out,            \
                                        sizeof(_vex_dq_rf_out), #T);         \
        _vex_dq_rf_out;                                                      \
    })

#define Deque_popLastAs(deque, T)                                            \
    ({                                                                       \
        typeof_unqual(T) _vex_dq_rl_out;                                     \
        memset(&_vex_dq_rl_out, 0, sizeof(_vex_dq_rl_out));                  \
        (discard) Deque_popLastCopyOut((deque), &_vex_dq_rl_out,             \
                                       sizeof(_vex_dq_rl_out), #T);          \
        _vex_dq_rl_out;                                                      \
    })

#define Deque_getAs(deque, index, T)                                         \
    ({                                                                       \
        typeof_unqual(T) _vex_dq_ga_out;                                     \
        memset(&_vex_dq_ga_out, 0, sizeof(_vex_dq_ga_out));                  \
        (discard) Deque_copyOut((deque), (index), &_vex_dq_ga_out,           \
                                sizeof(_vex_dq_ga_out), #T);                 \
        _vex_dq_ga_out;                                                      \
    })

#endif
