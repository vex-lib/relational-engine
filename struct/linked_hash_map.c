#include "vex.h"
#include <stdckdint.h>

#include "atomic/atomic.h"
#include "nio/mem.h"
#include "struct/array_list.h"
#include "struct/exhaustion.h"
#include "struct/linked_hash_map.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: LinkedHashMap
 * ============================================================================
 * Insertion-ordered hash map implementing the polymorphic Map interface.
 * Each entry lives in an immovable 32-byte-aligned LinkedHashMapNode carrying
 * [prev, next, hash, pad] followed by aligned inline key and value bytes,
 * indexed by a power-of-two open-addressing pointer table. Provides O(1)
 * lookup/put/remove/first/last, pointer stability across rehashes, and exact
 * insertion-order traversal in Map_keys, Map_values, and Map_forEach.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: LinkedHashMap (struct/linked_hash_map.c)
 * ============================================================================
 * Insertion-order-preserving Map backed by hash index + doubly-linked nodes.
 *
 * --FIELDS-------------------------------
 * public   Map                 base        embedded polymorphic Map, 1:1
 * private  LinkedHashMapNode **table       owns open-addressing index, 1:N
 * private  LinkedHashMapNode  *head        owns doubly-linked node chain, 1:N
 * private  LinkedHashMapNode  *tail        borrows newest entry node, 1:1
 * private  bytes               valOffset   aligned value offset inside node
 * private  bytes               nodeBytes   total byte allocation per node
 * private  u32                 tombstones  deleted index slot count
 *
 * --CONSTRUCTORS-------------------------
 * public   LinkedHashMap_newLayout(...)    constructs LinkedHashMap in arena
 *
 * --FUNCTIONS----------------------------
 * public   LinkedHashMap_isLinkedHashMap(...) checks MAP_KIND_LINKED_HASH
 * public   LinkedHashMap_firstKey(...)     O(1) oldest key pointer
 * public   LinkedHashMap_lastKey(...)      O(1) newest key pointer
 * public   LinkedHashMap_firstValue(...)   O(1) oldest value pointer
 * public   LinkedHashMap_lastValue(...)    O(1) newest value pointer
 * public   LinkedHashMap_putBytes(...)     insert or update in O(1)
 * public   LinkedHashMap_emplaceBytes(...) get-or-insert zeroed value slot
 * public   LinkedHashMap_getBytes(...)     O(1) value lookup
 * public   LinkedHashMap_containsKeyBytes(...) presence check
 * public   LinkedHashMap_removeBytes(...)  O(1) unlink + free node
 * public   LinkedHashMap_clear(...)        frees all nodes, resets index
 * public   LinkedHashMap_reserve(...)      grows index table
 * public   LinkedHashMap_keys(...)         exports keys in insertion order
 * public   LinkedHashMap_values(...)       exports values in insertion order
 * public   LinkedHashMap_forEach(...)      visits entries in insertion order
 * public   LinkedHashMap_free(...)         frees all nodes, index, and handle
 * private  linkedHashMapOps(...)           lazy thread-safe vtable singleton
 * private  lhmRehash(...)                  rebuilds pointer index table
 *
 * --GETTERS------------------------------
 * private  LhmNode_* / Lhm_* / LhmColl_*   internal field readers
 *
 * --SETTERS------------------------------
 * private  LhmNode_set* / Lhm_set* / LhmColl_set* internal field writers
 * ============================================================================
 */

#define LHM_TOMBSTONE ((LinkedHashMapNode*) (pointer) 1u)

static MapOps s_linkedHashMapOps;
static SpinLock s_linkedHashMapOpsLock = SPIN_LOCK_INIT;
static atomic_bool s_linkedHashMapOpsReady = false;

;;GETTER
static inline LinkedHashMapNode *LhmNode_prev(const LinkedHashMapNode *n)
{
    return (*n).prev;
}

;;SETTER
static inline fn LhmNode_setPrev(LinkedHashMapNode *n, LinkedHashMapNode *prev)
{
    (*n).prev = prev;
}

;;GETTER
static inline LinkedHashMapNode *LhmNode_next(const LinkedHashMapNode *n)
{
    return (*n).next;
}

;;SETTER
static inline fn LhmNode_setNext(LinkedHashMapNode *n, LinkedHashMapNode *next)
{
    (*n).next = next;
}

;;GETTER
static inline u64 LhmNode_hash(const LinkedHashMapNode *n)
{
    return (*n).hash;
}

