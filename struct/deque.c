#include "vex.h"
#include <stdckdint.h>

#include "nio/mem.h"
#include "struct/deque.h"
#include "struct/exhaustion.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Deque
 * ============================================================================
 * Polymorphic double-ended queue owned by Relational Engine (lifetime tier)
 * providing two arena-backed implementations behind one handle: ArrayDeque
 * (default circular buffer with geometric growth and O(1) indexed reads) and
 * LinkedDeque (32-byte doubly-linked nodes with pointer stability and
 * bidirectional indexed walks).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Deque (struct/deque.c)
 * ============================================================================
 * Double-ended queue supporting ArrayDeque (ring) and LinkedDeque (node chain).
 *
 * --CONSTANTS----------------------------
 * public   const u32 ARRAY_DEQUE_DEFAULT_CAPACITY  default array slots (16)
 *
 * --FIELDS-------------------------------
 * public   Collection    collection                embedded Collection, 1:1
 * public   DequeKind     kind                      concrete backing tag
 * public   MemoryArena  *arena                     borrows MemoryArena, 1:1
 * public   ElementLayout layout                    element layout metadata
 * private  DequeNode    *headNode                  owns linked chain front, 1:N
 * private  DequeNode    *tailNode                  borrows linked chain back, 1:1
 *
 * --CONSTRUCTORS-------------------------
 * public   ArrayDeque_newLayout(...)               circular array deque
 * public   LinkedDeque_newLayout(...)              doubly-linked node deque
 *
 * --FUNCTIONS----------------------------
 * public   ArrayDeque_isArrayDeque(...)            checks DEQUE_KIND_ARRAY
 * public   LinkedDeque_isLinkedDeque(...)          checks DEQUE_KIND_LINKED
 * public   Deque_addFirstBytes / addLastBytes      stride-checked front/back push
 * public   Deque_emplaceFirst / emplaceLast        zeroed slot front/back push
 * public   Deque_peekFirst / peekLast              front/back element pointers
 * public   Deque_peekFirstCopyOut / peekLastCopyOut stride-checked front/back copy
 * public   Deque_removeFirst / removeLast          removes front/back element
 * public   Deque_popFirstCopyOut / popLastCopyOut  copies out and removes
 * public   Deque_get / copyOut                     logical indexed read
 * public   Deque_clear / reserve / free            lifecycle operations
 * public   Deque_toString / toStringStruct         cold projections
 * private  arrayDequeGrow(...)                     linearizing ring growth
 *
 * --GETTERS------------------------------
 * public   Deque_count / size / length / capacity / isEmpty / stride / typeName
 * public   Deque_kind / kindName / arena / layout / exhaustionCount
 * private  DqNode_* / Dq_* / DqColl_*
 *
 * --SETTERS------------------------------
 * private  DqNode_set* / Dq_set* / DqColl_set*
 * ============================================================================
 */

;;GETTER
static inline DequeNode *DqNode_prev(const DequeNode *n)
{
    return (*n).prev;
}

;;GETTER
static inline DequeNode *DqNode_next(const DequeNode *n)
{
    return (*n).next;
}

;;SETTER
static inline fn DqNode_setPrev(DequeNode *n, DequeNode *prev)
{
    (*n).prev = prev;
}

;;SETTER
static inline fn DqNode_setNext(DequeNode *n, DequeNode *next)
{
    (*n).next = next;
}

;;SETTER
static inline fn DqNode_setPads(DequeNode *n)
{
    (*n).pad0 = 0;
    (*n).pad1 = 0;
}

;;GETTER
static inline DequeNode *Dq_headNode(const Deque *d)
{
    return (*d).headNode;
}

;;GETTER
static inline DequeNode *Dq_tailNode(const Deque *d)
{
    return (*d).tailNode;
}

;;GETTER
DequeKind Deque_kind(const Deque *self)
{
    if(!self)
        return DEQUE_KIND_UNKNOWN;

    return (*self).kind;
}

