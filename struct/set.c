#include "vex.h"
#include "nio/mem.h"

#include "struct/set.h"
#include "type/re_type.h"
#include "struct/exhaustion.h"
#include "hash/hash.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Set
 * ============================================================================
 * Hash set over a flat open-addressing slot table, ported from struct/Set.java.
 * Each 24-byte slot holds key, hash, and state (empty/occupied/deleted);
 * reference-class elements (strings, lists) are hashed and compared through
 * their memory payloads while scalars use Murmur3 mixing. The table rehashes
 * into a f64d arena buffer past the 0.75 load factor, so capacity grows
 * with no artificial ceiling. The Collection base record is embedded at
 * offset zero, shared with Deque.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Set (struct/set.c)
 * ============================================================================
 * the Set class, ported from struct/Set.java.
 *
 * STRUCT FIELDS (Mirroring struct/set.h):
 * ----------------------------------------------------------------------------
 *   Set {
 *     Collection collection; // instance state
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Set_2(element_class, capacity)
 *   - Set_1(element_class)
 *
 * Setters:
 *   - Set_free(set)
 *   - Set_add(set, element)
 *   - Set_contains(set, element)
 *   - Set_remove(set, element)
 *   - Set_toList(set)
 *   - Set_toSortedList(set)
 *   - Set_isEmpty(set)
 *   - Set_size(set)
 *   - Set_capacity(set)
 *   - Set_elementClassId(set)
 *   - Set_dataBuffer(set)
 * ============================================================================
 */


// set.c — Set port (Legacy: struct/Set.java).

static const bytes SLOT_SIZE = 24;
static const bytes DEFAULT_CAPACITY = 16;
static const bytes LOAD_DIVISOR = 4; // 0.75 load factor
static const u64 STATE_EMPTY = 0;
static const u64 STATE_OCCUPIED = 1;
static const u64 STATE_DELETED = 2;

static Collection *asCollection(Set *set) {
    return (Collection*) set;
}

;;SETTER
static inline fn Collection_setTypeId(Collection *c, u64 typeId) { (*c).typeId = typeId; }
;;GETTER
static inline u32 Collection_activeCount(const Collection *c) { return (*c).activeCount; }
;;SETTER
static inline fn Collection_setActiveCount(Collection *c, u32 count) { (*c).activeCount = count; }
;;GETTER
static inline u32 Collection_elementClass(const Collection *c) { return (*c).elementClass; }
;;SETTER
static inline fn Collection_setElementClass(Collection *c, u32 cls) { (*c).elementClass = cls; }
;;SETTER
static inline fn Collection_setStride(Collection *c, u32 stride) { (*c).stride = stride; }
;;GETTER
static inline u32 Collection_capacityField(const Collection *c) { return (*c).capacity; }
;;SETTER
static inline fn Collection_setCapacity(Collection *c, u32 cap) { (*c).capacity = cap; }
;;SETTER
static inline fn Collection_setHead(Collection *c, u32 head) { (*c).head = head; }
;;SETTER
static inline fn Collection_setExhaustionCount(Collection *c, u64 count) { (*c).exhaustionCount = count; }
;;SETTER
static inline fn Collection_setExhaustionReported(Collection *c, bool rep) { (*c).exhaustionReported = rep; }
;;GETTER
static inline u8 *Collection_data(const Collection *c) { return (*c).data; }
;;SETTER
static inline fn Collection_setData(Collection *c, u8 *data) { (*c).data = data; }

static i32 isReferenceClass(u32 classId) {
    return classId == ID_STRING || classId >= ID_LIST;
}

static u64 computeHash(u32 elementClass, u64 element) {
    if (element == 0) 
       return 0;

    if (isReferenceClass(elementClass) && element >= 4096u) {
        u64 inspected = Memory_type((any) (pointer)element);

        if (inspected != 0) {
            bytes len = Memory_length((any) (pointer)element);

            if (len > 0) 
                return Hash_fnv1a64((const u8*) (pointer)element, len);
        }
    }

    return Hash_murmur3Mix64(element);
}

static i32 elementsEqual(u32 elementClass, u64 e1, u64 e2) {
    if (e1 == e2) 
       return 1;

    if (e1 == 0 || e2 == 0) 
       return 0;

    if (isReferenceClass(elementClass) && e1 >= 4096u && e2 >= 4096u) {
        any p1 = (any) (pointer)e1;

        any p2 = (any) (pointer)e2;

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

static u64 slotState(u8 *slot) {
    return *(u64*) (slot + 16);
}

static fn rehash(Collection *c, bytes newCap) {
    bytes oldCap = Collection_capacityField(c);

    u8 *oldData = Collection_data(c);

    bytes Bytes = newCap * SLOT_SIZE;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, ID_SET);

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
            u64 elem = *(u64*) slot;

            u64 hash = *(u64*) (slot + 8);

            bytes idx = (bytes)(hash & mask);

            while (slotState(newData + idx * SLOT_SIZE) == STATE_OCCUPIED) 
                idx = (idx + 1) & mask;

            u8 *target = newData + idx * SLOT_SIZE;
            *(u64*) target = elem;
            *(u64*) (target + 8) = hash;
            *(u64*) (target + 16) = STATE_OCCUPIED;
        }
    }

    Memory_free(oldData);

    Collection_setData(c, newData);

    Collection_setCapacity(c, (u32)newCap);
}