;;SETTER
static inline fn LhmNode_setHash(LinkedHashMapNode *n, u64 hash)
{
    (*n).hash = hash;
}

;;GETTER
static inline LinkedHashMapNode **Lhm_table(const LinkedHashMap *m)
{
    return (*m).table;
}

;;SETTER
static inline fn Lhm_setTable(LinkedHashMap *m, LinkedHashMapNode **table)
{
    (*m).table = table;
}

;;GETTER
static inline LinkedHashMapNode *Lhm_head(const LinkedHashMap *m)
{
    return (*m).head;
}

;;SETTER
static inline fn Lhm_setHead(LinkedHashMap *m, LinkedHashMapNode *head)
{
    (*m).head = head;
}

;;GETTER
static inline LinkedHashMapNode *Lhm_tail(const LinkedHashMap *m)
{
    return (*m).tail;
}

;;SETTER
static inline fn Lhm_setTail(LinkedHashMap *m, LinkedHashMapNode *tail)
{
    (*m).tail = tail;
}

;;GETTER
static inline bytes Lhm_valOffset(const LinkedHashMap *m)
{
    return (*m).valOffset;
}

;;SETTER
static inline fn Lhm_setValOffset(LinkedHashMap *m, bytes off)
{
    (*m).valOffset = off;
}

;;GETTER
static inline bytes Lhm_nodeBytes(const LinkedHashMap *m)
{
    return (*m).nodeBytes;
}

;;SETTER
static inline fn Lhm_setNodeBytes(LinkedHashMap *m, bytes nb)
{
    (*m).nodeBytes = nb;
}

;;GETTER
static inline u32 Lhm_tombstones(const LinkedHashMap *m)
{
    return (*m).tombstones;
}

;;SETTER
static inline fn Lhm_setTombstones(LinkedHashMap *m, u32 t)
{
    (*m).tombstones = t;
}

;;GETTER
static inline u32 LhmColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;SETTER
static inline fn LhmColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;GETTER
static inline u32 LhmColl_capacity(const Collection *c)
{
    return (*c).capacity;
}

;;SETTER
static inline fn LhmColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

static inline any lhmNodeKey(LinkedHashMapNode *n)
{
    return ((u8*) n) + sizeof(LinkedHashMapNode);
}

static inline any lhmNodeVal(const LinkedHashMap *m, LinkedHashMapNode *n)
{
    return ((u8*) n) + Lhm_valOffset(m);
}

static bytes lhmPow2(bytes requested)
{
    bytes cap = 8u;

    while(cap < requested && cap <= ((bytes) UINT32_MAX / 2u))
        cap *= 2u;

    if(cap < requested)
        return 0;

    return cap;
}

static bool lhmRehash(LinkedHashMap *m, bytes newCap)
{
    if(newCap == 0 || newCap > (bytes) UINT32_MAX) {
        (discard) Collection_reportExhaustion((Collection*) m, SIZE_MAX);

        return false;
    }

    bytes byteCount = 0;

    if(ckd_mul(&byteCount, newCap, sizeof(LinkedHashMapNode*))) {
        (discard) Collection_reportExhaustion((Collection*) m, SIZE_MAX);

        return false;
    }

    MemoryArena *arena = Map_arena((const Map*) m);
    LinkedHashMapNode **nextTable = (LinkedHashMapNode**) MemoryArena_alloc(arena, TYPE_LINKED_HASH_MAP, byteCount);

    if(!nextTable) {
        (discard) Collection_reportExhaustion((Collection*) m, byteCount);

        return false;
    }

    memset(nextTable, 0, byteCount);

    bytes mask = newCap - 1u;
    LinkedHashMapNode *cur = Lhm_head(m);

    while(cur) {
        bytes idx = (bytes) (LhmNode_hash(cur) & mask);

        while(nextTable[idx] != nullptr)
            idx = (idx + 1u) & mask;

        nextTable[idx] = cur;
        cur = LhmNode_next(cur);
    }

    LinkedHashMapNode **oldTable = Lhm_table(m);

    if(oldTable)
        (discard) MemoryArena_free(arena, oldTable);

    Lhm_setTable(m, nextTable);
    LhmColl_setCapacity((Collection*) m, (u32) newCap);
    Lhm_setTombstones(m, 0);

    return true;
}

