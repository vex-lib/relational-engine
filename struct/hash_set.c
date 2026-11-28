#include "vex.h"
#include <stdckdint.h>

#include "atomic/atomic.h"
#include "nio/mem.h"
#include "struct/array_list.h"
#include "struct/exhaustion.h"
#include "struct/hash_set.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: HashSet
 * ============================================================================
 * Array-backed open-addressing HashSet implementing the polymorphic Set
 * interface (default Set backing). Every slot stores [hash:8][state:1][pad]
 * [elem:stride] aligned to max(8, elemAlign) inside one flat 32-byte-aligned
 * MemoryArena buffer. Rehashes cleanly past 0.75 combined load factor so
 * deleted tombstones never wedge probe loops.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: HashSet (struct/hash_set.c)
 * ============================================================================
 * Flat open-addressing array HashSet over arbitrary inline element types.
 *
 * --CONSTANTS----------------------------
 * public   const u32 HASH_SET_DEFAULT_CAPACITY  default initial capacity (16)
 * private  const u8  HS_STATE_EMPTY             empty slot marker (0)
 * private  const u8  HS_STATE_OCCUPIED          live entry marker (1)
 * private  const u8  HS_STATE_DELETED           tombstone marker (2)
 *
 * --FIELDS-------------------------------
 * public   Set   base                           embedded polymorphic Set, 1:1
 * private  bytes elemOffset                     aligned element offset in slot
 * private  bytes slotStride                     aligned byte width of one slot
 * private  u32   tombstones                     live tombstone count
 *
 * --CONSTRUCTORS-------------------------
 * public   HashSet_newLayout(...)               constructs array-backed HashSet
 *
 * --FUNCTIONS----------------------------
 * public   HashSet_isHashSet(...)               checks SET_KIND_HASH
 * public   HashSet_addBytes(...)                inserts unique element
 * public   HashSet_containsBytes(...)           membership predicate
 * public   HashSet_getBytes(...)                linear-probing lookup
 * public   HashSet_removeBytes(...)             tombstone removal
 * public   HashSet_clear(...)                   zeros all slots
 * public   HashSet_reserve(...)                 grows table for minCapacity
 * public   HashSet_toList(...)                  exports elements as ArrayList
 * public   HashSet_forEach(...)                 visits occupied slots
 * public   HashSet_free(...)                    releases buffer and handle
 * private  hashSetOps(...)                      lazy thread-safe vtable singleton
 * private  hashSetRehash(...)                   allocates and re-indexes slots
 * private  pow2Capacity(...)                    power-of-two capacity rounding
 *
 * --GETTERS------------------------------
 * private  Hs_elemOffset / Hs_slotStride / Hs_tombstones
 * private  HsColl_activeCount / HsColl_capacity / HsColl_data
 *
 * --SETTERS------------------------------
 * private  Hs_setElemOffset / Hs_setSlotStride / Hs_setTombstones
 * private  HsColl_setActiveCount / HsColl_setCapacity / HsColl_setData
 * ============================================================================
 */

static const u8 HS_STATE_EMPTY = 0u;
static const u8 HS_STATE_OCCUPIED = 1u;
static const u8 HS_STATE_DELETED = 2u;

static SetOps s_hashSetOps;
static SpinLock s_hashSetOpsLock = SPIN_LOCK_INIT;
static atomic_bool s_hashSetOpsReady = false;

;;GETTER
static inline bytes Hs_elemOffset(const HashSet *hs)
{
    return (*hs).elemOffset;
}

;;GETTER
static inline bytes Hs_slotStride(const HashSet *hs)
{
    return (*hs).slotStride;
}

;;GETTER
static inline u32 Hs_tombstones(const HashSet *hs)
{
    return (*hs).tombstones;
}

;;SETTER
static inline fn Hs_setElemOffset(HashSet *hs, bytes offset)
{
    (*hs).elemOffset = offset;
}

;;SETTER
static inline fn Hs_setSlotStride(HashSet *hs, bytes stride)
{
    (*hs).slotStride = stride;
}

;;SETTER
static inline fn Hs_setTombstones(HashSet *hs, u32 tombstones)
{
    (*hs).tombstones = tombstones;
}

