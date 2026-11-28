#include "vex.h"
#include <stdckdint.h>

#include "atomic/atomic.h"
#include "nio/mem.h"
#include "struct/exhaustion.h"
#include "struct/linked_list.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: LinkedList
 * ============================================================================
 * Doubly-linked arena-backed implementation of the polymorphic List interface
 * owned by Relational Engine (lifetime tier). Each node allocates a 32-byte
 * LinkedListNode header followed immediately by ElementLayout.stride payload
 * bytes inside a single MemoryArena allocation, preserving the 32-byte
 * alignment floor for every payload and guaranteeing stable element addresses
 * across insertions and removals. Indexed walks traverse from whichever end
 * (head or tail) is closer.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: LinkedList (struct/linked_list.c)
 * ============================================================================
 * Doubly-linked List backing with O(1) head/tail operations and stable pointers.
 *
 * --FIELDS-------------------------------
 * public   List            base            embedded polymorphic List, 1:1
 * private  LinkedListNode *head            owns LinkedListNode chain, 1:N
 * private  LinkedListNode *tail            borrows tail LinkedListNode, 1:1
 *
 * --CONSTRUCTORS-------------------------
 * public   LinkedList_newLayout(...)       allocates LinkedList in arena
 *
 * --FUNCTIONS----------------------------
 * public   LinkedList_isLinkedList(...)    kind check against LIST_KIND_LINKED
 * public   LinkedList_get(...)             bidirectional indexed node lookup
 * public   LinkedList_peekFirst(...)       O(1) pointer to head payload
 * public   LinkedList_peekLast(...)        O(1) pointer to tail payload
 * public   LinkedList_emplaceFirst(...)    O(1) prepend zeroed node
 * public   LinkedList_emplaceLast(...)     O(1) append zeroed node
 * public   LinkedList_addFirstBytes(...)   O(1) stride-checked prepend
 * public   LinkedList_addLastBytes(...)    O(1) stride-checked append
 * public   LinkedList_setBytes(...)        stride-checked overwrite at index
 * public   LinkedList_insertBytes(...)     stride-checked insert at index
 * public   LinkedList_removeFirst(...)     O(1) unlink and free head node
 * public   LinkedList_removeLast(...)      O(1) unlink and free tail node
 * public   LinkedList_remove(...)          unlink and free node at index
 * public   LinkedList_clear(...)           frees all nodes, keeps handle live
 * public   LinkedList_free(...)            frees all nodes and handle
 * public   LinkedList_toString(...)        cold value projection
 * public   LinkedList_toStringStruct(...)  cold structure projection
 * private  linkedListOps(...)              lazy thread-safe vtable singleton
 * private  nodeAlloc(...)                  allocates zeroed node + payload
 * private  nodePayload(...)                returns payload pointer past header
 * private  nodeAt(...)                     bidirectional index traversal
 * private  unlinkNode(...)                 unlinks and frees one node
 * private  linkedListReserve(...)          no-op reserve callback
 * private  linkedListCount(...)            vtable count callback
 *
 * --GETTERS------------------------------
 * private  LlNode_prev(...)                reads LinkedListNode.prev
 * private  LlNode_next(...)                reads LinkedListNode.next
 * private  Ll_head(...)                    reads LinkedList.head
 * private  Ll_tail(...)                    reads LinkedList.tail
 * private  LlColl_activeCount(...)         reads Collection.activeCount
 *
 * --SETTERS------------------------------
 * private  LlNode_setPrev(...)             writes LinkedListNode.prev
 * private  LlNode_setNext(...)             writes LinkedListNode.next
 * private  Ll_setHead(...)                 writes LinkedList.head
 * private  Ll_setTail(...)                 writes LinkedList.tail
 * private  LlColl_setActiveCount(...)      writes Collection.activeCount
 * private  LlColl_setCapacity(...)         writes Collection.capacity
 * ============================================================================
 */

static ListOps s_linkedListOps;
static SpinLock s_linkedListOpsLock = SPIN_LOCK_INIT;
static atomic_bool s_linkedListOpsReady = false;

;;GETTER
static inline LinkedListNode *LlNode_prev(const LinkedListNode *node)
{
    return (*node).prev;
}

;;SETTER
static inline fn LlNode_setPrev(LinkedListNode *node, LinkedListNode *prev)
{
    (*node).prev = prev;
}