;;GETTER
MemoryArena *Deque_arena(const Deque *self)
{
    if(!self)
        return nullptr;

    return (*self).arena;
}

;;GETTER
ElementLayout Deque_layout(const Deque *self)
{
    if(!self)
        return ElementLayout_make(0, 0, nullptr);

    return (*self).layout;
}

;;SETTER
static inline fn Dq_setKind(Deque *d, DequeKind kind)
{
    (*d).kind = kind;
}

;;SETTER
static inline fn Dq_setArena(Deque *d, MemoryArena *arena)
{
    (*d).arena = arena;
}

;;SETTER
static inline fn Dq_setLayout(Deque *d, ElementLayout layout)
{
    (*d).layout = layout;
}

;;SETTER
static inline fn Dq_setHeadNode(Deque *d, DequeNode *node)
{
    (*d).headNode = node;
}

;;SETTER
static inline fn Dq_setTailNode(Deque *d, DequeNode *node)
{
    (*d).tailNode = node;
}

;;GETTER
static inline u32 DqColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;GETTER
static inline u32 DqColl_capacity(const Collection *c)
{
    return (*c).capacity;
}

;;GETTER
static inline u32 DqColl_head(const Collection *c)
{
    return (*c).head;
}

;;GETTER
static inline u8 *DqColl_data(const Collection *c)
{
    return (*c).data;
}

;;SETTER
static inline fn DqColl_setTypeId(Collection *c, u64 typeId)
{
    (*c).typeId = typeId;
}

;;SETTER
static inline fn DqColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn DqColl_setElementClass(Collection *c, u32 cls)
{
    (*c).elementClass = cls;
}

;;SETTER
static inline fn DqColl_setStride(Collection *c, u32 stride)
{
    (*c).stride = stride;
}

;;SETTER
static inline fn DqColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;SETTER
static inline fn DqColl_setHead(Collection *c, u32 head)
{
    (*c).head = head;
}

;;SETTER
static inline fn DqColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

;;SETTER
static inline fn DqColl_setExhaustionCount(Collection *c, u64 count)
{
    (*c).exhaustionCount = count;
}

;;SETTER
static inline fn DqColl_setExhaustionReported(Collection *c, bool rep)
{
    (*c).exhaustionReported = rep;
}

static inline u8 *dqNodeElem(DequeNode *node)
{
    return ((u8*) node) + sizeof(DequeNode);
}

static fn formatWrite(utf8 *dest, bytes cap, bool *outTruncated, i32 needed)
{
    if(outTruncated)
        *outTruncated = needed < 0 || (bytes) needed >= cap;

    if(cap > 0 && needed < 0)
        dest[0] = '\0';
}

static fn initDequeCollection(Collection *c, u64 typeId, bytes stride)
{
    DqColl_setTypeId(c, typeId);
    DqColl_setActiveCount(c, 0);
    DqColl_setElementClass(c, 0);
    DqColl_setStride(c, (u32) stride);
    DqColl_setCapacity(c, 0);
    DqColl_setHead(c, 0);
    DqColl_setData(c, nullptr);
    DqColl_setExhaustionCount(c, 0);
    DqColl_setExhaustionReported(c, false);
}

static bool arrayDequeGrow(Deque *self, bytes minCapacity)
{
    Collection *c = (Collection*) self;
    u32 oldCap = DqColl_capacity(c);
    bytes newCap = oldCap == 0 ? ARRAY_DEQUE_DEFAULT_CAPACITY : (bytes) oldCap;

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

    bytes stride = Deque_stride(self);
    bytes allocBytes = 0;

    if(ckd_mul(&allocBytes, newCap, stride))
        return Collection_reportExhaustion(c, 0);

    MemoryArena *arena = Deque_arena(self);
    u8 *newBuf = (u8*) Memory_alloc(TYPE_BYTE_ARRAY, arena, allocBytes);

    if(!newBuf)
        return Collection_reportExhaustion(c, allocBytes);

    u8 *oldBuf = DqColl_data(c);
    u32 active = DqColl_activeCount(c);
    u32 head = DqColl_head(c);

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

    DqColl_setData(c, newBuf);
    DqColl_setCapacity(c, (u32) newCap);
    DqColl_setHead(c, 0);

    return true;
}

