#include "vex.h"
#include <stdckdint.h>

#include "atomic/atomic.h"
#include "nio/mem.h"
#include "struct/array_list.h"
#include "struct/exhaustion.h"
#include "struct/linked_hash_set.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: LinkedHashSet
 * ============================================================================
 * Insertion-ordered hash set implementing the polymorphic Set interface. Each
 * element resides in an immovable 32-byte-aligned LinkedHashSetNode carrying
 * [prev, next, hash, pad] followed by inline element bytes, indexed by a
 * power-of-two open-addressing pointer table. Provides O(1) add/contains/get/
 * remove/first/last, pointer stability across rehashes, and deterministic
 * insertion-order traversal in Set_toList and Set_forEach.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: LinkedHashSet (struct/linked_hash_set.c)
 * ============================================================================
 * Insertion-order-preserving Set backed by hash index + doubly-linked nodes.
 *
 * --FIELDS-------------------------------
 * public   Set                 base        embedded polymorphic Set, 1:1
 * private  LinkedHashSetNode **table       owns open-addressing index, 1:N
 * private  LinkedHashSetNode  *head        owns doubly-linked node chain, 1:N
 * private  LinkedHashSetNode  *tail        borrows newest entry node, 1:1
 * private  bytes               nodeBytes   total byte allocation per node
 * private  u32                 tombstones  deleted index slot count
 *
 * --CONSTRUCTORS-------------------------
 * public   LinkedHashSet_newLayout(...)    constructs LinkedHashSet in arena
 *
 * --FUNCTIONS----------------------------
 * public   LinkedHashSet_isLinkedHashSet(...) checks SET_KIND_LINKED_HASH
 * public   LinkedHashSet_first(...)        O(1) oldest element pointer
 * public   LinkedHashSet_last(...)         O(1) newest element pointer
 * public   LinkedHashSet_addBytes(...)     O(1) unique insert at tail
 * public   LinkedHashSet_containsBytes(...) O(1) membership predicate
 * public   LinkedHashSet_getBytes(...)     O(1) element slot pointer lookup
 * public   LinkedHashSet_removeBytes(...)  O(1) unlink + free node
 * public   LinkedHashSet_clear(...)        frees all nodes, resets index
 * public   LinkedHashSet_reserve(...)      grows index table
 * public   LinkedHashSet_toList(...)       exports elements in insertion order
 * public   LinkedHashSet_forEach(...)      visits elements in insertion order
 * public   LinkedHashSet_free(...)         frees nodes, index, and handle
 * private  linkedHashSetOps(...)           lazy thread-safe vtable singleton
 * private  lhsRehash(...)                  rebuilds pointer index table
 *
 * --GETTERS------------------------------
 * private  LhsNode_* / Lhs_* / LhsColl_*   internal field readers
 *
 * --SETTERS------------------------------
 * private  LhsNode_set* / Lhs_set* / LhsColl_set* internal field writers
 * ============================================================================
 */

#define LHS_TOMBSTONE ((LinkedHashSetNode*) (pointer) 1u)

static SetOps s_linkedHashSetOps;
static SpinLock s_linkedHashSetOpsLock = SPIN_LOCK_INIT;
static atomic_bool s_linkedHashSetOpsReady = false;

;;GETTER
static inline LinkedHashSetNode *LhsNode_prev(const LinkedHashSetNode *n)
{
    return (*n).prev;
}

;;GETTER
static inline LinkedHashSetNode *LhsNode_next(const LinkedHashSetNode *n)
{
    return (*n).next;
}

;;GETTER
static inline u64 LhsNode_hash(const LinkedHashSetNode *n)
{
    return (*n).hash;
}

;;SETTER
static inline fn LhsNode_setPrev(LinkedHashSetNode *n, LinkedHashSetNode *prev)
{
    (*n).prev = prev;
}

;;SETTER
static inline fn LhsNode_setNext(LinkedHashSetNode *n, LinkedHashSetNode *next)
{
    (*n).next = next;
}

