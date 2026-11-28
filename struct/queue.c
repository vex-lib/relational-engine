#include "vex.h"
#include <stdckdint.h>

#include "nio/mem.h"
#include "struct/exhaustion.h"
#include "struct/queue.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Queue
 * ============================================================================
 * Polymorphic FIFO Queue owned by Relational Engine (lifetime tier) providing
 * two arena-backed implementations behind one handle: ArrayQueue (default
 * contiguous circular buffer with geometric growth) and LinkedQueue (32-byte-
 * aligned singly-linked nodes with pointer stability). Elements are stored
 * inline by ElementLayout stride with zero bare libc allocations.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Queue (struct/queue.c)
 * ============================================================================
 * FIFO queue supporting ArrayQueue (circular ring) and LinkedQueue (node chain).
 *
 * --CONSTANTS----------------------------
 * public   const u32 ARRAY_QUEUE_DEFAULT_CAPACITY  default array slots (16)
 *
 * --FIELDS-------------------------------
 * public   Collection    collection                embedded Collection, 1:1
 * public   QueueKind     kind                      concrete backing tag
 * public   MemoryArena  *arena                     borrows MemoryArena, 1:1
 * public   ElementLayout layout                    element layout metadata
 * private  QueueNode    *headNode                  owns linked front, 1:N
 * private  QueueNode    *tailNode                  borrows linked back, 1:1
 *
 * --CONSTRUCTORS-------------------------
 * public   ArrayQueue_newLayout(...)               circular array queue
 * public   LinkedQueue_newLayout(...)              linked node queue
 *
 * --FUNCTIONS----------------------------
 * public   ArrayQueue_isArrayQueue(...)            checks QUEUE_KIND_ARRAY
 * public   LinkedQueue_isLinkedQueue(...)          checks QUEUE_KIND_LINKED
 * public   Queue_pushBytes(...)                    stride-checked FIFO enqueue
 * public   Queue_emplace(...)                      enqueues zeroed slot
 * public   Queue_peek(...)                         front element pointer
 * public   Queue_peekCopyOut(...)                  stride-checked front copy
 * public   Queue_pop(...)                          removes front element
 * public   Queue_popCopyOut(...)                   copies out and removes front
 * public   Queue_get(...)                          logical index lookup
 * public   Queue_copyOut(...)                      stride-checked index copy
 * public   Queue_clear(...)                        drops all queued elements
 * public   Queue_reserve(...)                      grows circular ring buffer
 * public   Queue_free(...)                         releases storage and handle
 * public   Queue_toString(...)                     cold value projection
 * public   Queue_toStringStruct(...)               cold structure projection
 * private  arrayQueueGrow(...)                     linearizing ring growth
 *
 * --GETTERS------------------------------
 * public   Queue_count / size / length / capacity / isEmpty / stride / typeName
 * public   Queue_kind / kindName / arena / layout / exhaustionCount
 * private  QNode_next / Q_headNode / Q_tailNode / QColl_*
 *
 * --SETTERS------------------------------
 * private  QNode_set* / Q_set* / QColl_set*
 * ============================================================================
 */

;;GETTER
static inline QueueNode *QNode_next(const QueueNode *n)
{
    return (*n).next;
}

;;SETTER
static inline fn QNode_setNext(QueueNode *n, QueueNode *next)
{
    (*n).next = next;
}

;;SETTER
static inline fn QNode_setPads(QueueNode *n)
{
    (*n).pad0 = 0;
    (*n).pad1 = 0;
    (*n).pad2 = 0;
}

;;GETTER
static inline QueueNode *Q_headNode(const Queue *q)
{
    return (*q).headNode;
}

;;GETTER
static inline QueueNode *Q_tailNode(const Queue *q)
{
    return (*q).tailNode;
}

;;GETTER
QueueKind Queue_kind(const Queue *self)
{
    if(!self)
        return QUEUE_KIND_UNKNOWN;

    return (*self).kind;
}

;;GETTER
MemoryArena *Queue_arena(const Queue *self)
{
    if(!self)
        return nullptr;

    return (*self).arena;
}

;;GETTER
ElementLayout Queue_layout(const Queue *self)
{
    if(!self)
        return ElementLayout_make(0, 0, nullptr);

    return (*self).layout;
}

;;SETTER
static inline fn Q_setKind(Queue *q, QueueKind kind)
{
    (*q).kind = kind;
}