;;CONSTRUCTOR
Deque *ArrayDeque_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("ArrayDeque: invalid layout for '%s'", ElementLayout_typeName(layout));

        return nullptr;
    }

    if(initialCapacity > (bytes) UINT32_MAX) {
        THROW("ArrayDeque: initial capacity %zu overflows u32", initialCapacity);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    bytes stride = ElementLayout_stride(layout);
    bytes cap = initialCapacity;
    bytes allocBytes = 0;

    if(cap > 0 && ckd_mul(&allocBytes, cap, stride)) {
        THROW("ArrayDeque: initial allocation overflow");

        return nullptr;
    }

    Deque *d = (Deque*) Memory_alloc(TYPE_ARRAY_DEQUE, targetArena, sizeof(Deque));

    if(!d)
        return nullptr;

    Collection *c = (Collection*) d;
    initDequeCollection(c, TYPE_ARRAY_DEQUE, stride);
    Dq_setKind(d, DEQUE_KIND_ARRAY);
    Dq_setArena(d, targetArena);
    Dq_setLayout(d, layout);
    Dq_setHeadNode(d, nullptr);
    Dq_setTailNode(d, nullptr);

    if(cap > 0) {
        u8 *buf = (u8*) Memory_alloc(TYPE_BYTE_ARRAY, targetArena, allocBytes);

        if(!buf) {
            MemoryArena_free(targetArena, d);

            return nullptr;
        }

        DqColl_setData(c, buf);
        DqColl_setCapacity(c, (u32) cap);
    }

    return d;
}

;;CONSTRUCTOR
Deque *LinkedDeque_newLayout(ElementLayout layout, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("LinkedDeque: invalid layout for '%s'", ElementLayout_typeName(layout));

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    Deque *d = (Deque*) Memory_alloc(TYPE_LINKED_DEQUE, targetArena, sizeof(Deque));

    if(!d)
        return nullptr;

    Collection *c = (Collection*) d;
    initDequeCollection(c, TYPE_LINKED_DEQUE, ElementLayout_stride(layout));
    Dq_setKind(d, DEQUE_KIND_LINKED);
    Dq_setArena(d, targetArena);
    Dq_setLayout(d, layout);
    Dq_setHeadNode(d, nullptr);
    Dq_setTailNode(d, nullptr);

    return d;
}

bool ArrayDeque_isArrayDeque(const Deque *self)
{
    return Deque_kind(self) == DEQUE_KIND_ARRAY;
}

bool LinkedDeque_isLinkedDeque(const Deque *self)
{
    return Deque_kind(self) == DEQUE_KIND_LINKED;
}

