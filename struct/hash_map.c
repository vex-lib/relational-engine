#include "vex.h"
#include <stdckdint.h>

#include "atomic/atomic.h"
#include "nio/mem.h"
#include "struct/array_list.h"
#include "struct/exhaustion.h"
#include "struct/hash_map.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: HashMap
 * ============================================================================
 * Array-backed open-addressing HashMap implementing the polymorphic Map
 * interface (default Map backing). Every slot holds [hash:8][state:1][pad]
 * [key:keyStride][pad][val:valStride] aligned to max(8, keyAlign, valAlign)
 * inside one flat 32-byte-aligned MemoryArena buffer. Rehashes cleanly past
 * 0.75 combined (active + tombstone) load factor so deleted tombstones never
 * wedge probe loops.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: HashMap (struct/hash_map.c)
 * ============================================================================
 * Flat open-addressing array HashMap over arbitrary inline K and V types.
 *
 * --CONSTANTS----------------------------
 * public   const u32 HASH_MAP_DEFAULT_CAPACITY  default initial capacity (16)
 * private  const u8  HM_STATE_EMPTY             empty slot marker (0)
 * private  const u8  HM_STATE_OCCUPIED          live entry marker (1)
 * private  const u8  HM_STATE_DELETED           tombstone marker (2)
 *
 * --FIELDS-------------------------------
 * public   Map   base                      embedded polymorphic Map, 1:1
 * private  bytes keyOffset                 aligned key offset inside slot
 * private  bytes valOffset                 aligned value offset inside slot
 * private  bytes slotStride                aligned byte width of one slot
 * private  u32   tombstones                live tombstone slot count
 *
 * --CONSTRUCTORS-------------------------
 * public   HashMap_newLayout(...)          constructs array-backed HashMap
 *
 * --FUNCTIONS----------------------------
 * public   HashMap_isHashMap(...)          checks MAP_KIND_HASH
 * public   HashMap_putBytes(...)           insert or update entry
 * public   HashMap_emplaceBytes(...)       insert zeroed value if absent
 * public   HashMap_getBytes(...)           linear-probing lookup
 * public   HashMap_containsKeyBytes(...)   presence predicate
 * public   HashMap_removeBytes(...)        tombstone removal
 * public   HashMap_clear(...)              zeros all slots
 * public   HashMap_reserve(...)            grows table to hold minCapacity
 * public   HashMap_keys(...)               exports keys as ArrayList
 * public   HashMap_values(...)             exports values as ArrayList
 * public   HashMap_forEach(...)            visits occupied slots
 * public   HashMap_free(...)               releases buffer and handle
 * private  hashMapOps(...)                 lazy thread-safe vtable singleton
 * private  hashMapRehash(...)              allocates and re-indexes slots
 * private  pow2Capacity(...)               power-of-two capacity rounding
 *
 * --GETTERS------------------------------
 * private  Hm_keyOffset / Hm_valOffset / Hm_slotStride / Hm_tombstones
 * private  HmColl_activeCount / HmColl_capacity / HmColl_data
 *
 * --SETTERS------------------------------
 * private  Hm_setKeyOffset / Hm_setValOffset / Hm_setSlotStride / Hm_setTombstones
 * private  HmColl_setActiveCount / HmColl_setCapacity / HmColl_setData
 * ============================================================================
 */

static const u8 HM_STATE_EMPTY = 0u;
static const u8 HM_STATE_OCCUPIED = 1u;
static const u8 HM_STATE_DELETED = 2u;

static MapOps s_hashMapOps;
static SpinLock s_hashMapOpsLock = SPIN_LOCK_INIT;
static atomic_bool s_hashMapOpsReady = false;

;;GETTER
static inline bytes Hm_keyOffset(const HashMap *hm)
{
    return (*hm).keyOffset;
}

;;SETTER
static inline fn Hm_setKeyOffset(HashMap *hm, bytes off)
{
    (*hm).keyOffset = off;
}