;;GETTER
static inline LinkedListNode *LlNode_next(const LinkedListNode *node)
{
    return (*node).next;
}

;;SETTER
static inline fn LlNode_setNext(LinkedListNode *node, LinkedListNode *next)
{
    (*node).next = next;
}

;;GETTER
static inline LinkedListNode *Ll_head(const LinkedList *ll)
{
    return (*ll).head;
}

;;SETTER
static inline fn Ll_setHead(LinkedList *ll, LinkedListNode *head)
{
    (*ll).head = head;
}

;;GETTER
static inline LinkedListNode *Ll_tail(const LinkedList *ll)
{
    return (*ll).tail;
}

;;SETTER
static inline fn Ll_setTail(LinkedList *ll, LinkedListNode *tail)
{
    (*ll).tail = tail;
}

;;GETTER
static inline u32 LlColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;SETTER
static inline fn LlColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn LlColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

static inline any nodePayload(LinkedListNode *node)
{
    if(!node)
        return nullptr;

    return ((u8*) node) + sizeof(LinkedListNode);
}

static LinkedListNode *nodeAlloc(List *self)
{
    Collection *c = (Collection*) self;
    bytes active = (bytes) LlColl_activeCount(c);

    if(active >= (bytes) UINT32_MAX) {
        (discard) Collection_reportExhaustion(c, active + 1u);

        return nullptr;
    }

    bytes stride = List_stride(self);
    bytes totalBytes = 0;

    if(ckd_add(&totalBytes, sizeof(LinkedListNode), stride)) {
        (discard) Collection_reportExhaustion(c, SIZE_MAX);

        return nullptr;
    }

    MemoryArena *arena = List_arena(self);
    LinkedListNode *node = (LinkedListNode*) MemoryArena_alloc(arena, TYPE_LINKED_LIST_NODE, totalBytes);

    if(!node) {
        (discard) Collection_reportExhaustion(c, totalBytes);

        return nullptr;
    }

    memset(node, 0, totalBytes);

    return node;
}

static LinkedListNode *nodeAt(const LinkedList *ll, bytes index)
{
    bytes count = (bytes) LlColl_activeCount((const Collection*) ll);

    if(index >= count)
        return nullptr;

    if(index <= (count / 2u)) {
        LinkedListNode *cur = Ll_head(ll);

        for(bytes i = 0; i < index && cur; i++)
            cur = LlNode_next(cur);

        return cur;
    }

    LinkedListNode *cur = Ll_tail(ll);
    bytes steps = count - 1u - index;

    for(bytes i = 0; i < steps && cur; i++)
        cur = LlNode_prev(cur);

    return cur;
}

static fn unlinkNode(LinkedList *ll, LinkedListNode *node)
{
    LinkedListNode *prev = LlNode_prev(node);
    LinkedListNode *next = LlNode_next(node);

    if(prev) {
        LlNode_setNext(prev, next);
    } else {
        Ll_setHead(ll, next);
    }

    if(next) {
        LlNode_setPrev(next, prev);
    } else {
        Ll_setTail(ll, prev);
    }

    Collection *c = (Collection*) ll;
    u32 active = LlColl_activeCount(c);

    if(active > 0) {
        u32 nextCount = active - 1u;
        LlColl_setActiveCount(c, nextCount);
        LlColl_setCapacity(c, nextCount);
    }

    (discard) MemoryArena_free(List_arena((const List*) ll), node);
}

static bool linkedListReserve(List *self, bytes minCapacity)
{
    (discard) minCapacity;

    return LinkedList_isLinkedList(self);
}

static bytes linkedListCount(const List *self)
{
    if(!self)
        return 0;

    return (bytes) LlColl_activeCount((const Collection*) self);
}