;;GETTER
static inline u32 HsColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;GETTER
static inline u32 HsColl_capacity(const Collection *c)
{
    return (*c).capacity;
}

;;GETTER
static inline u8 *HsColl_data(const Collection *c)
{
    return (*c).data;
}

;;SETTER
static inline fn HsColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn HsColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;SETTER
static inline fn HsColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
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

static inline u8 *hsSlotAt(u8 *data, bytes slotStride, bytes index)
{
    return data + (index * slotStride);
}

static inline u64 hsSlotHash(const u8 *slot)
{
    u64 hash = 0;
    memcpy(&hash, slot, sizeof(u64));

    return hash;
}

static inline fn hsSlotWriteHash(u8 *slot, u64 hash)
{
    memcpy(slot, &hash, sizeof(u64));
}

static inline u8 hsSlotState(const u8 *slot)
{
    return slot[sizeof(u64)];
}

static inline fn hsSlotWriteState(u8 *slot, u8 state)
{
    slot[sizeof(u64)] = state;
}

static inline u8 *hsSlotElem(u8 *slot, bytes elemOffset)
{
    return slot + elemOffset;
}

static bool hashSetRehash(HashSet *hs, bytes requestedCap)
{
    Set *self = (Set*) hs;
    Collection *c = (Collection*) hs;
    bytes newCap = pow2Capacity(requestedCap);

    if(newCap == 0 || newCap > (bytes) UINT32_MAX) {
        return Collection_reportExhaustion(c, 0);
    }

    bytes slotStride = Hs_slotStride(hs);
    bytes allocBytes = 0;

    if(ckd_mul(&allocBytes, newCap, slotStride))
        return Collection_reportExhaustion(c, 0);

    MemoryArena *arena = Set_arena(self);
    u8 *newBuf = (u8*) Memory_alloc(TYPE_BYTE_ARRAY, arena, allocBytes);

    if(!newBuf)
        return Collection_reportExhaustion(c, allocBytes);

    memset(newBuf, 0, allocBytes);

    u8 *oldBuf = HsColl_data(c);
    u32 oldCap = HsColl_capacity(c);
    bytes elemOffset = Hs_elemOffset(hs);
    bytes stride = Set_stride(self);
    bytes mask = newCap - 1u;

    for(u32 i = 0; i < oldCap; i++) {
        u8 *oldSlot = hsSlotAt(oldBuf, slotStride, i);

        if(hsSlotState(oldSlot) != HS_STATE_OCCUPIED)
            continue;

        u64 hash = hsSlotHash(oldSlot);
        bytes idx = (bytes) hash & mask;

        for(bytes step = 0; step < newCap; step++) {
            u8 *dst = hsSlotAt(newBuf, slotStride, idx);

            if(hsSlotState(dst) == HS_STATE_EMPTY) {
                hsSlotWriteHash(dst, hash);
                hsSlotWriteState(dst, HS_STATE_OCCUPIED);
                memcpy(hsSlotElem(dst, elemOffset),
                       hsSlotElem(oldSlot, elemOffset), stride);
                break;
            }

            idx = (idx + 1u) & mask;
        }
    }

    if(oldBuf)
        MemoryArena_free(arena, oldBuf);

    HsColl_setData(c, newBuf);
    HsColl_setCapacity(c, (u32) newCap);
    Hs_setTombstones(hs, 0);

    return true;
}

static bytes hashSetCount(const Set *self)
{
    if(!self)
        return 0;

    return (bytes) HsColl_activeCount((const Collection*) self);
}

static bytes hashSetCapacity(const Set *self)
{
    if(!self)
        return 0;

    return (bytes) HsColl_capacity((const Collection*) self);
}

