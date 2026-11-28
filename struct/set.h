#ifndef STRUCT_SET_H
#define STRUCT_SET_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/collection.h"
#include "struct/layout.h"
#include "struct/list.h"

/**
 * struct/set.h, the polymorphic Set interface.
 *
 * Concrete set backings (HashSet, LinkedHashSet) embed Set at offset zero and
 * register a SetOps vtable. Construct with any C element type directly:
 *
 * Set *s = HashSet(string);
 * Set *o = LinkedHashSet(u32);
 * Set_add(s, "hero");
 * bool has = Set_contains(s, "hero");
 * Set_free(s);
 */

/**
 * Concrete Set backing discriminator stored in SetOps.
 *
 * SET_KIND_UNKNOWN     - uninitialized or invalid backing
 * SET_KIND_HASH        - flat open-addressing array table (HashSet)
 * SET_KIND_LINKED_HASH - insertion-ordered doubly-linked hash set (LinkedHashSet)
 */
;;CLASSNAME("SetKind")
typedef enum SetKind {
    SET_KIND_UNKNOWN = 0,
    SET_KIND_HASH = 1,
    SET_KIND_LINKED_HASH = 2,
} SetKind;

typedef struct Set Set;

typedef bool (*SetAddBytesFn)(Set *self, const_any elem, bytes elemBytes);
typedef bool (*SetContainsBytesFn)(const Set *self, const_any elem, bytes elemBytes);
typedef any (*SetGetBytesFn)(const Set *self, const_any elem, bytes elemBytes);
typedef bool (*SetRemoveBytesFn)(Set *self, const_any elem, bytes elemBytes);
typedef fn (*SetClearFn)(Set *self);
typedef bool (*SetReserveFn)(Set *self, bytes minCapacity);
typedef List *(*SetToListFn)(const Set *self);
typedef fn (*SetVisitFn)(const_any elem, bytes index, any userdata);
typedef fn (*SetForEachFn)(Set *self, SetVisitFn visit, any userdata);
typedef bytes (*SetCountFn)(const Set *self);
typedef bytes (*SetCapacityFn)(const Set *self);
typedef fn (*SetDestroyFn)(Set *self);

/**
 * Polymorphic dispatch table implemented by every concrete Set backing.
 *
 * SetKind kind                     - concrete backing discriminator
 * string kindName                  - human-readable backing name
 * SetAddBytesFn addBytes           - insert unique element (true if added)
 * SetContainsBytesFn containsBytes - membership predicate
 * SetGetBytesFn getBytes           - lookup pointer to stored element or nullptr
 * SetRemoveBytesFn removeBytes     - remove element (true if removed)
 * SetClearFn clear                 - remove all elements
 * SetReserveFn reserve             - ensure slot table holds minCapacity elements
 * SetToListFn toList               - export all elements as an ArrayList
 * SetForEachFn forEach             - visit every element in backing order
 * SetCountFn count                 - active element count
 * SetCapacityFn capacity           - slot table capacity
 * SetDestroyFn destroy             - release backing storage and handle
 */
;;CLASSNAME("SetOps")
typedef struct SetOps {
    SetKind kind;
    string kindName;
    SetAddBytesFn addBytes;
    SetContainsBytesFn containsBytes;
    SetGetBytesFn getBytes;
    SetRemoveBytesFn removeBytes;
    SetClearFn clear;
    SetReserveFn reserve;
    SetToListFn toList;
    SetForEachFn forEach;
    SetCountFn count;
    SetCapacityFn capacity;
    SetDestroyFn destroy;
} SetOps;

/**
 * Polymorphic Set interface header embedded at offset zero of every Set backing.
 *
 * Collection collection - embedded Collection header (offset 0)
 * const SetOps *ops     - borrowed vtable describing the concrete backing
 * MemoryArena *arena    - borrowed MemoryArena owning allocations
 * ElementLayout layout  - element type layout (including string content-hash mode)
 */
;;CLASSNAME("Set")
typedef struct Set {
    Collection collection;
    const SetOps *ops;
    MemoryArena *arena;
    ElementLayout layout;
} Set;

bool Set_initBase(Set *self, u64 typeId, const SetOps *ops,
                  ElementLayout layout, MemoryArena *arena);

bool Set_addBytes(Set *self, const_any elem, bytes elemBytes);
bool Set_containsBytes(const Set *self, const_any elem, bytes elemBytes);
any Set_getBytes(const Set *self, const_any elem, bytes elemBytes);
bool Set_removeBytes(Set *self, const_any elem, bytes elemBytes);
fn Set_clear(Set *self);
bool Set_reserve(Set *self, bytes minCapacity);
List *Set_toList(const Set *self);
fn Set_forEach(Set *self, SetVisitFn visit, any userdata);
fn Set_free(Set *self);

bytes Set_count(const Set *self);
bytes Set_size(const Set *self);
bytes Set_capacity(const Set *self);
bool Set_isEmpty(const Set *self);
bytes Set_stride(const Set *self);
string Set_typeName(const Set *self);
SetKind Set_kind(const Set *self);
string Set_kindName(const Set *self);
MemoryArena *Set_arena(const Set *self);
const SetOps *Set_ops(const Set *self);
ElementLayout Set_layout(const Set *self);

;;TEST
u64 Set_exhaustionCount(const Set *self);

fn Set_toString(const Set *self, utf8 *dest, bytes cap, bool *outTruncated);
fn Set_toStringStruct(const Set *self, utf8 *dest, bytes cap, bool *outTruncated);

fn SetOps_setKind(SetOps *ops, SetKind kind);
fn SetOps_setKindName(SetOps *ops, string kindName);
fn SetOps_setAddBytes(SetOps *ops, SetAddBytesFn fnPtr);
fn SetOps_setContainsBytes(SetOps *ops, SetContainsBytesFn fnPtr);
fn SetOps_setGetBytes(SetOps *ops, SetGetBytesFn fnPtr);
fn SetOps_setRemoveBytes(SetOps *ops, SetRemoveBytesFn fnPtr);
fn SetOps_setClear(SetOps *ops, SetClearFn fnPtr);
fn SetOps_setReserve(SetOps *ops, SetReserveFn fnPtr);
fn SetOps_setToList(SetOps *ops, SetToListFn fnPtr);
fn SetOps_setForEach(SetOps *ops, SetForEachFn fnPtr);
fn SetOps_setCount(SetOps *ops, SetCountFn fnPtr);
fn SetOps_setCapacity(SetOps *ops, SetCapacityFn fnPtr);
fn SetOps_setDestroy(SetOps *ops, SetDestroyFn fnPtr);

#define Set_add(set, val)                                                    \
    ({                                                                       \
        auto _vex_set_add_v = (val);                                         \
        Set_addBytes((set), &_vex_set_add_v, sizeof(_vex_set_add_v));        \
    })

#define Set_contains(set, val)                                               \
    ({                                                                       \
        auto _vex_set_has_v = (val);                                         \
        Set_containsBytes((set), &_vex_set_has_v, sizeof(_vex_set_has_v));   \
    })

#define Set_get(set, val)                                                    \
    ({                                                                       \
        auto _vex_set_get_v = (val);                                         \
        Set_getBytes((set), &_vex_set_get_v, sizeof(_vex_set_get_v));        \
    })

#define Set_remove(set, val)                                                 \
    ({                                                                       \
        auto _vex_set_rem_v = (val);                                         \
        Set_removeBytes((set), &_vex_set_rem_v, sizeof(_vex_set_rem_v));     \
    })

#endif