static const ListOps *linkedListOps()
{
    if(atomic_load_explicit(&s_linkedListOpsReady, memory_order_acquire))
        return &s_linkedListOps;

    SpinLock_lock(&s_linkedListOpsLock);

    if(!atomic_load_explicit(&s_linkedListOpsReady, memory_order_relaxed)) {
        ListOps_setKind(&s_linkedListOps, LIST_KIND_LINKED);
        ListOps_setKindName(&s_linkedListOps, "LinkedList");
        ListOps_setGet(&s_linkedListOps, LinkedList_get);
        ListOps_setSetBytes(&s_linkedListOps, LinkedList_setBytes);
        ListOps_setEmplace(&s_linkedListOps, LinkedList_emplaceLast);
        ListOps_setAddBytes(&s_linkedListOps, LinkedList_addLastBytes);
        ListOps_setInsertBytes(&s_linkedListOps, LinkedList_insertBytes);
        ListOps_setRemoveAt(&s_linkedListOps, LinkedList_remove);
        ListOps_setClear(&s_linkedListOps, LinkedList_clear);
        ListOps_setReserve(&s_linkedListOps, linkedListReserve);
        ListOps_setCount(&s_linkedListOps, linkedListCount);
        ListOps_setCapacity(&s_linkedListOps, linkedListCount);
        ListOps_setDestroy(&s_linkedListOps, LinkedList_free);
        atomic_store_explicit(&s_linkedListOpsReady, true, memory_order_release);
    }

    SpinLock_unlock(&s_linkedListOpsLock);

    return &s_linkedListOps;
}

;;CONSTRUCTOR
List *LinkedList_newLayout(ElementLayout layout, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("LinkedList: invalid layout for '%s' (stride=%zu, align=%zu)",
              ElementLayout_typeName(layout),
              ElementLayout_stride(layout),
              ElementLayout_alignment(layout));

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    LinkedList *obj = (LinkedList*) MemoryArena_alloc(targetArena, TYPE_LINKED_LIST, sizeof(LinkedList));

    if(!obj) {
        (discard) Struct_reportConstructionExhaustion("LinkedList", sizeof(LinkedList));

        return nullptr;
    }

    List *list = (List*) obj;

    if(!List_initBase(list, TYPE_LINKED_LIST, linkedListOps(), layout, targetArena)) {
        (discard) MemoryArena_free(targetArena, obj);

        return nullptr;
    }

    Ll_setHead(obj, nullptr);
    Ll_setTail(obj, nullptr);

    return list;
}

bool LinkedList_isLinkedList(const List *self)
{
    if(!self)
        return false;

    return List_kind(self) == LIST_KIND_LINKED;
}

;;HOTCODE
any LinkedList_get(const List *self, bytes index)
{
    if(!LinkedList_isLinkedList(self))
        return nullptr;

    LinkedListNode *node = nodeAt((const LinkedList*) self, index);

    return nodePayload(node);
}

;;HOTCODE
any LinkedList_peekFirst(const List *self)
{
    if(!LinkedList_isLinkedList(self))
        return nullptr;

    return nodePayload(Ll_head((const LinkedList*) self));
}

;;HOTCODE
any LinkedList_peekLast(const List *self)
{
    if(!LinkedList_isLinkedList(self))
        return nullptr;

    return nodePayload(Ll_tail((const LinkedList*) self));
}

any LinkedList_emplaceFirst(List *self)
{
    if(!LinkedList_isLinkedList(self))
        return nullptr;

    LinkedListNode *node = nodeAlloc(self);

    if(!node)
        return nullptr;

    LinkedList *ll = (LinkedList*) self;
    LinkedListNode *oldHead = Ll_head(ll);

    LlNode_setPrev(node, nullptr);
    LlNode_setNext(node, oldHead);

    if(oldHead) {
        LlNode_setPrev(oldHead, node);
    } else {
        Ll_setTail(ll, node);
    }

    Ll_setHead(ll, node);

    Collection *c = (Collection*) self;
    u32 nextCount = LlColl_activeCount(c) + 1u;
    LlColl_setActiveCount(c, nextCount);
    LlColl_setCapacity(c, nextCount);

    return nodePayload(node);
}

any LinkedList_emplaceLast(List *self)
{
    if(!LinkedList_isLinkedList(self))
        return nullptr;

    LinkedListNode *node = nodeAlloc(self);

    if(!node)
        return nullptr;

    LinkedList *ll = (LinkedList*) self;
    LinkedListNode *oldTail = Ll_tail(ll);

    LlNode_setPrev(node, oldTail);
    LlNode_setNext(node, nullptr);

    if(oldTail) {
        LlNode_setNext(oldTail, node);
    } else {
        Ll_setHead(ll, node);
    }

    Ll_setTail(ll, node);

    Collection *c = (Collection*) self;
    u32 nextCount = LlColl_activeCount(c) + 1u;
    LlColl_setActiveCount(c, nextCount);
    LlColl_setCapacity(c, nextCount);

    return nodePayload(node);
}