static const SetOps *hashSetOps()
{
    if(atomic_load_explicit(&s_hashSetOpsReady, memory_order_acquire))
        return &s_hashSetOps;

    SpinLock_lock(&s_hashSetOpsLock);

    if(!atomic_load_explicit(&s_hashSetOpsReady, memory_order_relaxed)) {
        SetOps_setKind(&s_hashSetOps, SET_KIND_HASH);
        SetOps_setKindName(&s_hashSetOps, "HashSet");
        SetOps_setAddBytes(&s_hashSetOps, HashSet_addBytes);
        SetOps_setContainsBytes(&s_hashSetOps, HashSet_containsBytes);
        SetOps_setGetBytes(&s_hashSetOps, HashSet_getBytes);
        SetOps_setRemoveBytes(&s_hashSetOps, HashSet_removeBytes);
        SetOps_setClear(&s_hashSetOps, HashSet_clear);
        SetOps_setReserve(&s_hashSetOps, HashSet_reserve);
        SetOps_setToList(&s_hashSetOps, HashSet_toList);
        SetOps_setForEach(&s_hashSetOps, HashSet_forEach);
        SetOps_setCount(&s_hashSetOps, hashSetCount);
        SetOps_setCapacity(&s_hashSetOps, hashSetCapacity);
        SetOps_setDestroy(&s_hashSetOps, HashSet_free);
        atomic_store_explicit(&s_hashSetOpsReady, true, memory_order_release);
    }

    SpinLock_unlock(&s_hashSetOpsLock);

    return &s_hashSetOps;
}

;;CONSTRUCTOR
Set *HashSet_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("HashSet: invalid layout for '%s'", ElementLayout_typeName(layout));

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    bytes req = initialCapacity < 4u ? 4u : initialCapacity;
    bytes cap = pow2Capacity(req);

    if(cap == 0 || cap > (bytes) UINT32_MAX) {
        THROW("HashSet: initial capacity %zu overflows u32", initialCapacity);

        return nullptr;
    }

    bytes elemAlign = ElementLayout_alignment(layout);
    bytes slotAlign = elemAlign > sizeof(u64) ? elemAlign : sizeof(u64);
    bytes headerBytes = sizeof(u64) + 1u;
    bytes elemOffset = ElementLayout_alignUp(headerBytes, elemAlign);
    bytes rawStride = elemOffset + ElementLayout_stride(layout);
    bytes slotStride = ElementLayout_alignUp(rawStride, slotAlign);

    bytes allocBytes = 0;

    if(ckd_mul(&allocBytes, cap, slotStride)) {
        THROW("HashSet: initial allocation overflow");

        return nullptr;
    }

    HashSet *hs = (HashSet*) Memory_alloc(TYPE_HASH_SET, targetArena, sizeof(HashSet));

    if(!hs)
        return nullptr;

    Set *base = (Set*) hs;

    if(!Set_initBase(base, TYPE_HASH_SET, hashSetOps(), layout, targetArena)) {
        MemoryArena_free(targetArena, hs);

        return nullptr;
    }

    u8 *buf = (u8*) Memory_alloc(TYPE_BYTE_ARRAY, targetArena, allocBytes);

    if(!buf) {
        MemoryArena_free(targetArena, hs);

        return nullptr;
    }

    memset(buf, 0, allocBytes);

    Collection *c = (Collection*) hs;
    HsColl_setData(c, buf);
    HsColl_setCapacity(c, (u32) cap);
    Hs_setElemOffset(hs, elemOffset);
    Hs_setSlotStride(hs, slotStride);
    Hs_setTombstones(hs, 0);

    return base;
}

bool HashSet_isHashSet(const Set *self)
{
    return Set_kind(self) == SET_KIND_HASH;
}