;;SETTER
static inline fn Q_setArena(Queue *q, MemoryArena *arena)
{
    (*q).arena = arena;
}

;;SETTER
static inline fn Q_setLayout(Queue *q, ElementLayout layout)
{
    (*q).layout = layout;
}

;;SETTER
static inline fn Q_setHeadNode(Queue *q, QueueNode *node)
{
    (*q).headNode = node;
}

;;SETTER
static inline fn Q_setTailNode(Queue *q, QueueNode *node)
{
    (*q).tailNode = node;
}

;;GETTER
static inline u32 QColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;GETTER
static inline u32 QColl_capacity(const Collection *c)
{
    return (*c).capacity;
}

;;GETTER
static inline u32 QColl_head(const Collection *c)
{
    return (*c).head;
}

;;GETTER
static inline u8 *QColl_data(const Collection *c)
{
    return (*c).data;
}

;;SETTER
static inline fn QColl_setTypeId(Collection *c, u64 typeId)
{
    (*c).typeId = typeId;
}

;;SETTER
static inline fn QColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn QColl_setElementClass(Collection *c, u32 cls)
{
    (*c).elementClass = cls;
}

;;SETTER
static inline fn QColl_setStride(Collection *c, u32 stride)
{
    (*c).stride = stride;
}

;;SETTER
static inline fn QColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;SETTER
static inline fn QColl_setHead(Collection *c, u32 head)
{
    (*c).head = head;
}

;;SETTER
static inline fn QColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

;;SETTER
static inline fn QColl_setExhaustionCount(Collection *c, u64 count)
{
    (*c).exhaustionCount = count;
}

;;SETTER
static inline fn QColl_setExhaustionReported(Collection *c, bool rep)
{
    (*c).exhaustionReported = rep;
}

static inline u8 *qNodeElem(QueueNode *node)
{
    return ((u8*) node) + sizeof(QueueNode);
}

static fn formatWrite(utf8 *dest, bytes cap, bool *outTruncated, i32 needed)
{
    if(outTruncated)
        *outTruncated = needed < 0 || (bytes) needed >= cap;

    if(cap > 0 && needed < 0)
        dest[0] = '\0';
}

static fn initQueueCollection(Collection *c, u64 typeId, bytes stride)
{
    QColl_setTypeId(c, typeId);
    QColl_setActiveCount(c, 0);
    QColl_setElementClass(c, 0);
    QColl_setStride(c, (u32) stride);
    QColl_setCapacity(c, 0);
    QColl_setHead(c, 0);
    QColl_setData(c, nullptr);
    QColl_setExhaustionCount(c, 0);
    QColl_setExhaustionReported(c, false);
}

static bool arrayQueueGrow(Queue *self, bytes minCapacity)
{
    Collection *c = (Collection*) self;
    u32 oldCap = QColl_capacity(c);
    bytes newCap = oldCap == 0 ? ARRAY_QUEUE_DEFAULT_CAPACITY : (bytes) oldCap;

    while(newCap < minCapacity) {
        if(newCap > ((bytes) UINT32_MAX >> 1u)) {
            if(minCapacity <= (bytes) UINT32_MAX) {
                newCap = minCapacity;
                break;
            }

            return Collection_reportExhaustion(c, 0);
        }

        newCap <<= 1u;
    }

    if(newCap > (bytes) UINT32_MAX)
        return Collection_reportExhaustion(c, 0);

    bytes stride = Queue_stride(self);
    bytes allocBytes = 0;

    if(ckd_mul(&allocBytes, newCap, stride))
        return Collection_reportExhaustion(c, 0);

    MemoryArena *arena = Queue_arena(self);
    u8 *newBuf = (u8*) Memory_alloc(TYPE_BYTE_ARRAY, arena, allocBytes);

    if(!newBuf)
        return Collection_reportExhaustion(c, allocBytes);

    u8 *oldBuf = QColl_data(c);
    u32 active = QColl_activeCount(c);
    u32 head = QColl_head(c);

    if(oldBuf && active > 0 && oldCap > 0) {
        u32 firstChunk = oldCap - head;

        if(firstChunk > active)
            firstChunk = active;

        memcpy(newBuf, oldBuf + ((bytes) head * stride), (bytes) firstChunk * stride);
        u32 remain = active - firstChunk;

        if(remain > 0) {
            memcpy(newBuf + ((bytes) firstChunk * stride), oldBuf,
                   (bytes) remain * stride);
        }
    }

    if(oldBuf)
        MemoryArena_free(arena, oldBuf);

    QColl_setData(c, newBuf);
    QColl_setCapacity(c, (u32) newCap);
    QColl_setHead(c, 0);

    return true;
}

