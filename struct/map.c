#include "vex.h"
#include "nio/mem.h"

#include "struct/map.h"
#include "type/re_type.h"
#include "struct/exhaustion.h"
#include "hash/hash.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Map
 * ============================================================================
 * The Map class, ported from struct/Map.java: an open-addressing hash map
 * over 32-byte slots (key, value, hash, state) under a Collection header.
 * Capacity rounds up to a power of two and rehashes at a 0.75 load factor,
 * doubling the slot array; deleted slots are tombstones reused by later
 * inserts. Reference-class keys (strings and lists) hash and compare by
 * content through the memory type/length introspection; scalar keys use a
 * murmur3 mix. Map_keys materializes the live keys as an Array.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Map (struct/map.c)
 * ============================================================================
 * the Map class, ported from struct/Map.java.
 *
 * STRUCT FIELDS (Mirroring struct/map.h):
 * ----------------------------------------------------------------------------
 *   Map {
 *     Collection collection; // instance state
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Map_3(key_class, val_class, capacity)
 *   - Map_2(key_class, val_class)
 *
 * Core Functions:
 *   - Map_free(map)
 *   - Map_put(map, key, value)
 *   - Map_containsKey(map, key)
 *   - Map_remove(map, key)
 *   - Map_keys(map)
 *   - Map_size(map)
 *   - Map_capacity(map)
 *   - Map_keyClassId(map)
 *   - Map_valClassId(map)
 *   - Map_dataBuffer(map)
 *
 * Getters:
 *   - Map_get(map, key)
 *   - Map_isEmpty(map)
 * ============================================================================
 */


/** map.c, Map port (Legacy: struct/Map.java). */

static const bytes SLOT_SIZE = 32;
static const bytes DEFAULT_CAPACITY = 16;
static const bytes LOAD_DIVISOR = 4; /** 0.75 load factor = capacity - capacity/4 */
static const u64 STATE_EMPTY = 0;
static const u64 STATE_OCCUPIED = 1;
static const u64 STATE_DELETED = 2;

static Collection *asCollection(Map *map) {
    return (Collection*) map;
}

;;SETTER
static inline fn Collection_setTypeId(Collection *c, u64 typeId)
{
    (*c).typeId = typeId;
}
;;GETTER
static inline u32 Collection_activeCount(const Collection *c)
{
    return (*c).activeCount;
}
;;SETTER
static inline fn Collection_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}
;;GETTER
static inline u32 Collection_elementClass(const Collection *c)
{
    return (*c).elementClass;
}
;;SETTER
static inline fn Collection_setElementClass(Collection *c, u32 cls)
{
    (*c).elementClass = cls;
}
;;SETTER
static inline fn Collection_setStride(Collection *c, u32 stride)
{
    (*c).stride = stride;
}
;;GETTER
static inline u32 Collection_capacityField(const Collection *c)
{
    return (*c).capacity;
}
;;SETTER
static inline fn Collection_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}
;;SETTER
static inline fn Collection_setHead(Collection *c, u32 head)
{
    (*c).head = head;
}
;;SETTER
static inline fn Collection_setExhaustionCount(Collection *c, u64 count)
{
    (*c).exhaustionCount = count;
}
;;SETTER
static inline fn Collection_setExhaustionReported(Collection *c, bool rep)
{
    (*c).exhaustionReported = rep;
}
;;GETTER
static inline u8 *Collection_data(const Collection *c)
{
    return (*c).data;
}
;;SETTER
static inline fn Collection_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

static i32 isReferenceClass(u32 classId) {
    return classId == ID_STRING || classId >= ID_LIST;
}

static u64 computeHash(u32 keyClass, u64 key) {
    if (key == 0) 
       return 0;

    if (isReferenceClass(keyClass) && key >= 4096u) {
        u64 inspected = Memory_type((any) (pointer)key);

        if (inspected != 0) {
            bytes len = Memory_length((any) (pointer)key);

            if (len > 0) 
                return Hash_fnv1a64((const u8*) (pointer)key, len);
        }
    }

    return Hash_murmur3Mix64(key);
}

static i32 keysEqual(u32 keyClass, u64 k1, u64 k2) {
    if (k1 == k2) 
       return 1;

    if (k1 == 0 || k2 == 0) 
       return 0;

    if (isReferenceClass(keyClass) && k1 >= 4096u && k2 >= 4096u) {
        any p1 = (any) (pointer)k1;

        any p2 = (any) (pointer)k2;

        u64 t1 = Memory_type(p1);

        u64 t2 = Memory_type(p2);

        if (t1 != 0 && t1 == t2) {
            bytes len1 = Memory_length(p1);

            bytes len2 = Memory_length(p2);

            if (len1 != len2) 
               return 0;

            return memcmp(p1, p2, len1) == 0;
        }
    }

    return 0;
}