;;SETTER
static inline fn LhsNode_setHash(LinkedHashSetNode *n, u64 hash)
{
    (*n).hash = hash;
}

;;SETTER
static inline fn LhsNode_setPad(LinkedHashSetNode *n, u64 pad)
{
    (*n).pad = pad;
}

;;GETTER
static inline LinkedHashSetNode **Lhs_table(const LinkedHashSet *s)
{
    return (*s).table;
}

;;GETTER
static inline LinkedHashSetNode *Lhs_head(const LinkedHashSet *s)
{
    return (*s).head;
}

;;GETTER
static inline LinkedHashSetNode *Lhs_tail(const LinkedHashSet *s)
{
    return (*s).tail;
}

;;GETTER
static inline bytes Lhs_nodeBytes(const LinkedHashSet *s)
{
    return (*s).nodeBytes;
}

;;GETTER
static inline u32 Lhs_tombstones(const LinkedHashSet *s)
{
    return (*s).tombstones;
}

;;SETTER
static inline fn Lhs_setTable(LinkedHashSet *s, LinkedHashSetNode **table)
{
    (*s).table = table;
}

;;SETTER
static inline fn Lhs_setHead(LinkedHashSet *s, LinkedHashSetNode *head)
{
    (*s).head = head;
}

;;SETTER
static inline fn Lhs_setTail(LinkedHashSet *s, LinkedHashSetNode *tail)
{
    (*s).tail = tail;
}

;;SETTER
static inline fn Lhs_setNodeBytes(LinkedHashSet *s, bytes nodeBytes)
{
    (*s).nodeBytes = nodeBytes;
}

;;SETTER
static inline fn Lhs_setTombstones(LinkedHashSet *s, u32 tombs)
{
    (*s).tombstones = tombs;
}

;;GETTER
static inline u32 LhsColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;GETTER
static inline u32 LhsColl_capacity(const Collection *c)
{
    return (*c).capacity;
}

;;SETTER
static inline fn LhsColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn LhsColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

static inline u8 *lhsNodeElem(LinkedHashSetNode *node)
{
    return ((u8*) node) + sizeof(LinkedHashSetNode);
}

static bytes pow2Capacity(bytes requested)
{
    bytes cap = 4u;

    while(cap < requested) {
        if(cap > ((bytes) UINT32_MAX >> 1u))
            return 0;

        cap <<= 1u;
    }

    return cap;
}

static bool lhsRehash(LinkedHashSet *lhs, bytes requestedCap)
{
    Set *self = (Set*) lhs;
    Collection *c = (Collection*) lhs;
    bytes newCap = pow2Capacity(requestedCap);

    if(newCap == 0 || newCap > (bytes) UINT32_MAX)
        return Collection_reportExhaustion(c, 0);

    bytes allocBytes = 0;

    if(ckd_mul(&allocBytes, newCap, sizeof(LinkedHashSetNode*)))
        return Collection_reportExhaustion(c, 0);

    MemoryArena *arena = Set_arena(self);
    LinkedHashSetNode **newTable = (LinkedHashSetNode**) Memory_alloc(
        TYPE_BYTE_ARRAY, arena, allocBytes);

    if(!newTable)
        return Collection_reportExhaustion(c, allocBytes);

    memset(newTable, 0, allocBytes);

    bytes mask = newCap - 1u;
    LinkedHashSetNode *cur = Lhs_head(lhs);

    while(cur) {
        bytes idx = (bytes) LhsNode_hash(cur) & mask;

        for(bytes step = 0; step < newCap; step++) {
            if(!newTable[idx]) {
                newTable[idx] = cur;
                break;
            }

            idx = (idx + 1u) & mask;
        }

        cur = LhsNode_next(cur);
    }

    LinkedHashSetNode **oldTable = Lhs_table(lhs);

    if(oldTable)
        MemoryArena_free(arena, oldTable);

    Lhs_setTable(lhs, newTable);
    LhsColl_setCapacity(c, (u32) newCap);
    Lhs_setTombstones(lhs, 0);

    return true;
}