;;CONSTRUCTOR
Queue *ArrayQueue_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("ArrayQueue: invalid layout for '%s'", ElementLayout_typeName(layout));

        return nullptr;
    }

    if(initialCapacity > (bytes) UINT32_MAX) {
        THROW("ArrayQueue: initial capacity %zu overflows u32", initialCapacity);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    bytes stride = ElementLayout_stride(layout);
    bytes cap = initialCapacity;
    bytes allocBytes = 0;

    if(cap > 0 && ckd_mul(&allocBytes, cap, stride)) {
        THROW("ArrayQueue: initial allocation overflow");

        return nullptr;
    }

    Queue *q = (Queue*) Memory_alloc(TYPE_ARRAY_QUEUE, targetArena, sizeof(Queue));

    if(!q)
        return nullptr;

    Collection *c = (Collection*) q;
    initQueueCollection(c, TYPE_ARRAY_QUEUE, stride);
    Q_setKind(q, QUEUE_KIND_ARRAY);
    Q_setArena(q, targetArena);
    Q_setLayout(q, layout);
    Q_setHeadNode(q, nullptr);
    Q_setTailNode(q, nullptr);

    if(cap > 0) {
        u8 *buf = (u8*) Memory_alloc(TYPE_BYTE_ARRAY, targetArena, allocBytes);

        if(!buf) {
            MemoryArena_free(targetArena, q);

            return nullptr;
        }

        QColl_setData(c, buf);
        QColl_setCapacity(c, (u32) cap);
    }

    return q;
}

;;CONSTRUCTOR
Queue *LinkedQueue_newLayout(ElementLayout layout, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("LinkedQueue: invalid layout for '%s'", ElementLayout_typeName(layout));

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    Queue *q = (Queue*) Memory_alloc(TYPE_LINKED_QUEUE, targetArena, sizeof(Queue));

    if(!q)
        return nullptr;

    Collection *c = (Collection*) q;
    initQueueCollection(c, TYPE_LINKED_QUEUE, ElementLayout_stride(layout));
    Q_setKind(q, QUEUE_KIND_LINKED);
    Q_setArena(q, targetArena);
    Q_setLayout(q, layout);
    Q_setHeadNode(q, nullptr);
    Q_setTailNode(q, nullptr);

    return q;
}

bool ArrayQueue_isArrayQueue(const Queue *self)
{
    return Queue_kind(self) == QUEUE_KIND_ARRAY;
}

bool LinkedQueue_isLinkedQueue(const Queue *self)
{
    return Queue_kind(self) == QUEUE_KIND_LINKED;
}

any Queue_emplace(Queue *self)
{
    if(!self)
        return nullptr;

    Collection *c = (Collection*) self;
    bytes stride = Queue_stride(self);
    u32 active = QColl_activeCount(c);

    if(active == UINT32_MAX) {
        (discard) Collection_reportExhaustion(c, 0);

        return nullptr;
    }

    if(Queue_kind(self) == QUEUE_KIND_ARRAY) {
        u32 cap = QColl_capacity(c);

        if(active >= cap) {
            if(!arrayQueueGrow(self, (bytes) active + 1u))
                return nullptr;

            cap = QColl_capacity(c);
        }

        u32 head = QColl_head(c);
        bytes phys = ((bytes) head + (bytes) active) % (bytes) cap;
        u8 *slot = QColl_data(c) + (phys * stride);
        memset(slot, 0, stride);
        QColl_setActiveCount(c, active + 1u);

        return slot;
    }

    if(Queue_kind(self) == QUEUE_KIND_LINKED) {
        bytes nodeBytes = sizeof(QueueNode) + stride;
        QueueNode *node = (QueueNode*) Memory_alloc(TYPE_LINKED_NODE, Queue_arena(self), nodeBytes);

        if(!node) {
            (discard) Collection_reportExhaustion(c, nodeBytes);

            return nullptr;
        }

        QNode_setNext(node, nullptr);
        QNode_setPads(node);
        u8 *elem = qNodeElem(node);
        memset(elem, 0, stride);

        QueueNode *tail = Q_tailNode(self);

        if(tail)
            QNode_setNext(tail, node);
        else
            Q_setHeadNode(self, node);

        Q_setTailNode(self, node);
        QColl_setActiveCount(c, active + 1u);

        return elem;
    }

    return nullptr;
}