Set *Set_2(u32 elementClass, bytes capacity) {
    bytes cap = capacity == 0 ? DEFAULT_CAPACITY : highestOneBit(capacity);

    if (cap < 4) 
       cap = 4;

    Set *set = (Set*) Memory_alloc(TYPE_SET, Re_arena(), sizeof(Set));

    if (!set) {
        (discard) Struct_reportConstructionExhaustion("set", sizeof(Set));

        return nullptr;
    }

    Collection *c = asCollection(set);

    Collection_setTypeId(c, TYPE_SET);

    Collection_setActiveCount(c, 0);

    Collection_setElementClass(c, elementClass);

    Collection_setStride(c, 0);

    Collection_setCapacity(c, (u32)cap);

    Collection_setHead(c, 0);

    Collection_setExhaustionCount(c, 0);

    Collection_setExhaustionReported(c, false);

    bytes Bytes = cap * SLOT_SIZE;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, ID_SET);

    u8 *data = (u8*) Memory_alloc(bufType, Re_arena(), Bytes);
    Collection_setData(c, data);

    if (!data) {
        (discard) Struct_reportConstructionExhaustion("set", Bytes);

        Memory_free(set);

        return nullptr;
    }

    memset(data, 0, Bytes);

    return set;
}

fn Set_free(Set *set) {
    if (!set) 
       return;

    Collection *c = asCollection(set);
    u8 *data = Collection_data(c);
    if (data) 
        Memory_free(data);

    Memory_free(set);
}

i32 Set_add(Set *set, u64 element) {
    if (!set) 
       return 0;

    Collection *c = asCollection(set);
    u32 cap = Collection_capacityField(c);
    u32 activeCount = Collection_activeCount(c);
    bytes load = cap - cap / LOAD_DIVISOR;

    if (activeCount >= load) {
        rehash(c, cap * 2);
        cap = Collection_capacityField(c);
    }

    u32 elemClass = Collection_elementClass(c);
    u64 hash = computeHash(elemClass, element);

    bytes mask = cap - 1;

    bytes idx = (bytes)(hash & mask);

    bytes firstDeleted = SIZE_MAX;

    while (true) {
        u8 *slot = slotAt(c, idx);

        u64 st = slotState(slot);

        if (st == STATE_EMPTY) {
            bytes target = firstDeleted != SIZE_MAX ? firstDeleted : idx;

            u8 *tslot = slotAt(c, target);
            *(u64*) tslot = element;
            *(u64*) (tslot + 8) = hash;
            *(u64*) (tslot + 16) = STATE_OCCUPIED;

            Collection_setActiveCount(c, activeCount + 1u);

            return 1;
        } else if (st == STATE_DELETED) {
            if (firstDeleted == SIZE_MAX) 
                firstDeleted = idx;
        } else if (st == STATE_OCCUPIED) {
            if (*(u64*) (slot + 8) == hash
                && elementsEqual(elemClass, *(u64*) slot, element))
                return 0;
        }

        idx = (idx + 1) & mask;
    }
}

bool Set_contains(Set *set, u64 element) {
    if (!set) 
       return false;

    Collection *c = asCollection(set);
    u32 cap = Collection_capacityField(c);

    if (cap == 0) 
       return false;

    u32 elemClass = Collection_elementClass(c);
    u64 hash = computeHash(elemClass, element);

    bytes mask = cap - 1;

    bytes idx = (bytes)(hash & mask);

    for (bytes i = 0; i < cap; i++) {
        u8 *slot = slotAt(c, idx);

        u64 st = slotState(slot);

        if (st == STATE_EMPTY) 
           return false;

        if (st == STATE_OCCUPIED
            && *(u64*) (slot + 8) == hash
            && elementsEqual(elemClass, *(u64*) slot, element))
            return true;
        idx = (idx + 1) & mask;
    }

    return false;
}