any Deque_emplaceFirst(Deque *self)
{
    if(!self)
        return nullptr;

    Collection *c = (Collection*) self;
    bytes stride = Deque_stride(self);
    u32 active = DqColl_activeCount(c);

    if(active == UINT32_MAX) {
        (discard) Collection_reportExhaustion(c, 0);

        return nullptr;
    }

    if(Deque_kind(self) == DEQUE_KIND_ARRAY) {
        u32 cap = DqColl_capacity(c);

        if(active >= cap) {
            if(!arrayDequeGrow(self, (bytes) active + 1u))
                return nullptr;

            cap = DqColl_capacity(c);
        }

        u32 head = DqColl_head(c);
        u32 newHead = head == 0 ? (cap - 1u) : (head - 1u);
        u8 *slot = DqColl_data(c) + ((bytes) newHead * stride);
        memset(slot, 0, stride);
        DqColl_setHead(c, newHead);
        DqColl_setActiveCount(c, active + 1u);

        return slot;
    }

    if(Deque_kind(self) == DEQUE_KIND_LINKED) {
        bytes nodeBytes = sizeof(DequeNode) + stride;
        DequeNode *node = (DequeNode*) Memory_alloc(TYPE_LINKED_NODE, Deque_arena(self), nodeBytes);

        if(!node) {
            (discard) Collection_reportExhaustion(c, nodeBytes);

            return nullptr;
        }

        DequeNode *oldHead = Dq_headNode(self);
        DqNode_setPrev(node, nullptr);
        DqNode_setNext(node, oldHead);
        DqNode_setPads(node);
        u8 *elem = dqNodeElem(node);
        memset(elem, 0, stride);

        if(oldHead)
            DqNode_setPrev(oldHead, node);
        else
            Dq_setTailNode(self, node);

        Dq_setHeadNode(self, node);
        DqColl_setActiveCount(c, active + 1u);

        return elem;
    }

    return nullptr;
}

any Deque_emplaceLast(Deque *self)
{
    if(!self)
        return nullptr;

    Collection *c = (Collection*) self;
    bytes stride = Deque_stride(self);
    u32 active = DqColl_activeCount(c);

    if(active == UINT32_MAX) {
        (discard) Collection_reportExhaustion(c, 0);

        return nullptr;
    }

    if(Deque_kind(self) == DEQUE_KIND_ARRAY) {
        u32 cap = DqColl_capacity(c);

        if(active >= cap) {
            if(!arrayDequeGrow(self, (bytes) active + 1u))
                return nullptr;

            cap = DqColl_capacity(c);
        }

        bytes phys = ((bytes) DqColl_head(c) + (bytes) active) % (bytes) cap;
        u8 *slot = DqColl_data(c) + (phys * stride);
        memset(slot, 0, stride);
        DqColl_setActiveCount(c, active + 1u);

        return slot;
    }

    if(Deque_kind(self) == DEQUE_KIND_LINKED) {
        bytes nodeBytes = sizeof(DequeNode) + stride;
        DequeNode *node = (DequeNode*) Memory_alloc(TYPE_LINKED_NODE, Deque_arena(self), nodeBytes);

        if(!node) {
            (discard) Collection_reportExhaustion(c, nodeBytes);

            return nullptr;
        }

        DequeNode *oldTail = Dq_tailNode(self);
        DqNode_setPrev(node, oldTail);
        DqNode_setNext(node, nullptr);
        DqNode_setPads(node);
        u8 *elem = dqNodeElem(node);
        memset(elem, 0, stride);

        if(oldTail)
            DqNode_setNext(oldTail, node);
        else
            Dq_setHeadNode(self, node);

        Dq_setTailNode(self, node);
        DqColl_setActiveCount(c, active + 1u);

        return elem;
    }

    return nullptr;
}

bool Deque_addFirstBytes(Deque *self, const_any elem, bytes elemBytes)
{
    if(!self || !elem)
        return false;

    if(elemBytes != Deque_stride(self)) {
        THROW("Deque_addFirst: width %zu does not match '%s' stride %zu",
              elemBytes, Deque_typeName(self), Deque_stride(self));

        return false;
    }

    any slot = Deque_emplaceFirst(self);

    if(!slot)
        return false;

    memcpy(slot, elem, elemBytes);

    return true;
}

bool Deque_addLastBytes(Deque *self, const_any elem, bytes elemBytes)
{
    if(!self || !elem)
        return false;

    if(elemBytes != Deque_stride(self)) {
        THROW("Deque_addLast: width %zu does not match '%s' stride %zu",
              elemBytes, Deque_typeName(self), Deque_stride(self));

        return false;
    }

    any slot = Deque_emplaceLast(self);

    if(!slot)
        return false;

    memcpy(slot, elem, elemBytes);

    return true;
}