bool LinkedList_addFirstBytes(List *self, const_any src, bytes elemBytes)
{
    if(!LinkedList_isLinkedList(self) || !src)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("LinkedList_addFirst: value width %zu does not match '%s' stride %zu",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    any slot = LinkedList_emplaceFirst(self);

    if(!slot)
        return false;

    memcpy(slot, src, stride);

    return true;
}

bool LinkedList_addLastBytes(List *self, const_any src, bytes elemBytes)
{
    if(!LinkedList_isLinkedList(self) || !src)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("LinkedList_addLast: value width %zu does not match '%s' stride %zu",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    any slot = LinkedList_emplaceLast(self);

    if(!slot)
        return false;

    memcpy(slot, src, stride);

    return true;
}

bool LinkedList_setBytes(List *self, bytes index, const_any src, bytes elemBytes)
{
    if(!LinkedList_isLinkedList(self) || !src)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("LinkedList_set: value width %zu does not match '%s' stride %zu",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    any slot = LinkedList_get(self, index);

    if(!slot)
        return false;

    memcpy(slot, src, stride);

    return true;
}

bool LinkedList_insertBytes(List *self, bytes index, const_any src, bytes elemBytes)
{
    if(!LinkedList_isLinkedList(self) || !src)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("LinkedList_insert: value width %zu does not match '%s' stride %zu",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    bytes active = (bytes) LlColl_activeCount((const Collection*) self);

    if(index > active)
        return false;

    if(index == 0)
        return LinkedList_addFirstBytes(self, src, elemBytes);

    if(index == active)
        return LinkedList_addLastBytes(self, src, elemBytes);

    LinkedList *ll = (LinkedList*) self;
    LinkedListNode *target = nodeAt(ll, index);

    if(!target)
        return false;

    LinkedListNode *node = nodeAlloc(self);

    if(!node)
        return false;

    LinkedListNode *prev = LlNode_prev(target);
    LlNode_setPrev(node, prev);
    LlNode_setNext(node, target);
    LlNode_setPrev(target, node);

    if(prev) {
        LlNode_setNext(prev, node);
    } else {
        Ll_setHead(ll, node);
    }

    memcpy(nodePayload(node), src, stride);

    Collection *c = (Collection*) self;
    u32 nextCount = (u32) (active + 1u);
    LlColl_setActiveCount(c, nextCount);
    LlColl_setCapacity(c, nextCount);

    return true;
}

bool LinkedList_removeFirst(List *self)
{
    if(!LinkedList_isLinkedList(self))
        return false;

    LinkedList *ll = (LinkedList*) self;
    LinkedListNode *head = Ll_head(ll);

    if(!head)
        return false;

    unlinkNode(ll, head);

    return true;
}

bool LinkedList_removeLast(List *self)
{
    if(!LinkedList_isLinkedList(self))
        return false;

    LinkedList *ll = (LinkedList*) self;
    LinkedListNode *tail = Ll_tail(ll);

    if(!tail)
        return false;

    unlinkNode(ll, tail);

    return true;
}

bool LinkedList_remove(List *self, bytes index)
{
    if(!LinkedList_isLinkedList(self))
        return false;

    LinkedList *ll = (LinkedList*) self;
    LinkedListNode *node = nodeAt(ll, index);

    if(!node)
        return false;

    unlinkNode(ll, node);

    return true;
}

fn LinkedList_clear(List *self)
{
    if(!LinkedList_isLinkedList(self))
        return;

    LinkedList *ll = (LinkedList*) self;
    MemoryArena *arena = List_arena(self);
    LinkedListNode *cur = Ll_head(ll);

    while(cur) {
        LinkedListNode *next = LlNode_next(cur);
        (discard) MemoryArena_free(arena, cur);
        cur = next;
    }

    Ll_setHead(ll, nullptr);
    Ll_setTail(ll, nullptr);

    Collection *c = (Collection*) self;
    LlColl_setActiveCount(c, 0);
    LlColl_setCapacity(c, 0);
}

;;DECONSTRUCT
fn LinkedList_free(List *self)
{
    if(!self)
        return;

    if(!LinkedList_isLinkedList(self)) {
        List_free(self);

        return;
    }

    LinkedList_clear(self);
    (discard) MemoryArena_free(List_arena(self), self);
}

fn LinkedList_toString(const List *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    List_toString(self, dest, cap, outTruncated);
}

fn LinkedList_toStringStruct(const List *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    List_toStringStruct(self, dest, cap, outTruncated);
}