static bytes linkedHashSetCount(const Set *self)
{
    if(!self)
        return 0;

    return (bytes) LhsColl_activeCount((const Collection*) self);
}

static bytes linkedHashSetCapacity(const Set *self)
{
    if(!self)
        return 0;

    return (bytes) LhsColl_capacity((const Collection*) self);
}

static const SetOps *linkedHashSetOps()
{
    if(atomic_load_explicit(&s_linkedHashSetOpsReady, memory_order_acquire))
        return &s_linkedHashSetOps;

    SpinLock_lock(&s_linkedHashSetOpsLock);

    if(!atomic_load_explicit(&s_linkedHashSetOpsReady, memory_order_relaxed)) {
        SetOps_setKind(&s_linkedHashSetOps, SET_KIND_LINKED_HASH);
        SetOps_setKindName(&s_linkedHashSetOps, "LinkedHashSet");
        SetOps_setAddBytes(&s_linkedHashSetOps, LinkedHashSet_addBytes);
        SetOps_setContainsBytes(&s_linkedHashSetOps, LinkedHashSet_containsBytes);
        SetOps_setGetBytes(&s_linkedHashSetOps, LinkedHashSet_getBytes);
        SetOps_setRemoveBytes(&s_linkedHashSetOps, LinkedHashSet_removeBytes);
        SetOps_setClear(&s_linkedHashSetOps, LinkedHashSet_clear);
        SetOps_setReserve(&s_linkedHashSetOps, LinkedHashSet_reserve);
        SetOps_setToList(&s_linkedHashSetOps, LinkedHashSet_toList);
        SetOps_setForEach(&s_linkedHashSetOps, LinkedHashSet_forEach);
        SetOps_setCount(&s_linkedHashSetOps, linkedHashSetCount);
        SetOps_setCapacity(&s_linkedHashSetOps, linkedHashSetCapacity);
        SetOps_setDestroy(&s_linkedHashSetOps, LinkedHashSet_free);
        atomic_store_explicit(&s_linkedHashSetOpsReady, true, memory_order_release);
    }

    SpinLock_unlock(&s_linkedHashSetOpsLock);

    return &s_linkedHashSetOps;
}

;;CONSTRUCTOR
Set *LinkedHashSet_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("LinkedHashSet: invalid layout for '%s'", ElementLayout_typeName(layout));

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    bytes req = initialCapacity < 4u ? 4u : initialCapacity;
    bytes cap = pow2Capacity(req);

    if(cap == 0 || cap > (bytes) UINT32_MAX) {
        THROW("LinkedHashSet: initial capacity %zu overflows u32", initialCapacity);

        return nullptr;
    }

    bytes nodeBytes = sizeof(LinkedHashSetNode) + ElementLayout_stride(layout);
    bytes tableBytes = 0;

    if(ckd_mul(&tableBytes, cap, sizeof(LinkedHashSetNode*))) {
        THROW("LinkedHashSet: initial table overflow");

        return nullptr;
    }

    LinkedHashSet *lhs = (LinkedHashSet*) Memory_alloc(
        TYPE_LINKED_HASH_SET, targetArena, sizeof(LinkedHashSet));

    if(!lhs)
        return nullptr;

    Set *base = (Set*) lhs;

    if(!Set_initBase(base, TYPE_LINKED_HASH_SET, linkedHashSetOps(), layout, targetArena)) {
        MemoryArena_free(targetArena, lhs);

        return nullptr;
    }

    LinkedHashSetNode **table = (LinkedHashSetNode**) Memory_alloc(
        TYPE_BYTE_ARRAY, targetArena, tableBytes);

    if(!table) {
        MemoryArena_free(targetArena, lhs);

        return nullptr;
    }

    memset(table, 0, tableBytes);

    Collection *c = (Collection*) lhs;
    Lhs_setTable(lhs, table);
    Lhs_setHead(lhs, nullptr);
    Lhs_setTail(lhs, nullptr);
    Lhs_setNodeBytes(lhs, nodeBytes);
    Lhs_setTombstones(lhs, 0);
    LhsColl_setCapacity(c, (u32) cap);

    return base;
}