static bool lhmEnsureLoad(LinkedHashMap *m)
{
    Collection *c = (Collection*) m;
    bytes cap = (bytes) LhmColl_capacity(c);
    bytes active = (bytes) LhmColl_activeCount(c);
    bytes used = active + (bytes) Lhm_tombstones(m);
    bytes limit = cap - (cap / 4u);

    if(cap > 0 && used < limit)
        return true;

    bytes targetCap = cap == 0 ? (bytes) LINKED_HASH_MAP_DEFAULT_CAPACITY : cap;

    if(active >= (cap / 2u)) {
        if(targetCap > ((bytes) UINT32_MAX / 2u))
            return false;

        targetCap *= 2u;
    }

    return lhmRehash(m, targetCap);
}

static bytes lhmCount(const Map *self)
{
    if(!self)
        return 0;

    return (bytes) LhmColl_activeCount((const Collection*) self);
}

static bytes lhmCapacity(const Map *self)
{
    if(!self)
        return 0;

    return (bytes) LhmColl_capacity((const Collection*) self);
}

static const MapOps *linkedHashMapOps()
{
    if(atomic_load_explicit(&s_linkedHashMapOpsReady, memory_order_acquire))
        return &s_linkedHashMapOps;

    SpinLock_lock(&s_linkedHashMapOpsLock);

    if(!atomic_load_explicit(&s_linkedHashMapOpsReady, memory_order_relaxed)) {
        MapOps_setKind(&s_linkedHashMapOps, MAP_KIND_LINKED_HASH);
        MapOps_setKindName(&s_linkedHashMapOps, "LinkedHashMap");
        MapOps_setPutBytes(&s_linkedHashMapOps, LinkedHashMap_putBytes);
        MapOps_setEmplaceBytes(&s_linkedHashMapOps, LinkedHashMap_emplaceBytes);
        MapOps_setGetBytes(&s_linkedHashMapOps, LinkedHashMap_getBytes);
        MapOps_setContainsBytes(&s_linkedHashMapOps, LinkedHashMap_containsKeyBytes);
        MapOps_setRemoveBytes(&s_linkedHashMapOps, LinkedHashMap_removeBytes);
        MapOps_setClear(&s_linkedHashMapOps, LinkedHashMap_clear);
        MapOps_setReserve(&s_linkedHashMapOps, LinkedHashMap_reserve);
        MapOps_setKeys(&s_linkedHashMapOps, LinkedHashMap_keys);
        MapOps_setValues(&s_linkedHashMapOps, LinkedHashMap_values);
        MapOps_setForEach(&s_linkedHashMapOps, LinkedHashMap_forEach);
        MapOps_setCount(&s_linkedHashMapOps, lhmCount);
        MapOps_setCapacity(&s_linkedHashMapOps, lhmCapacity);
        MapOps_setDestroy(&s_linkedHashMapOps, LinkedHashMap_free);
        atomic_store_explicit(&s_linkedHashMapOpsReady, true, memory_order_release);
    }

    SpinLock_unlock(&s_linkedHashMapOpsLock);

    return &s_linkedHashMapOps;
}

