#ifndef STRUCT_HASH_SET_H
#define STRUCT_HASH_SET_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/set.h"

/**
 * struct/hash_set.h, flat open-addressing array-backed HashSet (default Set).
 *
 * Stores hash, state, and inline element inside one contiguous 32-byte-aligned
 * arena slot array. Construct with any C type directly:
 *
 * Set *s = HashSet(string);
 * Set *n = HashSet(u32, 64);
 * Set *a = HashSet(Class, 128, myArena);
 */

#define HASH_SET_DEFAULT_CAPACITY 16u

/**
 * Flat array-backed open-addressing HashSet embedding Set at offset zero.
 *
 * Set base         - polymorphic Set header and Collection table buffer
 * bytes elemOffset - aligned byte offset of inline element inside one slot
 * bytes slotStride - aligned total byte width of one slot
 * u32 tombstones   - number of DELETED slots currently occupying probe chains
 */
;;CLASSNAME("HashSet")
typedef struct HashSet {
    Set base;
    bytes elemOffset;
    bytes slotStride;
    u32 tombstones;
} HashSet;

Set *HashSet_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena);

#define HashSet_ctor_1(T)             HashSet_newLayout(ElementLayout_of(T), HASH_SET_DEFAULT_CAPACITY, Re_arena())
#define HashSet_ctor_2(T, cap)        HashSet_newLayout(ElementLayout_of(T), (cap), Re_arena())
#define HashSet_ctor_3(T, cap, arena) HashSet_newLayout(ElementLayout_of(T), (cap), (arena))
#define HashSet(...)                  CONSTRUCTOR_DISPATCH(HashSet_ctor, __VA_ARGS__)

#define Set(...)                      HashSet(__VA_ARGS__)

bool HashSet_isHashSet(const Set *self);
bool HashSet_addBytes(Set *self, const_any elem, bytes elemBytes);
bool HashSet_containsBytes(const Set *self, const_any elem, bytes elemBytes);
any HashSet_getBytes(const Set *self, const_any elem, bytes elemBytes);
bool HashSet_removeBytes(Set *self, const_any elem, bytes elemBytes);
fn HashSet_clear(Set *self);
bool HashSet_reserve(Set *self, bytes minCapacity);
List *HashSet_toList(const Set *self);
fn HashSet_forEach(Set *self, SetVisitFn visit, any userdata);
fn HashSet_free(Set *self);

#define HashSet_add(set, val)         Set_add((set), (val))
#define HashSet_contains(set, val)    Set_contains((set), (val))
#define HashSet_get(set, val)         Set_get((set), (val))
#define HashSet_remove(set, val)      Set_remove((set), (val))

#endif