bool LinkedHashSet_isLinkedHashSet(const Set *self)
{
    return Set_kind(self) == SET_KIND_LINKED_HASH;
}

;;HOTCODE
any LinkedHashSet_first(const Set *self)
{
    if(!LinkedHashSet_isLinkedHashSet(self))
        return nullptr;

    LinkedHashSetNode *head = Lhs_head((const LinkedHashSet*) self);

    if(!head)
        return nullptr;

    return lhsNodeElem(head);
}

;;HOTCODE
any LinkedHashSet_last(const Set *self)
{
    if(!LinkedHashSet_isLinkedHashSet(self))
        return nullptr;

    LinkedHashSetNode *tail = Lhs_tail((const LinkedHashSet*) self);

    if(!tail)
        return nullptr;

    return lhsNodeElem(tail);
}

bool LinkedHashSet_addBytes(Set *self, const_any elem, bytes elemBytes)
{
    if(!LinkedHashSet_isLinkedHashSet(self) || !elem || elemBytes != Set_stride(self))
        return false;

    LinkedHashSet *lhs = (LinkedHashSet*) self;
    Collection *c = (Collection*) self;
    ElementLayout layout = Set_layout(self);
    u32 cap = LhsColl_capacity(c);
    u32 active = LhsColl_activeCount(c);
    u32 tombs = Lhs_tombstones(lhs);

    if(cap == 0 || ((bytes) active + (bytes) tombs + 1u) * 4u >= (bytes) cap * 3u) {
        bytes nextCap = cap == 0 ? 4u : ((bytes) active * 2u >= (bytes) cap ? (bytes) cap * 2u : (bytes) cap);

        if(!lhsRehash(lhs, nextCap))
            return false;

        cap = LhsColl_capacity(c);
    }

    LinkedHashSetNode **table = Lhs_table(lhs);
    u64 hash = ElementLayout_hash(layout, elem);
    bytes mask = (bytes) cap - 1u;
    bytes idx = (bytes) hash & mask;
    bytes firstTomb = (bytes) cap;

    for(u32 step = 0; step < cap; step++) {
        LinkedHashSetNode *slot = table[idx];

        if(!slot) {
            bytes targetIdx = firstTomb < (bytes) cap ? firstTomb : idx;
            LinkedHashSetNode *node = (LinkedHashSetNode*) Memory_alloc(
                TYPE_LINKED_HASH_SET_NODE, Set_arena(self), Lhs_nodeBytes(lhs));

            if(!node)
                return Collection_reportExhaustion(c, Lhs_nodeBytes(lhs));

            LinkedHashSetNode *oldTail = Lhs_tail(lhs);
            LhsNode_setPrev(node, oldTail);
            LhsNode_setNext(node, nullptr);
            LhsNode_setHash(node, hash);
            LhsNode_setPad(node, 0);
            memcpy(lhsNodeElem(node), elem, elemBytes);

            if(oldTail)
                LhsNode_setNext(oldTail, node);
            else
                Lhs_setHead(lhs, node);

            Lhs_setTail(lhs, node);

            if(table[targetIdx] == LHS_TOMBSTONE && Lhs_tombstones(lhs) > 0)
                Lhs_setTombstones(lhs, Lhs_tombstones(lhs) - 1u);

            table[targetIdx] = node;
            LhsColl_setActiveCount(c, active + 1u);

            return true;
        }

        if(slot == LHS_TOMBSTONE) {
            if(firstTomb == (bytes) cap)
                firstTomb = idx;
        } else if(LhsNode_hash(slot) == hash
                  && ElementLayout_bytesEqual(layout, lhsNodeElem(slot), elem)) {
            return false;
        }

        idx = (idx + 1u) & mask;
    }

    if(firstTomb < (bytes) cap) {
        LinkedHashSetNode *node = (LinkedHashSetNode*) Memory_alloc(
            TYPE_LINKED_HASH_SET_NODE, Set_arena(self), Lhs_nodeBytes(lhs));

        if(!node)
            return Collection_reportExhaustion(c, Lhs_nodeBytes(lhs));

        LinkedHashSetNode *oldTail = Lhs_tail(lhs);
        LhsNode_setPrev(node, oldTail);
        LhsNode_setNext(node, nullptr);
        LhsNode_setHash(node, hash);
        LhsNode_setPad(node, 0);
        memcpy(lhsNodeElem(node), elem, elemBytes);

        if(oldTail)
            LhsNode_setNext(oldTail, node);
        else
            Lhs_setHead(lhs, node);

        Lhs_setTail(lhs, node);

        if(Lhs_tombstones(lhs) > 0)
            Lhs_setTombstones(lhs, Lhs_tombstones(lhs) - 1u);

        table[firstTomb] = node;
        LhsColl_setActiveCount(c, active + 1u);

        return true;
    }

    return false;
}