;;CONSTRUCTOR
Map *LinkedHashMap_newLayout(ElementLayout keyLayout, ElementLayout valLayout,
                             bytes initialCapacity, MemoryArena *arena)
{
    if(!ElementLayout_isValid(keyLayout) || !ElementLayout_isValid(valLayout)) {
        THROW("LinkedHashMap: invalid layout for <%s, %s>",
              ElementLayout_typeName(keyLayout),
              ElementLayout_typeName(valLayout));

        return nullptr;
    }

    bytes req = initialCapacity == 0 ? (bytes) LINKED_HASH_MAP_DEFAULT_CAPACITY : initialCapacity;
    bytes cap = lhmPow2(req);

    if(cap == 0) {
        (discard) Struct_reportConstructionExhaustion("LinkedHashMap", req);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    LinkedHashMap *m = (LinkedHashMap*) MemoryArena_alloc(targetArena, TYPE_LINKED_HASH_MAP, sizeof(LinkedHashMap));

    if(!m) {
        (discard) Struct_reportConstructionExhaustion("LinkedHashMap", sizeof(LinkedHashMap));

        return nullptr;
    }

    Map *map = (Map*) m;

    if(!Map_initBase(map, TYPE_LINKED_HASH_MAP, linkedHashMapOps(), keyLayout, valLayout, targetArena)) {
        (discard) MemoryArena_free(targetArena, m);

        return nullptr;
    }

    bytes valOff = ElementLayout_alignUp(sizeof(LinkedHashMapNode) + ElementLayout_stride(keyLayout),
                                         ElementLayout_alignment(valLayout));
    bytes nodeBytes = valOff + ElementLayout_stride(valLayout);

    Lhm_setTable(m, nullptr);
    Lhm_setHead(m, nullptr);
    Lhm_setTail(m, nullptr);
    Lhm_setValOffset(m, valOff);
    Lhm_setNodeBytes(m, nodeBytes);
    Lhm_setTombstones(m, 0);

    if(!lhmRehash(m, cap)) {
        (discard) Struct_reportConstructionExhaustion("LinkedHashMap", cap * sizeof(LinkedHashMapNode*));
        (discard) MemoryArena_free(targetArena, m);

        return nullptr;
    }

    return map;
}

bool LinkedHashMap_isLinkedHashMap(const Map *self)
{
    if(!self)
        return false;

    return Map_kind(self) == MAP_KIND_LINKED_HASH;
}

any LinkedHashMap_firstKey(const Map *self)
{
    if(!LinkedHashMap_isLinkedHashMap(self))
        return nullptr;

    LinkedHashMapNode *head = Lhm_head((const LinkedHashMap*) self);

    if(!head)
        return nullptr;

    return lhmNodeKey(head);
}

any LinkedHashMap_lastKey(const Map *self)
{
    if(!LinkedHashMap_isLinkedHashMap(self))
        return nullptr;

    LinkedHashMapNode *tail = Lhm_tail((const LinkedHashMap*) self);

    if(!tail)
        return nullptr;

    return lhmNodeKey(tail);
}

any LinkedHashMap_firstValue(const Map *self)
{
    if(!LinkedHashMap_isLinkedHashMap(self))
        return nullptr;

    const LinkedHashMap *m = (const LinkedHashMap*) self;
    LinkedHashMapNode *head = Lhm_head(m);

    if(!head)
        return nullptr;

    return lhmNodeVal(m, head);
}

any LinkedHashMap_lastValue(const Map *self)
{
    if(!LinkedHashMap_isLinkedHashMap(self))
        return nullptr;

    const LinkedHashMap *m = (const LinkedHashMap*) self;
    LinkedHashMapNode *tail = Lhm_tail(m);

    if(!tail)
        return nullptr;

    return lhmNodeVal(m, tail);
}

any LinkedHashMap_emplaceBytes(Map *self, const_any key, bytes keyBytes, bool *outInserted)
{
    if(outInserted)
        *outInserted = false;

    if(!LinkedHashMap_isLinkedHashMap(self) || !key)
        return nullptr;

    ElementLayout keyLayout = Map_keyLayout(self);

    if(keyBytes != ElementLayout_stride(keyLayout))
        return nullptr;

    LinkedHashMap *m = (LinkedHashMap*) self;

    if(!lhmEnsureLoad(m))
        return nullptr;

    Collection *c = (Collection*) self;
    bytes cap = (bytes) LhmColl_capacity(c);
    LinkedHashMapNode **table = Lhm_table(m);
    u64 hash = ElementLayout_hash(keyLayout, key);
    bytes mask = cap - 1u;
    bytes idx = (bytes) (hash & mask);
    bytes firstDeleted = SIZE_MAX;

    for(bytes probe = 0; probe < cap; probe++) {
        LinkedHashMapNode *entry = table[idx];

        if(entry == nullptr) {
            bytes targetIdx = firstDeleted != SIZE_MAX ? firstDeleted : idx;
            bytes nb = Lhm_nodeBytes(m);
            LinkedHashMapNode *node = (LinkedHashMapNode*) MemoryArena_alloc(Map_arena(self),
                                                                             TYPE_LINKED_HASH_MAP_NODE,
                                                                             nb);

            if(!node) {
                (discard) Collection_reportExhaustion(c, nb);

                return nullptr;
            }

            memset(node, 0, nb);
            LhmNode_setHash(node, hash);
            memcpy(lhmNodeKey(node), key, keyBytes);

            LinkedHashMapNode *oldTail = Lhm_tail(m);
            LhmNode_setPrev(node, oldTail);
            LhmNode_setNext(node, nullptr);

            if(oldTail) {
                LhmNode_setNext(oldTail, node);
            } else {
                Lhm_setHead(m, node);
            }

            Lhm_setTail(m, node);

            if(firstDeleted != SIZE_MAX)
                Lhm_setTombstones(m, Lhm_tombstones(m) - 1u);

            table[targetIdx] = node;
            LhmColl_setActiveCount(c, LhmColl_activeCount(c) + 1u);

            if(outInserted)
                *outInserted = true;

            return lhmNodeVal(m, node);
        }

        if(entry == LHM_TOMBSTONE) {
            if(firstDeleted == SIZE_MAX)
                firstDeleted = idx;
        } else if(LhmNode_hash(entry) == hash
                  && ElementLayout_bytesEqual(keyLayout, lhmNodeKey(entry), key)) {
            return lhmNodeVal(m, entry);
        }

        idx = (idx + 1u) & mask;
    }

    return nullptr;
}

bool LinkedHashMap_putBytes(Map *self, const_any key, bytes keyBytes, const_any val, bytes valBytes)
{
    if(!LinkedHashMap_isLinkedHashMap(self) || !key || !val)
        return false;

    if(valBytes != Map_valStride(self))
        return false;

    any valSlot = LinkedHashMap_emplaceBytes(self, key, keyBytes, nullptr);

    if(!valSlot)
        return false;

    memcpy(valSlot, val, valBytes);

    return true;
}

;;HOTCODE
any LinkedHashMap_getBytes(const Map *self, const_any key, bytes keyBytes)
{
    if(!LinkedHashMap_isLinkedHashMap(self) || !key)
        return nullptr;

    ElementLayout keyLayout = Map_keyLayout(self);

    if(keyBytes != ElementLayout_stride(keyLayout))
        return nullptr;

    const LinkedHashMap *m = (const LinkedHashMap*) self;
    bytes cap = (bytes) LhmColl_capacity((const Collection*) self);
    LinkedHashMapNode **table = Lhm_table(m);

    if(cap == 0 || !table)
        return nullptr;

    u64 hash = ElementLayout_hash(keyLayout, key);
    bytes mask = cap - 1u;
    bytes idx = (bytes) (hash & mask);

    for(bytes probe = 0; probe < cap; probe++) {
        LinkedHashMapNode *entry = table[idx];

        if(entry == nullptr)
            return nullptr;

        if(entry != LHM_TOMBSTONE
           && LhmNode_hash(entry) == hash
           && ElementLayout_bytesEqual(keyLayout, lhmNodeKey(entry), key))
            return lhmNodeVal(m, entry);

        idx = (idx + 1u) & mask;
    }

    return nullptr;
}

bool LinkedHashMap_containsKeyBytes(const Map *self, const_any key, bytes keyBytes)
{
    return LinkedHashMap_getBytes(self, key, keyBytes) != nullptr;
}

bool LinkedHashMap_removeBytes(Map *self, const_any key, bytes keyBytes)
{
    if(!LinkedHashMap_isLinkedHashMap(self) || !key)
        return false;

    ElementLayout keyLayout = Map_keyLayout(self);

    if(keyBytes != ElementLayout_stride(keyLayout))
        return false;

    LinkedHashMap *m = (LinkedHashMap*) self;
    Collection *c = (Collection*) self;
    bytes cap = (bytes) LhmColl_capacity(c);
    LinkedHashMapNode **table = Lhm_table(m);

    if(cap == 0 || !table)
        return false;

    u64 hash = ElementLayout_hash(keyLayout, key);
    bytes mask = cap - 1u;
    bytes idx = (bytes) (hash & mask);

    for(bytes probe = 0; probe < cap; probe++) {
        LinkedHashMapNode *entry = table[idx];

        if(entry == nullptr)
            return false;

        if(entry != LHM_TOMBSTONE
           && LhmNode_hash(entry) == hash
           && ElementLayout_bytesEqual(keyLayout, lhmNodeKey(entry), key)) {
            LinkedHashMapNode *prev = LhmNode_prev(entry);
            LinkedHashMapNode *next = LhmNode_next(entry);

            if(prev) {
                LhmNode_setNext(prev, next);
            } else {
                Lhm_setHead(m, next);
            }

            if(next) {
                LhmNode_setPrev(next, prev);
            } else {
                Lhm_setTail(m, prev);
            }

            table[idx] = LHM_TOMBSTONE;
            LhmColl_setActiveCount(c, LhmColl_activeCount(c) - 1u);
            Lhm_setTombstones(m, Lhm_tombstones(m) + 1u);
            (discard) MemoryArena_free(Map_arena(self), entry);

            return true;
        }

        idx = (idx + 1u) & mask;
    }

    return false;
}

fn LinkedHashMap_clear(Map *self)
{
    if(!LinkedHashMap_isLinkedHashMap(self))
        return;

    LinkedHashMap *m = (LinkedHashMap*) self;
    MemoryArena *arena = Map_arena(self);
    LinkedHashMapNode *cur = Lhm_head(m);

    while(cur) {
        LinkedHashMapNode *next = LhmNode_next(cur);
        (discard) MemoryArena_free(arena, cur);
        cur = next;
    }

    Lhm_setHead(m, nullptr);
    Lhm_setTail(m, nullptr);

    Collection *c = (Collection*) self;
    bytes cap = (bytes) LhmColl_capacity(c);
    LinkedHashMapNode **table = Lhm_table(m);

    if(table && cap > 0)
        memset(table, 0, cap * sizeof(LinkedHashMapNode*));

    LhmColl_setActiveCount(c, 0);
    Lhm_setTombstones(m, 0);
}

bool LinkedHashMap_reserve(Map *self, bytes minCapacity)
{
    if(!LinkedHashMap_isLinkedHashMap(self))
        return false;

    LinkedHashMap *m = (LinkedHashMap*) self;
    bytes neededSlots = minCapacity + (minCapacity / 3u) + 2u;
    bytes targetCap = lhmPow2(neededSlots);

    if(targetCap == 0) {
        (discard) Collection_reportExhaustion((Collection*) self, SIZE_MAX);

        return false;
    }

    if(targetCap <= (bytes) LhmColl_capacity((const Collection*) self))
        return true;

    return lhmRehash(m, targetCap);
}

List *LinkedHashMap_keys(const Map *self)
{
    if(!LinkedHashMap_isLinkedHashMap(self))
        return nullptr;

    const LinkedHashMap *m = (const LinkedHashMap*) self;
    bytes active = (bytes) LhmColl_activeCount((const Collection*) self);
    ElementLayout keyLayout = Map_keyLayout(self);
    List *out = ArrayList_newLayout(keyLayout, active, Map_arena(self));

    if(!out)
        return nullptr;

    bytes keyStride = ElementLayout_stride(keyLayout);
    LinkedHashMapNode *cur = Lhm_head(m);

    while(cur) {
        (discard) ArrayList_addBytes(out, lhmNodeKey(cur), keyStride);
        cur = LhmNode_next(cur);
    }

    return out;
}

List *LinkedHashMap_values(const Map *self)
{
    if(!LinkedHashMap_isLinkedHashMap(self))
        return nullptr;

    const LinkedHashMap *m = (const LinkedHashMap*) self;
    bytes active = (bytes) LhmColl_activeCount((const Collection*) self);
    ElementLayout valLayout = Map_valLayout(self);
    List *out = ArrayList_newLayout(valLayout, active, Map_arena(self));

    if(!out)
        return nullptr;

    bytes valStride = ElementLayout_stride(valLayout);
    LinkedHashMapNode *cur = Lhm_head(m);

    while(cur) {
        (discard) ArrayList_addBytes(out, lhmNodeVal(m, cur), valStride);
        cur = LhmNode_next(cur);
    }

    return out;
}

fn LinkedHashMap_forEach(Map *self, MapVisitFn visit, any userdata)
{
    if(!LinkedHashMap_isLinkedHashMap(self) || !visit)
        return;

    LinkedHashMap *m = (LinkedHashMap*) self;
    LinkedHashMapNode *cur = Lhm_head(m);
    bytes idx = 0;

    while(cur) {
        LinkedHashMapNode *next = LhmNode_next(cur);
        visit(lhmNodeKey(cur), lhmNodeVal(m, cur), idx, userdata);
        idx++;
        cur = next;
    }
}

;;DECONSTRUCT
fn LinkedHashMap_free(Map *self)
{
    if(!self)
        return;

    if(!LinkedHashMap_isLinkedHashMap(self)) {
        Map_free(self);

        return;
    }

    LinkedHashMap_clear(self);
    LinkedHashMap *m = (LinkedHashMap*) self;
    MemoryArena *arena = Map_arena(self);
    LinkedHashMapNode **table = Lhm_table(m);

    if(table) {
        Lhm_setTable(m, nullptr);
        (discard) MemoryArena_free(arena, table);
    }

    (discard) MemoryArena_free(arena, self);
}
