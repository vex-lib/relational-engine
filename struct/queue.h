#ifndef STRUCT_QUEUE_H
#define STRUCT_QUEUE_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/collection.h"
#include "struct/layout.h"

/**
 * struct/queue.h, the polymorphic FIFO Queue interface with ArrayQueue (default
 * circular buffer) and LinkedQueue (32-byte-aligned arena node chain) backings.
 *
 * Queue *q = ArrayQueue(Class);
 * Queue *l = LinkedQueue(u32);
 * Queue_push(q, item);
 * Class *front = Queue_peek(q);
 * Class popped = Queue_popAs(q, Class);
 * Queue_free(q);
 */

#define ARRAY_QUEUE_DEFAULT_CAPACITY 16u

/**
 * Concrete Queue backing discriminator.
 *
 * QUEUE_KIND_UNKNOWN - uninitialized or invalid backing
 * QUEUE_KIND_ARRAY   - growable circular array buffer (ArrayQueue)
 * QUEUE_KIND_LINKED  - singly-linked 32B arena nodes (LinkedQueue)
 */
;;CLASSNAME("QueueKind")
typedef enum QueueKind {
    QUEUE_KIND_UNKNOWN = 0,
    QUEUE_KIND_ARRAY = 1,
    QUEUE_KIND_LINKED = 2,
} QueueKind;

/**
 * 32-byte singly-linked node header preceding inline element payload bytes.
 *
 * struct QueueNode *next - next element toward the tail
 * u64 pad0               - alignment padding
 * u64 pad1               - alignment padding
 * u64 pad2               - alignment padding
 */
;;CLASSNAME("QueueNode")
typedef struct QueueNode {
    struct QueueNode *next;
    u64 pad0;
    u64 pad1;
    u64 pad2;
} QueueNode;

_Static_assert(sizeof(QueueNode) == 32u, "QueueNode header must be 32 bytes");

/**
 * Polymorphic FIFO Queue supporting both ArrayQueue and LinkedQueue backings.
 *
 * Collection collection - embedded Collection header (offset 0)
 * QueueKind kind        - concrete backing discriminator
 * MemoryArena *arena    - borrowed MemoryArena owning allocations
 * ElementLayout layout  - compile-time-captured element layout
 * QueueNode *headNode   - front node for LinkedQueue (nullptr on ArrayQueue)
 * QueueNode *tailNode   - back node for LinkedQueue (nullptr on ArrayQueue)
 */
;;CLASSNAME("Queue")
typedef struct Queue {
    Collection collection;
    QueueKind kind;
    MemoryArena *arena;
    ElementLayout layout;
    QueueNode *headNode;
    QueueNode *tailNode;
} Queue;

Queue *ArrayQueue_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena);
Queue *LinkedQueue_newLayout(ElementLayout layout, MemoryArena *arena);

#define ArrayQueue_ctor_1(T)             ArrayQueue_newLayout(ElementLayout_of(T), ARRAY_QUEUE_DEFAULT_CAPACITY, Re_arena())
#define ArrayQueue_ctor_2(T, cap)        ArrayQueue_newLayout(ElementLayout_of(T), (cap), Re_arena())
#define ArrayQueue_ctor_3(T, cap, arena) ArrayQueue_newLayout(ElementLayout_of(T), (cap), (arena))
#define ArrayQueue(...)                  CONSTRUCTOR_DISPATCH(ArrayQueue_ctor, __VA_ARGS__)

#define LinkedQueue_ctor_1(T)            LinkedQueue_newLayout(ElementLayout_of(T), Re_arena())
#define LinkedQueue_ctor_2(T, arena)     LinkedQueue_newLayout(ElementLayout_of(T), (arena))
#define LinkedQueue(...)                 CONSTRUCTOR_DISPATCH(LinkedQueue_ctor, __VA_ARGS__)

#define Queue(...)                       ArrayQueue(__VA_ARGS__)

bool ArrayQueue_isArrayQueue(const Queue *self);
bool LinkedQueue_isLinkedQueue(const Queue *self);

bool Queue_pushBytes(Queue *self, const_any elem, bytes elemBytes);
any Queue_emplace(Queue *self);
any Queue_peek(const Queue *self);
bool Queue_peekCopyOut(const Queue *self, any dest, bytes elemBytes, string typeName);
bool Queue_pop(Queue *self);
bool Queue_popCopyOut(Queue *self, any dest, bytes elemBytes, string typeName);
any Queue_get(const Queue *self, bytes index);
bool Queue_copyOut(const Queue *self, bytes index, any dest, bytes elemBytes, string typeName);
fn Queue_clear(Queue *self);
bool Queue_reserve(Queue *self, bytes minCapacity);
fn Queue_free(Queue *self);

bytes Queue_count(const Queue *self);
bytes Queue_size(const Queue *self);
bytes Queue_length(const Queue *self);
bytes Queue_capacity(const Queue *self);
bool Queue_isEmpty(const Queue *self);
bytes Queue_stride(const Queue *self);
string Queue_typeName(const Queue *self);
QueueKind Queue_kind(const Queue *self);
string Queue_kindName(const Queue *self);
MemoryArena *Queue_arena(const Queue *self);
ElementLayout Queue_layout(const Queue *self);

;;TEST
u64 Queue_exhaustionCount(const Queue *self);

fn Queue_toString(const Queue *self, utf8 *dest, bytes cap, bool *outTruncated);
fn Queue_toStringStruct(const Queue *self, utf8 *dest, bytes cap, bool *outTruncated);

#define Queue_push(queue, val)                                               \
    ({                                                                       \
        auto _vex_q_push_v = (val);                                          \
        Queue_pushBytes((queue), &_vex_q_push_v, sizeof(_vex_q_push_v));     \
    })

#define Queue_peekAs(queue, T)                                               \
    ({                                                                       \
        typeof_unqual(T) _vex_q_pk_out;                                      \
        memset(&_vex_q_pk_out, 0, sizeof(_vex_q_pk_out));                    \
        (discard) Queue_peekCopyOut((queue), &_vex_q_pk_out,                 \
                                    sizeof(_vex_q_pk_out), #T);              \
        _vex_q_pk_out;                                                       \
    })

#define Queue_popAs(queue, T)                                                \
    ({                                                                       \
        typeof_unqual(T) _vex_q_pop_out;                                     \
        memset(&_vex_q_pop_out, 0, sizeof(_vex_q_pop_out));                  \
        (discard) Queue_popCopyOut((queue), &_vex_q_pop_out,                 \
                                   sizeof(_vex_q_pop_out), #T);              \
        _vex_q_pop_out;                                                      \
    })

#define Queue_getAs(queue, index, T)                                         \
    ({                                                                       \
        typeof_unqual(T) _vex_q_ga_out;                                      \
        memset(&_vex_q_ga_out, 0, sizeof(_vex_q_ga_out));                    \
        (discard) Queue_copyOut((queue), (index), &_vex_q_ga_out,            \
                                sizeof(_vex_q_ga_out), #T);                  \
        _vex_q_ga_out;                                                       \
    })

#endif