;;HOTCODE
any Deque_peekFirst(const Deque *self)
{
    if(!self)
        return nullptr;

    const Collection *c = (const Collection*) self;

    if(DqColl_activeCount(c) == 0)
        return nullptr;

    if(Deque_kind(self) == DEQUE_KIND_ARRAY) {
        u8 *buf = DqColl_data(c);

        if(!buf)
            return nullptr;

        return buf + ((bytes) DqColl_head(c) * Deque_stride(self));
    }

    if(Deque_kind(self) == DEQUE_KIND_LINKED) {
        DequeNode *head = Dq_headNode(self);

        if(!head)
            return nullptr;

        return dqNodeElem(head);
    }

    return nullptr;
}

;;HOTCODE
any Deque_peekLast(const Deque *self)
{
    if(!self)
        return nullptr;

    const Collection *c = (const Collection*) self;
    u32 active = DqColl_activeCount(c);

    if(active == 0)
        return nullptr;

    if(Deque_kind(self) == DEQUE_KIND_ARRAY) {
        u32 cap = DqColl_capacity(c);
        u8 *buf = DqColl_data(c);

        if(!buf || cap == 0)
            return nullptr;

        bytes phys = ((bytes) DqColl_head(c) + (bytes) active - 1u) % (bytes) cap;

        return buf + (phys * Deque_stride(self));
    }

    if(Deque_kind(self) == DEQUE_KIND_LINKED) {
        DequeNode *tail = Dq_tailNode(self);

        if(!tail)
            return nullptr;

        return dqNodeElem(tail);
    }

    return nullptr;
}