i32 Set_remove(Set *set, u64 element) {
    if (!set) 
       return 0;

    Collection *c = asCollection(set);
    u32 cap = Collection_capacityField(c);

    if (cap == 0) 
       return 0;

    u32 elemClass = Collection_elementClass(c);
    u64 hash = computeHash(elemClass, element);

    bytes mask = cap - 1;

    bytes idx = (bytes)(hash & mask);

    for (bytes i = 0; i < cap; i++) {
        u8 *slot = slotAt(c, idx);

        u64 st = slotState(slot);

        if (st == STATE_EMPTY) 
           return 0;

        if (st == STATE_OCCUPIED
            && *(u64*) (slot + 8) == hash
            && elementsEqual(elemClass, *(u64*) slot, element)) {
            *(u64*) (slot + 16) = STATE_DELETED;

            Collection_setActiveCount(c, Collection_activeCount(c) - 1u);

            return 1;
        }

        idx = (idx + 1) & mask;
    }

    return 0;
}

List *Set_toList(Set *set) {
    if (!set) 
       return nullptr;

    Collection *c = asCollection(set);

    List *list = List_2(Collection_elementClass(c), Collection_activeCount(c));

    if (!list) 
       return nullptr;

    u32 cap = Collection_capacityField(c);
    for (bytes i = 0; i < cap; i++) {
        u8 *slot = slotAt(c, i);

        if (slotState(slot) == STATE_OCCUPIED) 
            List_add(list, *(u64*) slot);
    }

    return list;
}

static i64 readSignedElement(const u8 *buf, bytes idx, bool is64) {
    return is64 ? ((const i64*) buf)[idx] : (i64) ((const i32*) buf)[idx];
}

static fn writeSignedElement(u8 *buf, bytes idx, i64 val, bool is64) {
    if (is64) {
        ((i64*) buf)[idx] = val;
    } else {
        ((i32*) buf)[idx] = (i32) val;
    }
}

static fn siftDownSigned(u8 *buf, bytes root, bytes count, bool is64) {
    while (root * 2 + 1 < count) {
        bytes child = root * 2 + 1;

        if (child + 1 < count
            && readSignedElement(buf, child, is64) < readSignedElement(buf, child + 1, is64))
            child++;

        i64 rootVal = readSignedElement(buf, root, is64);

        i64 childVal = readSignedElement(buf, child, is64);

        if (rootVal >= childVal) 
            return;

        writeSignedElement(buf, root, childVal, is64);

        writeSignedElement(buf, child, rootVal, is64);

        root = child;
    }
}

static fn sortSigned(u8 *buf, bytes n, bool is64) {
    for (bytes start = n / 2; start > 0; start--) 
        siftDownSigned(buf, start - 1, n, is64);

    for (bytes end = n; end > 1; end--) {
        i64 top = readSignedElement(buf, 0, is64);

        i64 tail = readSignedElement(buf, end - 1, is64);

        writeSignedElement(buf, 0, tail, is64);

        writeSignedElement(buf, end - 1, top, is64);

        siftDownSigned(buf, 0, end - 1, is64);
    }
}

List *Set_toSortedList(Set *set) {
    List *list = Set_toList(set);

    if (!list) 
        return nullptr;

    if (List_size(list) > 1) {
        bytes stride = List_stride(list);

        u8 *buf = List_dataBuffer(list);

        bytes n = List_size(list);

        if (stride == 4 || stride == 8) 
            sortSigned(buf, n, stride == 8);
    }

    return list;
}

bool Set_isEmpty(Set *set) {
    return Collection_isEmpty(asCollection(set));
}

bytes Set_size(Set *set) {
    return Collection_size(asCollection(set));
}

;;TEST
u64 Set_exhaustionCount(const Set *set) {
    return Collection_exhaustionCount((const Collection*) set);
}

bytes Set_capacity(Set *set) {
    return Collection_capacity(asCollection(set));
}

u32 Set_elementClassId(Set *set) {
    return Collection_elementClassId(asCollection(set));
}

u8 *Set_dataBuffer(Set *set) {
    return Collection_dataBuffer(asCollection(set));
}

Set *Set_1(u32 element_class) {
    return Set_2(element_class, 16);
}