;;GETTER
static inline bytes Hm_valOffset(const HashMap *hm)
{
    return (*hm).valOffset;
}

;;SETTER
static inline fn Hm_setValOffset(HashMap *hm, bytes off)
{
    (*hm).valOffset = off;
}

;;GETTER
static inline bytes Hm_slotStride(const HashMap *hm)
{
    return (*hm).slotStride;
}

;;SETTER
static inline fn Hm_setSlotStride(HashMap *hm, bytes stride)
{
    (*hm).slotStride = stride;
}

;;GETTER
static inline u32 Hm_tombstones(const HashMap *hm)
{
    return (*hm).tombstones;
}

;;SETTER
static inline fn Hm_setTombstones(HashMap *hm, u32 tombs)
{
    (*hm).tombstones = tombs;
}

;;GETTER
static inline u32 HmColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;SETTER
static inline fn HmColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;GETTER
static inline u32 HmColl_capacity(const Collection *c)
{
    return (*c).capacity;
}

;;SETTER
static inline fn HmColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;GETTER
static inline u8 *HmColl_data(const Collection *c)
{
    return (*c).data;
}

;;SETTER
static inline fn HmColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

static bytes pow2Capacity(bytes requested)
{
    bytes cap = 8u;

    while(cap < requested && cap <= ((bytes) UINT32_MAX / 2u))
        cap *= 2u;

    if(cap < requested)
        return 0;

    return cap;
}

static inline u8 *hmSlot(const HashMap *hm, u8 *base, bytes index)
{
    return base + (index * Hm_slotStride(hm));
}

static inline u64 hmSlotHash(const u8 *slot)
{
    u64 h = 0;
    memcpy(&h, slot, sizeof(u64));

    return h;
}

static inline fn hmSetSlotHash(u8 *slot, u64 hash)
{
    memcpy(slot, &hash, sizeof(u64));
}

static inline u8 hmSlotState(const u8 *slot)
{
    return slot[sizeof(u64)];
}

static inline fn hmSetSlotState(u8 *slot, u8 state)
{
    slot[sizeof(u64)] = state;
}

static bool hashMapRehash(HashMap *hm, bytes newCap)
{
    if(newCap == 0 || newCap > (bytes) UINT32_MAX) {
        (discard) Collection_reportExhaustion((Collection*) hm, SIZE_MAX);

        return false;
    }

    bytes slotStride = Hm_slotStride(hm);
    bytes totalBytes = 0;

    if(ckd_mul(&totalBytes, newCap, slotStride)) {
        (discard) Collection_reportExhaustion((Collection*) hm, SIZE_MAX);

        return false;
    }

    MemoryArena *arena = Map_arena((const Map*) hm);
    u8 *nextData = (u8*) MemoryArena_alloc(arena, TYPE_HASH_MAP, totalBytes);

    if(!nextData) {
        (discard) Collection_reportExhaustion((Collection*) hm, totalBytes);

        return false;
    }

    memset(nextData, 0, totalBytes);

    Collection *c = (Collection*) hm;
    bytes oldCap = (bytes) HmColl_capacity(c);
    u8 *oldData = HmColl_data(c);
    bytes mask = newCap - 1u;

    if(oldData) {
        for(bytes i = 0; i < oldCap; i++) {
            u8 *oldSlot = hmSlot(hm, oldData, i);

            if(hmSlotState(oldSlot) == HM_STATE_OCCUPIED) {
                u64 h = hmSlotHash(oldSlot);
                bytes idx = (bytes) (h & mask);

                while(hmSlotState(hmSlot(hm, nextData, idx)) == HM_STATE_OCCUPIED)
                    idx = (idx + 1u) & mask;

                memcpy(hmSlot(hm, nextData, idx), oldSlot, slotStride);
            }
        }

        (discard) MemoryArena_free(arena, oldData);
    }

    HmColl_setData(c, nextData);
    HmColl_setCapacity(c, (u32) newCap);
    Hm_setTombstones(hm, 0);

    return true;
}