;;HOTCODE
any LinkedHashSet_getBytes(const Set *self, const_any elem, bytes elemBytes)
{
    if(!LinkedHashSet_isLinkedHashSet(self) || !elem || elemBytes != Set_stride(self))
        return nullptr;

    const LinkedHashSet *lhs = (const LinkedHashSet*) self;
    const Collection *c = (const Collection*) self;
    u32 cap = LhsColl_capacity(c);
    LinkedHashSetNode **table = Lhs_table(lhs);

    if(cap == 0 || !table || LhsColl_activeCount(c) == 0)
        return nullptr;

    ElementLayout layout = Set_layout(self);
    u64 hash = ElementLayout_hash(layout, elem);
    bytes mask = (bytes) cap - 1u;
    bytes idx = (bytes) hash & mask;

    for(u32 step = 0; step < cap; step++) {
        LinkedHashSetNode *slot = table[idx];

        if(!slot)
            return nullptr;

        if(slot != LHS_TOMBSTONE
           && LhsNode_hash(slot) == hash
           && ElementLayout_bytesEqual(layout, lhsNodeElem(slot), elem)) {
            return lhsNodeElem(slot);
        }

        idx = (idx + 1u) & mask;
    }

    return nullptr;
}

;;HOTCODE
bool LinkedHashSet_containsBytes(const Set *self, const_any elem, bytes elemBytes)
{
    return LinkedHashSet_getBytes(self, elem, elemBytes) != nullptr;
}

bool LinkedHashSet_removeBytes(Set *self, const_any elem, bytes elemBytes)
{
    if(!LinkedHashSet_isLinkedHashSet(self) || !elem || elemBytes != Set_stride(self))
        return false;

    LinkedHashSet *lhs = (LinkedHashSet*) self;
    Collection *c = (Collection*) self;
    u32 cap = LhsColl_capacity(c);
    LinkedHashSetNode **table = Lhs_table(lhs);

    if(cap == 0 || !table || LhsColl_activeCount(c) == 0)
        return false;

    ElementLayout layout = Set_layout(self);
    u64 hash = ElementLayout_hash(layout, elem);
    bytes mask = (bytes) cap - 1u;
    bytes idx = (bytes) hash & mask;

    for(u32 step = 0; step < cap; step++) {
        LinkedHashSetNode *slot = table[idx];

        if(!slot)
            return false;

        if(slot != LHS_TOMBSTONE
           && LhsNode_hash(slot) == hash
           && ElementLayout_bytesEqual(layout, lhsNodeElem(slot), elem)) {
            LinkedHashSetNode *prev = LhsNode_prev(slot);
            LinkedHashSetNode *next = LhsNode_next(slot);

            if(prev)
                LhsNode_setNext(prev, next);
            else
                Lhs_setHead(lhs, next);

            if(next)
                LhsNode_setPrev(next, prev);
            else
                Lhs_setTail(lhs, prev);

            table[idx] = LHS_TOMBSTONE;
            LhsColl_setActiveCount(c, LhsColl_activeCount(c) - 1u);
            Lhs_setTombstones(lhs, Lhs_tombstones(lhs) + 1u);
            MemoryArena_free(Set_arena(self), slot);

            return true;
        }

        idx = (idx + 1u) & mask;
    }

    return false;
}