static bytes highestOneBit(bytes n) {
    if (n <= 1) 
       return 1;

    bytes v = n - 1;

    v |= v >> 1;

    v |= v >> 2;

    v |= v >> 4;

    v |= v >> 8;

    v |= v >> 16;

    v |= v >> 32;

    return v + 1;
}

static u8 *slotAt(Collection *c, bytes index) {
    return Collection_data(c) + index * SLOT_SIZE;
}

static u64 slotState(const u8 *slot) {
    return *(u64*) (slot + 24);
}

static fn rehash(Collection *c, bytes newCap) {
    bytes oldCap = Collection_capacityField(c);

    u8 *oldData = Collection_data(c);

    bytes Bytes = newCap * SLOT_SIZE;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, ID_MAP);

    u8 *newData = (u8*) Memory_alloc(bufType, Re_arena(), Bytes);

    if (!newData) {
        Collection_reportExhaustion(c, Bytes);

        return;
    }

    memset(newData, 0, Bytes);

    bytes mask = newCap - 1;

    for (bytes i = 0; i < oldCap; i++) {
        u8 *slot = slotAt(c, i);

        if (slotState(slot) == STATE_OCCUPIED) {
            u64 key = *(u64*) slot;

            u64 val = *(u64*) (slot + 8);

            u64 hash = *(u64*) (slot + 16);

            bytes idx = (bytes)(hash & mask);

            while (slotState(newData + idx * SLOT_SIZE) == STATE_OCCUPIED) 
                idx = (idx + 1) & mask;

            u8 *target = newData + idx * SLOT_SIZE;
            *(u64*) target = key;
            *(u64*) (target + 8) = val;
            *(u64*) (target + 16) = hash;
            *(u64*) (target + 24) = STATE_OCCUPIED;
        }
    }

    Memory_free(oldData);

    Collection_setData(c, newData);

    Collection_setCapacity(c, (u32)newCap);
}

Map *Map_3(u32 keyClass, u32 valClass, bytes capacity) {
    bytes cap = capacity == 0 ? DEFAULT_CAPACITY : highestOneBit(capacity);

    if (cap < 4) 
       cap = 4;

    Map *map = (Map*) Memory_alloc(TYPE_MAP, Re_arena(), sizeof(Map));

    if (!map) {
        (discard) Struct_reportConstructionExhaustion("map", sizeof(Map));

        return nullptr;
    }

    Collection *c = asCollection(map);

    Collection_setTypeId(c, TYPE_MAP);

    Collection_setActiveCount(c, 0);

    Collection_setElementClass(c, keyClass);

    Collection_setStride(c, valClass);

    Collection_setCapacity(c, (u32)cap);

    Collection_setHead(c, 0);

    Collection_setExhaustionCount(c, 0);

    Collection_setExhaustionReported(c, false);

    bytes Bytes = cap * SLOT_SIZE;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, ID_MAP);

    u8 *data = (u8*) Memory_alloc(bufType, Re_arena(), Bytes);
    Collection_setData(c, data);

    if (!data) {
        (discard) Struct_reportConstructionExhaustion("map", Bytes);

        Memory_free(map);

        return nullptr;
    }

    memset(data, 0, Bytes);

    return map;
}

fn Map_free(Map *map) {
    if (!map) 
       return;

    Collection *c = asCollection(map);
    u8 *data = Collection_data(c);
    if (data) 
        Memory_free(data);

    Memory_free(map);
}

fn Map_put(Map *map, u64 key, u64 value) {
    if (!map) 
       return;

    Collection *c = asCollection(map);
    u32 cap = Collection_capacityField(c);
    u32 activeCount = Collection_activeCount(c);
    bytes load = cap - cap / LOAD_DIVISOR;

    if (activeCount >= load) {
        rehash(c, cap * 2);
        cap = Collection_capacityField(c);
    }

    u32 elemClass = Collection_elementClass(c);
    u64 hash = computeHash(elemClass, key);

    bytes mask = cap - 1;

    bytes idx = (bytes)(hash & mask);

    bytes firstDeleted = SIZE_MAX;

    while (true) {
        u8 *slot = slotAt(c, idx);

        u64 st = slotState(slot);

        if (st == STATE_EMPTY) {
            bytes target = firstDeleted != SIZE_MAX ? firstDeleted : idx;

            u8 *tslot = slotAt(c, target);
            *(u64*) tslot = key;
            *(u64*) (tslot + 8) = value;
            *(u64*) (tslot + 16) = hash;
            *(u64*) (tslot + 24) = STATE_OCCUPIED;

            Collection_setActiveCount(c, activeCount + 1u);

            return;
        } else if (st == STATE_DELETED) {
            if (firstDeleted == SIZE_MAX) 
                firstDeleted = idx;
        } else if (st == STATE_OCCUPIED) {
            if (*(u64*) (slot + 16) == hash
                && keysEqual(elemClass, *(u64*) slot, key)) {
                *(u64*) (slot + 8) = value;

                return;
            }
        }

        idx = (idx + 1) & mask;
    }
}