bool HashSet_addBytes(Set *self, const_any elem, bytes elemBytes)
{
    if(!HashSet_isHashSet(self) || !elem || elemBytes != Set_stride(self))
        return false;

    HashSet *hs = (HashSet*) self;
    Collection *c = (Collection*) self;
    ElementLayout layout = Set_layout(self);
    u32 cap = HsColl_capacity(c);
    u32 active = HsColl_activeCount(c);
    u32 tombs = Hs_tombstones(hs);

    if(cap == 0 || ((bytes) active + (bytes) tombs + 1u) * 4u >= (bytes) cap * 3u) {
        bytes nextCap = cap == 0 ? 4u : ((bytes) active * 2u >= (bytes) cap ? (bytes) cap * 2u : (bytes) cap);

        if(!hashSetRehash(hs, nextCap))
            return false;

        cap = HsColl_capacity(c);
    }

    u8 *buf = HsColl_data(c);
    bytes slotStride = Hs_slotStride(hs);
    bytes elemOffset = Hs_elemOffset(hs);
    u64 hash = ElementLayout_hash(layout, elem);
    bytes mask = (bytes) cap - 1u;
    bytes idx = (bytes) hash & mask;
    bytes firstTomb = (bytes) cap;

    for(u32 step = 0; step < cap; step++) {
        u8 *slot = hsSlotAt(buf, slotStride, idx);
        u8 state = hsSlotState(slot);

        if(state == HS_STATE_EMPTY) {
            bytes targetIdx = firstTomb < (bytes) cap ? firstTomb : idx;
            u8 *target = hsSlotAt(buf, slotStride, targetIdx);

            if(hsSlotState(target) == HS_STATE_DELETED && Hs_tombstones(hs) > 0)
                Hs_setTombstones(hs, Hs_tombstones(hs) - 1u);

            hsSlotWriteHash(target, hash);
            hsSlotWriteState(target, HS_STATE_OCCUPIED);
            memcpy(hsSlotElem(target, elemOffset), elem, elemBytes);
            HsColl_setActiveCount(c, active + 1u);

            return true;
        }

        if(state == HS_STATE_DELETED) {
            if(firstTomb == (bytes) cap)
                firstTomb = idx;
        } else if(hsSlotHash(slot) == hash
                  && ElementLayout_bytesEqual(layout, hsSlotElem(slot, elemOffset), elem)) {
            return false;
        }

        idx = (idx + 1u) & mask;
    }

    if(firstTomb < (bytes) cap) {
        u8 *target = hsSlotAt(buf, slotStride, firstTomb);

        if(Hs_tombstones(hs) > 0)
            Hs_setTombstones(hs, Hs_tombstones(hs) - 1u);

        hsSlotWriteHash(target, hash);
        hsSlotWriteState(target, HS_STATE_OCCUPIED);
        memcpy(hsSlotElem(target, elemOffset), elem, elemBytes);
        HsColl_setActiveCount(c, active + 1u);

        return true;
    }

    return false;
}

;;HOTCODE
any HashSet_getBytes(const Set *self, const_any elem, bytes elemBytes)
{
    if(!HashSet_isHashSet(self) || !elem || elemBytes != Set_stride(self))
        return nullptr;

    const HashSet *hs = (const HashSet*) self;
    const Collection *c = (const Collection*) self;
    u32 cap = HsColl_capacity(c);
    u8 *buf = HsColl_data(c);

    if(cap == 0 || !buf || HsColl_activeCount(c) == 0)
        return nullptr;

    ElementLayout layout = Set_layout(self);
    bytes slotStride = Hs_slotStride(hs);
    bytes elemOffset = Hs_elemOffset(hs);
    u64 hash = ElementLayout_hash(layout, elem);
    bytes mask = (bytes) cap - 1u;
    bytes idx = (bytes) hash & mask;

    for(u32 step = 0; step < cap; step++) {
        u8 *slot = hsSlotAt(buf, slotStride, idx);
        u8 state = hsSlotState(slot);

        if(state == HS_STATE_EMPTY)
            return nullptr;

        if(state == HS_STATE_OCCUPIED
           && hsSlotHash(slot) == hash
           && ElementLayout_bytesEqual(layout, hsSlotElem(slot, elemOffset), elem)) {
            return hsSlotElem(slot, elemOffset);
        }

        idx = (idx + 1u) & mask;
    }

    return nullptr;
}

;;HOTCODE
bool HashSet_containsBytes(const Set *self, const_any elem, bytes elemBytes)
{
    return HashSet_getBytes(self, elem, elemBytes) != nullptr;
}