bool Queue_pushBytes(Queue *self, const_any elem, bytes elemBytes)
{
    if(!self || !elem)
        return false;

    if(elemBytes != Queue_stride(self)) {
        THROW("Queue_push: element width %zu does not match '%s' stride %zu",
              elemBytes, Queue_typeName(self), Queue_stride(self));

        return false;
    }

    any slot = Queue_emplace(self);

    if(!slot)
        return false;

    memcpy(slot, elem, elemBytes);

    return true;
}

;;HOTCODE
any Queue_peek(const Queue *self)
{
    if(!self)
        return nullptr;

    const Collection *c = (const Collection*) self;

    if(QColl_activeCount(c) == 0)
        return nullptr;

    if(Queue_kind(self) == QUEUE_KIND_ARRAY) {
        u8 *buf = QColl_data(c);

        if(!buf)
            return nullptr;

        return buf + ((bytes) QColl_head(c) * Queue_stride(self));
    }

    if(Queue_kind(self) == QUEUE_KIND_LINKED) {
        QueueNode *head = Q_headNode(self);

        if(!head)
            return nullptr;

        return qNodeElem(head);
    }

    return nullptr;
}

bool Queue_peekCopyOut(const Queue *self, any dest, bytes elemBytes, string typeName)
{
    if(!self || !dest)
        return false;

    if(elemBytes != Queue_stride(self)) {
        THROW("Queue_peekAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Queue_typeName(self), Queue_stride(self));

        return false;
    }

    any front = Queue_peek(self);

    if(!front)
        return false;

    memcpy(dest, front, elemBytes);

    return true;
}

bool Queue_popCopyOut(Queue *self, any dest, bytes elemBytes, string typeName)
{
    if(!self)
        return false;

    if(dest && elemBytes != Queue_stride(self)) {
        THROW("Queue_popAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Queue_typeName(self), Queue_stride(self));

        return false;
    }

    Collection *c = (Collection*) self;
    u32 active = QColl_activeCount(c);

    if(active == 0)
        return false;

    bytes stride = Queue_stride(self);

    if(Queue_kind(self) == QUEUE_KIND_ARRAY) {
        u32 cap = QColl_capacity(c);
        u32 head = QColl_head(c);
        u8 *buf = QColl_data(c);

        if(!buf || cap == 0)
            return false;

        if(dest)
            memcpy(dest, buf + ((bytes) head * stride), stride);

        u32 nextHead = head + 1u;

        if(nextHead >= cap)
            nextHead = 0;

        QColl_setHead(c, active == 1u ? 0u : nextHead);
        QColl_setActiveCount(c, active - 1u);

        return true;
    }

    if(Queue_kind(self) == QUEUE_KIND_LINKED) {
        QueueNode *head = Q_headNode(self);

        if(!head)
            return false;

        if(dest)
            memcpy(dest, qNodeElem(head), stride);

        QueueNode *next = QNode_next(head);
        Q_setHeadNode(self, next);

        if(!next)
            Q_setTailNode(self, nullptr);

        QColl_setActiveCount(c, active - 1u);
        MemoryArena_free(Queue_arena(self), head);

        return true;
    }

    return false;
}

bool Queue_pop(Queue *self)
{
    return Queue_popCopyOut(self, nullptr, 0, nullptr);
}

;;HOTCODE
any Queue_get(const Queue *self, bytes index)
{
    if(!self)
        return nullptr;

    const Collection *c = (const Collection*) self;
    u32 active = QColl_activeCount(c);

    if(index >= (bytes) active)
        return nullptr;

    if(Queue_kind(self) == QUEUE_KIND_ARRAY) {
        u32 cap = QColl_capacity(c);
        u8 *buf = QColl_data(c);

        if(!buf || cap == 0)
            return nullptr;

        bytes phys = ((bytes) QColl_head(c) + index) % (bytes) cap;

        return buf + (phys * Queue_stride(self));
    }

    if(Queue_kind(self) == QUEUE_KIND_LINKED) {
        QueueNode *cur = Q_headNode(self);
        bytes i = 0;

        while(cur && i < index) {
            cur = QNode_next(cur);
            i++;
        }

        if(!cur)
            return nullptr;

        return qNodeElem(cur);
    }

    return nullptr;
}

bool Queue_copyOut(const Queue *self, bytes index, any dest, bytes elemBytes, string typeName)
{
    if(!self || !dest)
        return false;

    if(elemBytes != Queue_stride(self)) {
        THROW("Queue_getAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Queue_typeName(self), Queue_stride(self));

        return false;
    }

    any slot = Queue_get(self, index);

    if(!slot)
        return false;

    memcpy(dest, slot, elemBytes);

    return true;
}

fn Queue_clear(Queue *self)
{
    if(!self)
        return;

    Collection *c = (Collection*) self;

    if(Queue_kind(self) == QUEUE_KIND_LINKED) {
        MemoryArena *arena = Queue_arena(self);
        QueueNode *cur = Q_headNode(self);

        while(cur) {
            QueueNode *next = QNode_next(cur);
            MemoryArena_free(arena, cur);
            cur = next;
        }

        Q_setHeadNode(self, nullptr);
        Q_setTailNode(self, nullptr);
    }

    QColl_setActiveCount(c, 0);
    QColl_setHead(c, 0);
}

bool Queue_reserve(Queue *self, bytes minCapacity)
{
    if(!self)
        return false;

    if(Queue_kind(self) == QUEUE_KIND_LINKED)
        return true;

    if(Queue_kind(self) != QUEUE_KIND_ARRAY)
        return false;

    Collection *c = (Collection*) self;

    if(minCapacity <= (bytes) QColl_capacity(c))
        return true;

    return arrayQueueGrow(self, minCapacity);
}

;;DECONSTRUCT
fn Queue_free(Queue *self)
{
    if(!self)
        return;

    MemoryArena *arena = Queue_arena(self);
    Collection *c = (Collection*) self;

    if(Queue_kind(self) == QUEUE_KIND_LINKED) {
        Queue_clear(self);
    } else if(Queue_kind(self) == QUEUE_KIND_ARRAY) {
        u8 *buf = QColl_data(c);

        if(buf) {
            MemoryArena_free(arena, buf);
            QColl_setData(c, nullptr);
        }
    }

    MemoryArena_free(arena, self);
}

bytes Queue_count(const Queue *self)
{
    if(!self)
        return 0;

    return (bytes) QColl_activeCount((const Collection*) self);
}

bytes Queue_size(const Queue *self)
{
    return Queue_count(self);
}

bytes Queue_length(const Queue *self)
{
    return Queue_count(self);
}

bytes Queue_capacity(const Queue *self)
{
    if(!self)
        return 0;

    if(Queue_kind(self) == QUEUE_KIND_LINKED)
        return Queue_count(self);

    return (bytes) QColl_capacity((const Collection*) self);
}

bool Queue_isEmpty(const Queue *self)
{
    return Queue_count(self) == 0;
}

bytes Queue_stride(const Queue *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(Queue_layout(self));
}

string Queue_typeName(const Queue *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(Queue_layout(self));
}

string Queue_kindName(const Queue *self)
{
    QueueKind k = Queue_kind(self);

    if(k == QUEUE_KIND_ARRAY)
        return "ArrayQueue";

    if(k == QUEUE_KIND_LINKED)
        return "LinkedQueue";

    return "UnknownQueue";
}

;;TEST
u64 Queue_exhaustionCount(const Queue *self)
{
    if(!self)
        return 0;

    return Collection_exhaustionCount((const Collection*) self);
}

fn Queue_toString(const Queue *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 needed = snprintf(dest, cap, "Queue(nullptr)");
        formatWrite(dest, cap, outTruncated, needed);

        return;
    }

    i32 needed = snprintf(dest, cap, "%s<%s>(count=%zu, capacity=%zu)",
                          Queue_kindName(self),
                          Queue_typeName(self),
                          Queue_count(self),
                          Queue_capacity(self));
    formatWrite(dest, cap, outTruncated, needed);
}

fn Queue_toStringStruct(const Queue *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 needed = snprintf(dest, cap, "Queue{nullptr}");
        formatWrite(dest, cap, outTruncated, needed);

        return;
    }

    i32 needed = snprintf(dest, cap,
                          "Queue{kind=%s, type=%s, stride=%zu, count=%zu, capacity=%zu, exhausted=%llu}",
                          Queue_kindName(self),
                          Queue_typeName(self),
                          Queue_stride(self),
                          Queue_count(self),
                          Queue_capacity(self),
                          (u64) Queue_exhaustionCount(self));
    formatWrite(dest, cap, outTruncated, needed);
}