bool Deque_peekFirstCopyOut(const Deque *self, any dest, bytes elemBytes, string typeName)
{
    if(!self || !dest)
        return false;

    if(elemBytes != Deque_stride(self)) {
        THROW("Deque_peekFirstAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Deque_typeName(self), Deque_stride(self));

        return false;
    }

    any slot = Deque_peekFirst(self);

    if(!slot)
        return false;

    memcpy(dest, slot, elemBytes);

    return true;
}

bool Deque_peekLastCopyOut(const Deque *self, any dest, bytes elemBytes, string typeName)
{
    if(!self || !dest)
        return false;

    if(elemBytes != Deque_stride(self)) {
        THROW("Deque_peekLastAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Deque_typeName(self), Deque_stride(self));

        return false;
    }

    any slot = Deque_peekLast(self);

    if(!slot)
        return false;

    memcpy(dest, slot, elemBytes);

    return true;
}

bool Deque_popFirstCopyOut(Deque *self, any dest, bytes elemBytes, string typeName)
{
    if(!self)
        return false;

    if(dest && elemBytes != Deque_stride(self)) {
        THROW("Deque_popFirstAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Deque_typeName(self), Deque_stride(self));

        return false;
    }

    Collection *c = (Collection*) self;
    u32 active = DqColl_activeCount(c);

    if(active == 0)
        return false;

    bytes stride = Deque_stride(self);

    if(Deque_kind(self) == DEQUE_KIND_ARRAY) {
        u32 cap = DqColl_capacity(c);
        u32 head = DqColl_head(c);
        u8 *buf = DqColl_data(c);

        if(!buf || cap == 0)
            return false;

        if(dest)
            memcpy(dest, buf + ((bytes) head * stride), stride);

        u32 nextHead = head + 1u;

        if(nextHead >= cap)
            nextHead = 0;

        DqColl_setHead(c, active == 1u ? 0u : nextHead);
        DqColl_setActiveCount(c, active - 1u);

        return true;
    }

    if(Deque_kind(self) == DEQUE_KIND_LINKED) {
        DequeNode *head = Dq_headNode(self);

        if(!head)
            return false;

        if(dest)
            memcpy(dest, dqNodeElem(head), stride);

        DequeNode *next = DqNode_next(head);
        Dq_setHeadNode(self, next);

        if(next)
            DqNode_setPrev(next, nullptr);
        else
            Dq_setTailNode(self, nullptr);

        DqColl_setActiveCount(c, active - 1u);
        MemoryArena_free(Deque_arena(self), head);

        return true;
    }

    return false;
}

bool Deque_popLastCopyOut(Deque *self, any dest, bytes elemBytes, string typeName)
{
    if(!self)
        return false;

    if(dest && elemBytes != Deque_stride(self)) {
        THROW("Deque_popLastAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Deque_typeName(self), Deque_stride(self));

        return false;
    }

    Collection *c = (Collection*) self;
    u32 active = DqColl_activeCount(c);

    if(active == 0)
        return false;

    bytes stride = Deque_stride(self);

    if(Deque_kind(self) == DEQUE_KIND_ARRAY) {
        u32 cap = DqColl_capacity(c);
        u8 *buf = DqColl_data(c);

        if(!buf || cap == 0)
            return false;

        bytes phys = ((bytes) DqColl_head(c) + (bytes) active - 1u) % (bytes) cap;

        if(dest)
            memcpy(dest, buf + (phys * stride), stride);

        DqColl_setActiveCount(c, active - 1u);

        if(active == 1u)
            DqColl_setHead(c, 0);

        return true;
    }

    if(Deque_kind(self) == DEQUE_KIND_LINKED) {
        DequeNode *tail = Dq_tailNode(self);

        if(!tail)
            return false;

        if(dest)
            memcpy(dest, dqNodeElem(tail), stride);

        DequeNode *prev = DqNode_prev(tail);
        Dq_setTailNode(self, prev);

        if(prev)
            DqNode_setNext(prev, nullptr);
        else
            Dq_setHeadNode(self, nullptr);

        DqColl_setActiveCount(c, active - 1u);
        MemoryArena_free(Deque_arena(self), tail);

        return true;
    }

    return false;
}

bool Deque_removeFirst(Deque *self)
{
    return Deque_popFirstCopyOut(self, nullptr, 0, nullptr);
}

bool Deque_removeLast(Deque *self)
{
    return Deque_popLastCopyOut(self, nullptr, 0, nullptr);
}

;;HOTCODE
any Deque_get(const Deque *self, bytes index)
{
    if(!self)
        return nullptr;

    const Collection *c = (const Collection*) self;
    u32 active = DqColl_activeCount(c);

    if(index >= (bytes) active)
        return nullptr;

    if(Deque_kind(self) == DEQUE_KIND_ARRAY) {
        u32 cap = DqColl_capacity(c);
        u8 *buf = DqColl_data(c);

        if(!buf || cap == 0)
            return nullptr;

        bytes phys = ((bytes) DqColl_head(c) + index) % (bytes) cap;

        return buf + (phys * Deque_stride(self));
    }

    if(Deque_kind(self) == DEQUE_KIND_LINKED) {
        DequeNode *cur = nullptr;

        if(index <= ((bytes) active >> 1u)) {
            cur = Dq_headNode(self);
            bytes i = 0;

            while(cur && i < index) {
                cur = DqNode_next(cur);
                i++;
            }
        } else {
            cur = Dq_tailNode(self);
            bytes steps = (bytes) active - 1u - index;

            while(cur && steps > 0) {
                cur = DqNode_prev(cur);
                steps--;
            }
        }

        if(!cur)
            return nullptr;

        return dqNodeElem(cur);
    }

    return nullptr;
}

bool Deque_copyOut(const Deque *self, bytes index, any dest, bytes elemBytes, string typeName)
{
    if(!self || !dest)
        return false;

    if(elemBytes != Deque_stride(self)) {
        THROW("Deque_getAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Deque_typeName(self), Deque_stride(self));

        return false;
    }

    any slot = Deque_get(self, index);

    if(!slot)
        return false;

    memcpy(dest, slot, elemBytes);

    return true;
}

fn Deque_clear(Deque *self)
{
    if(!self)
        return;

    Collection *c = (Collection*) self;

    if(Deque_kind(self) == DEQUE_KIND_LINKED) {
        MemoryArena *arena = Deque_arena(self);
        DequeNode *cur = Dq_headNode(self);

        while(cur) {
            DequeNode *next = DqNode_next(cur);
            MemoryArena_free(arena, cur);
            cur = next;
        }

        Dq_setHeadNode(self, nullptr);
        Dq_setTailNode(self, nullptr);
    }

    DqColl_setActiveCount(c, 0);
    DqColl_setHead(c, 0);
}

bool Deque_reserve(Deque *self, bytes minCapacity)
{
    if(!self)
        return false;

    if(Deque_kind(self) == DEQUE_KIND_LINKED)
        return true;

    if(Deque_kind(self) != DEQUE_KIND_ARRAY)
        return false;

    Collection *c = (Collection*) self;

    if(minCapacity <= (bytes) DqColl_capacity(c))
        return true;

    return arrayDequeGrow(self, minCapacity);
}

;;DECONSTRUCT
fn Deque_free(Deque *self)
{
    if(!self)
        return;

    MemoryArena *arena = Deque_arena(self);
    Collection *c = (Collection*) self;

    if(Deque_kind(self) == DEQUE_KIND_LINKED) {
        Deque_clear(self);
    } else if(Deque_kind(self) == DEQUE_KIND_ARRAY) {
        u8 *buf = DqColl_data(c);

        if(buf) {
            MemoryArena_free(arena, buf);
            DqColl_setData(c, nullptr);
        }
    }

    MemoryArena_free(arena, self);
}

bytes Deque_count(const Deque *self)
{
    if(!self)
        return 0;

    return (bytes) DqColl_activeCount((const Collection*) self);
}

bytes Deque_size(const Deque *self)
{
    return Deque_count(self);
}

bytes Deque_length(const Deque *self)
{
    return Deque_count(self);
}

bytes Deque_capacity(const Deque *self)
{
    if(!self)
        return 0;

    if(Deque_kind(self) == DEQUE_KIND_LINKED)
        return Deque_count(self);

    return (bytes) DqColl_capacity((const Collection*) self);
}

bool Deque_isEmpty(const Deque *self)
{
    return Deque_count(self) == 0;
}

bytes Deque_stride(const Deque *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(Deque_layout(self));
}

string Deque_typeName(const Deque *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(Deque_layout(self));
}

string Deque_kindName(const Deque *self)
{
    DequeKind k = Deque_kind(self);

    if(k == DEQUE_KIND_ARRAY)
        return "ArrayDeque";

    if(k == DEQUE_KIND_LINKED)
        return "LinkedDeque";

    return "UnknownDeque";
}

;;TEST
u64 Deque_exhaustionCount(const Deque *self)
{
    if(!self)
        return 0;

    return Collection_exhaustionCount((const Collection*) self);
}

fn Deque_toString(const Deque *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 needed = snprintf(dest, cap, "Deque(nullptr)");
        formatWrite(dest, cap, outTruncated, needed);

        return;
    }

    i32 needed = snprintf(dest, cap, "%s<%s>(count=%zu, capacity=%zu)",
                          Deque_kindName(self),
                          Deque_typeName(self),
                          Deque_count(self),
                          Deque_capacity(self));
    formatWrite(dest, cap, outTruncated, needed);
}

fn Deque_toStringStruct(const Deque *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 needed = snprintf(dest, cap, "Deque{nullptr}");
        formatWrite(dest, cap, outTruncated, needed);

        return;
    }

    i32 needed = snprintf(dest, cap,
                          "Deque{kind=%s, type=%s, stride=%zu, count=%zu, capacity=%zu, exhausted=%llu}",
                          Deque_kindName(self),
                          Deque_typeName(self),
                          Deque_stride(self),
                          Deque_count(self),
                          Deque_capacity(self),
                          (u64) Deque_exhaustionCount(self));
    formatWrite(dest, cap, outTruncated, needed);
}