static bool hashMapEnsureLoad(HashMap *hm)
{
    Collection *c = (Collection*) hm;
    bytes cap = (bytes) HmColl_capacity(c);
    bytes active = (bytes) HmColl_activeCount(c);
    bytes used = active + (bytes) Hm_tombstones(hm);
    bytes limit = cap - (cap / 4u);

    if(cap > 0 && used < limit)
        return true;

    bytes targetCap = cap == 0 ? (bytes) HASH_MAP_DEFAULT_CAPACITY : cap;

    if(active >= (cap / 2u)) {
        if(targetCap > ((bytes) UINT32_MAX / 2u))
            return false;

        targetCap *= 2u;
    }

    return hashMapRehash(hm, targetCap);
}

static bytes hashMapCount(const Map *self)
{
    if(!self)
        return 0;

    return (bytes) HmColl_activeCount((const Collection*) self);
}

static bytes hashMapCapacity(const Map *self)
{
    if(!self)
        return 0;

    return (bytes) HmColl_capacity((const Collection*) self);
}

static const MapOps *hashMapOps()
{
    if(atomic_load_explicit(&s_hashMapOpsReady, memory_order_acquire))
        return &s_hashMapOps;

    SpinLock_lock(&s_hashMapOpsLock);

    if(!atomic_load_explicit(&s_hashMapOpsReady, memory_order_relaxed)) {
        MapOps_setKind(&s_hashMapOps, MAP_KIND_HASH);
        MapOps_setKindName(&s_hashMapOps, "HashMap");
        MapOps_setPutBytes(&s_hashMapOps, HashMap_putBytes);
        MapOps_setEmplaceBytes(&s_hashMapOps, HashMap_emplaceBytes);
        MapOps_setGetBytes(&s_hashMapOps, HashMap_getBytes);
        MapOps_setContainsBytes(&s_hashMapOps, HashMap_containsKeyBytes);
        MapOps_setRemoveBytes(&s_hashMapOps, HashMap_removeBytes);
        MapOps_setClear(&s_hashMapOps, HashMap_clear);
        MapOps_setReserve(&s_hashMapOps, HashMap_reserve);
        MapOps_setKeys(&s_hashMapOps, HashMap_keys);
        MapOps_setValues(&s_hashMapOps, HashMap_values);
        MapOps_setForEach(&s_hashMapOps, HashMap_forEach);
        MapOps_setCount(&s_hashMapOps, hashMapCount);
        MapOps_setCapacity(&s_hashMapOps, hashMapCapacity);
        MapOps_setDestroy(&s_hashMapOps, HashMap_free);
        atomic_store_explicit(&s_hashMapOpsReady, true, memory_order_release);
    }

    SpinLock_unlock(&s_hashMapOpsLock);

    return &s_hashMapOps;
}