u64 Map_get(Map *map, u64 key) {
    if (!map) 
       return 0;

    Collection *c = asCollection(map);
    u32 cap = Collection_capacityField(c);

    if (cap == 0) 
       return 0;

    u32 elemClass = Collection_elementClass(c);
    u64 hash = computeHash(elemClass, key);

    bytes mask = cap - 1;

    bytes idx = (bytes)(hash & mask);

    for (bytes i = 0; i < cap; i++) {
        u8 *slot = slotAt(c, idx);

        u64 st = slotState(slot);

        if (st == STATE_EMPTY) 
           return 0;

        if (st == STATE_OCCUPIED
            && *(u64*) (slot + 16) == hash
            && keysEqual(elemClass, *(u64*) slot, key))
            return *(u64*) (slot + 8);
        idx = (idx + 1) & mask;
    }

    return 0;
}

bool Map_containsKey(Map *map, u64 key) {
    if (!map) 
       return false;

    Collection *c = asCollection(map);
    u32 cap = Collection_capacityField(c);

    if (cap == 0) 
       return false;

    u32 elemClass = Collection_elementClass(c);
    u64 hash = computeHash(elemClass, key);

    bytes mask = cap - 1;

    bytes idx = (bytes)(hash & mask);

    for (bytes i = 0; i < cap; i++) {
        u8 *slot = slotAt(c, idx);

        u64 st = slotState(slot);

        if (st == STATE_EMPTY) 
           return false;

        if (st == STATE_OCCUPIED
            && *(u64*) (slot + 16) == hash
            && keysEqual(elemClass, *(u64*) slot, key))
            return true;
        idx = (idx + 1) & mask;
    }

    return false;
}

u64 Map_remove(Map *map, u64 key) {
    if (!map) 
       return 0;

    Collection *c = asCollection(map);
    u32 cap = Collection_capacityField(c);

    if (cap == 0) 
       return 0;

    u32 elemClass = Collection_elementClass(c);
    u64 hash = computeHash(elemClass, key);

    bytes mask = cap - 1;

    bytes idx = (bytes)(hash & mask);

    for (bytes i = 0; i < cap; i++) {
        u8 *slot = slotAt(c, idx);

        u64 st = slotState(slot);

        if (st == STATE_EMPTY) 
           return 0;

        if (st == STATE_OCCUPIED
            && *(u64*) (slot + 16) == hash
            && keysEqual(elemClass, *(u64*) slot, key)) {
            u64 oldVal = *(u64*) (slot + 8);
            *(u64*) (slot + 24) = STATE_DELETED;

            Collection_setActiveCount(c, Collection_activeCount(c) - 1u);

            return oldVal;
        }

        idx = (idx + 1) & mask;
    }

    return 0;
}

Array *Map_keys(Map *map) {
    if (!map) 
       return nullptr;

    Collection *c = asCollection(map);

    Array *keys = Array(Collection_elementClass(c), Collection_activeCount(c));

    if (!keys) 
       return nullptr;

    bytes out = 0;
    u32 cap = Collection_capacityField(c);

    for (bytes i = 0; i < cap; i++) {
        u8 *slot = slotAt(c, i);

        if (slotState(slot) == STATE_OCCUPIED) 
            Array_set(keys, out++, *(u64*) slot);
    }

    return keys;
}

bool Map_isEmpty(Map *map) {
    return Collection_isEmpty(asCollection(map));
}

bytes Map_size(Map *map) {
    return Collection_size(asCollection(map));
}

;;TEST
u64 Map_exhaustionCount(const Map *map) {
    return Collection_exhaustionCount((const Collection*) map);
}

bytes Map_capacity(Map *map) {
    return Collection_capacity(asCollection(map));
}

u32 Map_keyClassId(Map *map) {
    return Collection_keyClassId(asCollection(map));
}

u32 Map_valClassId(Map *map) {
    return Collection_valClassId(asCollection(map));
}

u8 *Map_dataBuffer(Map *map) {
    return Collection_dataBuffer(asCollection(map));
}

Map *Map_2(u32 key_class, u32 val_class) {
    return Map_3(key_class, val_class, 16);
}