bool HashSet_removeBytes(Set *self, const_any elem, bytes elemBytes)
{
    if(!HashSet_isHashSet(self) || !elem || elemBytes != Set_stride(self))
        return false;

    HashSet *hs = (HashSet*) self;
    Collection *c = (Collection*) self;
    u32 cap = HsColl_capacity(c);
    u8 *buf = HsColl_data(c);

    if(cap == 0 || !buf || HsColl_activeCount(c) == 0)
        return false;

    ElementLayout layout = Set_layout(self);
    bytes slotStride = Hs_slotStride(hs);
    bytes elemOffset = Hs_elemOffset(hs);
    u64 hash = ElementLayout_hash(layout, elem);
    bytes mask = (bytes) cap - 1u;
    bytes idx = (bytes) hash & mask;

    for(u32 step = 0; step < cap; step++) {
        u8 *slot = hsSlotAt(buf, slotStride, idx);
        u8 state = hsSlotState(slot);

        if(state == HS_STATE_EMPTY)
            return false;

        if(state == HS_STATE_OCCUPIED
           && hsSlotHash(slot) == hash
           && ElementLayout_bytesEqual(layout, hsSlotElem(slot, elemOffset), elem)) {
            hsSlotWriteState(slot, HS_STATE_DELETED);
            HsColl_setActiveCount(c, HsColl_activeCount(c) - 1u);
            Hs_setTombstones(hs, Hs_tombstones(hs) + 1u);

            return true;
        }

        idx = (idx + 1u) & mask;
    }

    return false;
}

fn HashSet_clear(Set *self)
{
    if(!HashSet_isHashSet(self))
        return;

    HashSet *hs = (HashSet*) self;
    Collection *c = (Collection*) self;
    u32 cap = HsColl_capacity(c);
    u8 *buf = HsColl_data(c);

    if(buf && cap > 0)
        memset(buf, 0, (bytes) cap * Hs_slotStride(hs));

    HsColl_setActiveCount(c, 0);
    Hs_setTombstones(hs, 0);
}

bool HashSet_reserve(Set *self, bytes minCapacity)
{
    if(!HashSet_isHashSet(self))
        return false;

    HashSet *hs = (HashSet*) self;
    Collection *c = (Collection*) self;
    bytes neededSlots = 0;

    if(ckd_mul(&neededSlots, minCapacity, (bytes) 4u))
        return Collection_reportExhaustion(c, 0);

    neededSlots = (neededSlots + 2u) / 3u;

    if(neededSlots <= (bytes) HsColl_capacity(c))
        return true;

    return hashSetRehash(hs, neededSlots);
}

List *HashSet_toList(const Set *self)
{
    if(!HashSet_isHashSet(self))
        return nullptr;

    const HashSet *hs = (const HashSet*) self;
    const Collection *c = (const Collection*) self;
    u32 active = HsColl_activeCount(c);
    ElementLayout layout = Set_layout(self);
    List *out = ArrayList_newLayout(layout, active < 4u ? 4u : active, Set_arena(self));

    if(!out)
        return nullptr;

    u32 cap = HsColl_capacity(c);
    u8 *buf = HsColl_data(c);
    bytes slotStride = Hs_slotStride(hs);
    bytes elemOffset = Hs_elemOffset(hs);
    bytes stride = Set_stride(self);

    for(u32 i = 0; i < cap; i++) {
        u8 *slot = hsSlotAt(buf, slotStride, i);

        if(hsSlotState(slot) != HS_STATE_OCCUPIED)
            continue;

        if(!List_addBytes(out, hsSlotElem(slot, elemOffset), stride)) {
            List_free(out);

            return nullptr;
        }
    }

    return out;
}

fn HashSet_forEach(Set *self, SetVisitFn visit, any userdata)
{
    if(!HashSet_isHashSet(self) || !visit)
        return;

    HashSet *hs = (HashSet*) self;
    Collection *c = (Collection*) self;
    u32 cap = HsColl_capacity(c);
    u8 *buf = HsColl_data(c);
    bytes slotStride = Hs_slotStride(hs);
    bytes elemOffset = Hs_elemOffset(hs);
    bytes ordinal = 0;

    for(u32 i = 0; i < cap; i++) {
        u8 *slot = hsSlotAt(buf, slotStride, i);

        if(hsSlotState(slot) != HS_STATE_OCCUPIED)
            continue;

        visit(hsSlotElem(slot, elemOffset), ordinal, userdata);
        ordinal++;
    }
}

;;DECONSTRUCT
fn HashSet_free(Set *self)
{
    if(!HashSet_isHashSet(self))
        return;

    MemoryArena *arena = Set_arena(self);
    Collection *c = (Collection*) self;
    u8 *buf = HsColl_data(c);

    if(buf) {
        MemoryArena_free(arena, buf);
        HsColl_setData(c, nullptr);
    }

    MemoryArena_free(arena, self);
}