;;CONSTRUCTOR
Map *HashMap_newLayout(ElementLayout keyLayout, ElementLayout valLayout,
                       bytes initialCapacity, MemoryArena *arena)
{
    if(!ElementLayout_isValid(keyLayout) || !ElementLayout_isValid(valLayout)) {
        THROW("HashMap: invalid layout for <%s, %s>",
              ElementLayout_typeName(keyLayout),
              ElementLayout_typeName(valLayout));

        return nullptr;
    }

    bytes req = initialCapacity == 0 ? (bytes) HASH_MAP_DEFAULT_CAPACITY : initialCapacity;
    bytes cap = pow2Capacity(req);

    if(cap == 0) {
        (discard) Struct_reportConstructionExhaustion("HashMap", req);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    HashMap *hm = (HashMap*) MemoryArena_alloc(targetArena, TYPE_HASH_MAP, sizeof(HashMap));

    if(!hm) {
        (discard) Struct_reportConstructionExhaustion("HashMap", sizeof(HashMap));

        return nullptr;
    }

    Map *map = (Map*) hm;

    if(!Map_initBase(map, TYPE_HASH_MAP, hashMapOps(), keyLayout, valLayout, targetArena)) {
        (discard) MemoryArena_free(targetArena, hm);

        return nullptr;
    }

    bytes keyAlign = ElementLayout_alignment(keyLayout);
    bytes valAlign = ElementLayout_alignment(valLayout);
    bytes maxAlign = 8u;

    if(keyAlign > maxAlign)
        maxAlign = keyAlign;

    if(valAlign > maxAlign)
        maxAlign = valAlign;

    bytes keyOffset = ElementLayout_alignUp(16u, keyAlign);
    bytes valOffset = ElementLayout_alignUp(keyOffset + ElementLayout_stride(keyLayout), valAlign);
    bytes slotStride = ElementLayout_alignUp(valOffset + ElementLayout_stride(valLayout), maxAlign);

    Hm_setKeyOffset(hm, keyOffset);
    Hm_setValOffset(hm, valOffset);
    Hm_setSlotStride(hm, slotStride);
    Hm_setTombstones(hm, 0);

    if(!hashMapRehash(hm, cap)) {
        (discard) Struct_reportConstructionExhaustion("HashMap", cap * slotStride);
        (discard) MemoryArena_free(targetArena, hm);

        return nullptr;
    }

    return map;
}

bool HashMap_isHashMap(const Map *self)
{
    if(!self)
        return false;

    return Map_kind(self) == MAP_KIND_HASH;
}

any HashMap_emplaceBytes(Map *self, const_any key, bytes keyBytes, bool *outInserted)
{
    if(outInserted)
        *outInserted = false;

    if(!HashMap_isHashMap(self) || !key)
        return nullptr;

    ElementLayout keyLayout = Map_keyLayout(self);
    ElementLayout valLayout = Map_valLayout(self);

    if(keyBytes != ElementLayout_stride(keyLayout))
        return nullptr;

    HashMap *hm = (HashMap*) self;

    if(!hashMapEnsureLoad(hm))
        return nullptr;

    Collection *c = (Collection*) self;
    bytes cap = (bytes) HmColl_capacity(c);
    u8 *data = HmColl_data(c);
    u64 hash = ElementLayout_hash(keyLayout, key);
    bytes mask = cap - 1u;
    bytes idx = (bytes) (hash & mask);
    bytes firstDeleted = SIZE_MAX;
    bytes keyOff = Hm_keyOffset(hm);
    bytes valOff = Hm_valOffset(hm);

    for(bytes probe = 0; probe < cap; probe++) {
        u8 *slot = hmSlot(hm, data, idx);
        u8 state = hmSlotState(slot);

        if(state == HM_STATE_EMPTY) {
            bytes targetIdx = firstDeleted != SIZE_MAX ? firstDeleted : idx;
            u8 *target = hmSlot(hm, data, targetIdx);

            if(firstDeleted != SIZE_MAX)
                Hm_setTombstones(hm, Hm_tombstones(hm) - 1u);

            hmSetSlotHash(target, hash);
            hmSetSlotState(target, HM_STATE_OCCUPIED);
            memcpy(target + keyOff, key, keyBytes);
            memset(target + valOff, 0, ElementLayout_stride(valLayout));
            HmColl_setActiveCount(c, HmColl_activeCount(c) + 1u);

            if(outInserted)
                *outInserted = true;

            return target + valOff;
        }

        if(state == HM_STATE_DELETED) {
            if(firstDeleted == SIZE_MAX)
                firstDeleted = idx;
        } else if(hmSlotHash(slot) == hash
                  && ElementLayout_bytesEqual(keyLayout, slot + keyOff, key)) {
            return slot + valOff;
        }

        idx = (idx + 1u) & mask;
    }

    return nullptr;
}

bool HashMap_putBytes(Map *self, const_any key, bytes keyBytes, const_any val, bytes valBytes)
{
    if(!HashMap_isHashMap(self) || !key || !val)
        return false;

    if(valBytes != Map_valStride(self))
        return false;

    any valSlot = HashMap_emplaceBytes(self, key, keyBytes, nullptr);

    if(!valSlot)
        return false;

    memcpy(valSlot, val, valBytes);

    return true;
}

;;HOTCODE
any HashMap_getBytes(const Map *self, const_any key, bytes keyBytes)
{
    if(!HashMap_isHashMap(self) || !key)
        return nullptr;

    ElementLayout keyLayout = Map_keyLayout(self);

    if(keyBytes != ElementLayout_stride(keyLayout))
        return nullptr;

    const HashMap *hm = (const HashMap*) self;
    const Collection *c = (const Collection*) self;
    bytes cap = (bytes) HmColl_capacity(c);
    u8 *data = HmColl_data(c);

    if(cap == 0 || !data)
        return nullptr;

    u64 hash = ElementLayout_hash(keyLayout, key);
    bytes mask = cap - 1u;
    bytes idx = (bytes) (hash & mask);
    bytes keyOff = Hm_keyOffset(hm);
    bytes valOff = Hm_valOffset(hm);

    for(bytes probe = 0; probe < cap; probe++) {
        u8 *slot = hmSlot(hm, data, idx);
        u8 state = hmSlotState(slot);

        if(state == HM_STATE_EMPTY)
            return nullptr;

        if(state == HM_STATE_OCCUPIED
           && hmSlotHash(slot) == hash
           && ElementLayout_bytesEqual(keyLayout, slot + keyOff, key))
            return slot + valOff;

        idx = (idx + 1u) & mask;
    }

    return nullptr;
}

bool HashMap_containsKeyBytes(const Map *self, const_any key, bytes keyBytes)
{
    return HashMap_getBytes(self, key, keyBytes) != nullptr;
}

bool HashMap_removeBytes(Map *self, const_any key, bytes keyBytes)
{
    if(!HashMap_isHashMap(self) || !key)
        return false;

    ElementLayout keyLayout = Map_keyLayout(self);

    if(keyBytes != ElementLayout_stride(keyLayout))
        return false;

    HashMap *hm = (HashMap*) self;
    Collection *c = (Collection*) self;
    bytes cap = (bytes) HmColl_capacity(c);
    u8 *data = HmColl_data(c);

    if(cap == 0 || !data)
        return false;

    u64 hash = ElementLayout_hash(keyLayout, key);
    bytes mask = cap - 1u;
    bytes idx = (bytes) (hash & mask);
    bytes keyOff = Hm_keyOffset(hm);

    for(bytes probe = 0; probe < cap; probe++) {
        u8 *slot = hmSlot(hm, data, idx);
        u8 state = hmSlotState(slot);

        if(state == HM_STATE_EMPTY)
            return false;

        if(state == HM_STATE_OCCUPIED
           && hmSlotHash(slot) == hash
           && ElementLayout_bytesEqual(keyLayout, slot + keyOff, key)) {
            hmSetSlotState(slot, HM_STATE_DELETED);
            HmColl_setActiveCount(c, HmColl_activeCount(c) - 1u);
            Hm_setTombstones(hm, Hm_tombstones(hm) + 1u);

            return true;
        }

        idx = (idx + 1u) & mask;
    }

    return false;
}

fn HashMap_clear(Map *self)
{
    if(!HashMap_isHashMap(self))
        return;

    HashMap *hm = (HashMap*) self;
    Collection *c = (Collection*) self;
    u8 *data = HmColl_data(c);
    bytes cap = (bytes) HmColl_capacity(c);

    if(data && cap > 0)
        memset(data, 0, cap * Hm_slotStride(hm));

    HmColl_setActiveCount(c, 0);
    Hm_setTombstones(hm, 0);
}

bool HashMap_reserve(Map *self, bytes minCapacity)
{
    if(!HashMap_isHashMap(self))
        return false;

    HashMap *hm = (HashMap*) self;
    bytes neededSlots = minCapacity + (minCapacity / 3u) + 2u;
    bytes targetCap = pow2Capacity(neededSlots);

    if(targetCap == 0) {
        (discard) Collection_reportExhaustion((Collection*) self, SIZE_MAX);

        return false;
    }

    if(targetCap <= (bytes) HmColl_capacity((const Collection*) self))
        return true;

    return hashMapRehash(hm, targetCap);
}

List *HashMap_keys(const Map *self)
{
    if(!HashMap_isHashMap(self))
        return nullptr;

    const HashMap *hm = (const HashMap*) self;
    const Collection *c = (const Collection*) self;
    bytes active = (bytes) HmColl_activeCount(c);
    ElementLayout keyLayout = Map_keyLayout(self);
    List *out = ArrayList_newLayout(keyLayout, active, Map_arena(self));

    if(!out)
        return nullptr;

    bytes cap = (bytes) HmColl_capacity(c);
    u8 *data = HmColl_data(c);
    bytes keyOff = Hm_keyOffset(hm);
    bytes keyStride = ElementLayout_stride(keyLayout);

    for(bytes i = 0; i < cap; i++) {
        u8 *slot = hmSlot(hm, data, i);

        if(hmSlotState(slot) == HM_STATE_OCCUPIED)
            (discard) ArrayList_addBytes(out, slot + keyOff, keyStride);
    }

    return out;
}

List *HashMap_values(const Map *self)
{
    if(!HashMap_isHashMap(self))
        return nullptr;

    const HashMap *hm = (const HashMap*) self;
    const Collection *c = (const Collection*) self;
    bytes active = (bytes) HmColl_activeCount(c);
    ElementLayout valLayout = Map_valLayout(self);
    List *out = ArrayList_newLayout(valLayout, active, Map_arena(self));

    if(!out)
        return nullptr;

    bytes cap = (bytes) HmColl_capacity(c);
    u8 *data = HmColl_data(c);
    bytes valOff = Hm_valOffset(hm);
    bytes valStride = ElementLayout_stride(valLayout);

    for(bytes i = 0; i < cap; i++) {
        u8 *slot = hmSlot(hm, data, i);

        if(hmSlotState(slot) == HM_STATE_OCCUPIED)
            (discard) ArrayList_addBytes(out, slot + valOff, valStride);
    }

    return out;
}

fn HashMap_forEach(Map *self, MapVisitFn visit, any userdata)
{
    if(!HashMap_isHashMap(self) || !visit)
        return;

    HashMap *hm = (HashMap*) self;
    Collection *c = (Collection*) self;
    bytes cap = (bytes) HmColl_capacity(c);
    u8 *data = HmColl_data(c);
    bytes keyOff = Hm_keyOffset(hm);
    bytes valOff = Hm_valOffset(hm);
    bytes visited = 0;

    for(bytes i = 0; i < cap; i++) {
        u8 *slot = hmSlot(hm, data, i);

        if(hmSlotState(slot) == HM_STATE_OCCUPIED) {
            visit(slot + keyOff, slot + valOff, visited, userdata);
            visited++;
        }
    }
}

;;DECONSTRUCT
fn HashMap_free(Map *self)
{
    if(!self)
        return;

    if(!HashMap_isHashMap(self)) {
        Map_free(self);

        return;
    }

    Collection *c = (Collection*) self;
    MemoryArena *arena = Map_arena(self);
    u8 *data = HmColl_data(c);

    if(data) {
        HmColl_setData(c, nullptr);
        (discard) MemoryArena_free(arena, data);
    }

    (discard) MemoryArena_free(arena, self);
}