fn LinkedHashSet_clear(Set *self)
{
    if(!LinkedHashSet_isLinkedHashSet(self))
        return;

    LinkedHashSet *lhs = (LinkedHashSet*) self;
    Collection *c = (Collection*) self;
    MemoryArena *arena = Set_arena(self);
    LinkedHashSetNode *cur = Lhs_head(lhs);

    while(cur) {
        LinkedHashSetNode *next = LhsNode_next(cur);
        MemoryArena_free(arena, cur);
        cur = next;
    }

    Lhs_setHead(lhs, nullptr);
    Lhs_setTail(lhs, nullptr);
    LhsColl_setActiveCount(c, 0);
    Lhs_setTombstones(lhs, 0);

    LinkedHashSetNode **table = Lhs_table(lhs);
    u32 cap = LhsColl_capacity(c);

    if(table && cap > 0)
        memset(table, 0, (bytes) cap * sizeof(LinkedHashSetNode*));
}

bool LinkedHashSet_reserve(Set *self, bytes minCapacity)
{
    if(!LinkedHashSet_isLinkedHashSet(self))
        return false;

    LinkedHashSet *lhs = (LinkedHashSet*) self;
    Collection *c = (Collection*) self;
    bytes neededSlots = 0;

    if(ckd_mul(&neededSlots, minCapacity, (bytes) 4u))
        return Collection_reportExhaustion(c, 0);

    neededSlots = (neededSlots + 2u) / 3u;

    if(neededSlots <= (bytes) LhsColl_capacity(c))
        return true;

    return lhsRehash(lhs, neededSlots);
}

List *LinkedHashSet_toList(const Set *self)
{
    if(!LinkedHashSet_isLinkedHashSet(self))
        return nullptr;

    const LinkedHashSet *lhs = (const LinkedHashSet*) self;
    const Collection *c = (const Collection*) self;
    u32 active = LhsColl_activeCount(c);
    ElementLayout layout = Set_layout(self);
    List *out = ArrayList_newLayout(layout, active < 4u ? 4u : active, Set_arena(self));

    if(!out)
        return nullptr;

    bytes stride = Set_stride(self);
    LinkedHashSetNode *cur = Lhs_head(lhs);

    while(cur) {
        if(!List_addBytes(out, lhsNodeElem(cur), stride)) {
            List_free(out);

            return nullptr;
        }

        cur = LhsNode_next(cur);
    }

    return out;
}

fn LinkedHashSet_forEach(Set *self, SetVisitFn visit, any userdata)
{
    if(!LinkedHashSet_isLinkedHashSet(self) || !visit)
        return;

    LinkedHashSet *lhs = (LinkedHashSet*) self;
    LinkedHashSetNode *cur = Lhs_head(lhs);
    bytes ordinal = 0;

    while(cur) {
        LinkedHashSetNode *next = LhsNode_next(cur);
        visit(lhsNodeElem(cur), ordinal, userdata);
        ordinal++;
        cur = next;
    }
}

;;DECONSTRUCT
fn LinkedHashSet_free(Set *self)
{
    if(!LinkedHashSet_isLinkedHashSet(self))
        return;

    LinkedHashSet *lhs = (LinkedHashSet*) self;
    MemoryArena *arena = Set_arena(self);
    LinkedHashSet_clear(self);

    LinkedHashSetNode **table = Lhs_table(lhs);

    if(table) {
        MemoryArena_free(arena, table);
        Lhs_setTable(lhs, nullptr);
    }

    MemoryArena_free(arena, self);
}
