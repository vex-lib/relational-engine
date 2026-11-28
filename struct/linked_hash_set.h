#ifndef STRUCT_LINKED_HASH_SET_H
#define STRUCT_LINKED_HASH_SET_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/set.h"

/**
 * struct/linked_hash_set.h, insertion-order-preserving LinkedHashSet
 * implementing the polymorphic Set interface.
 *
 * Combines a flat open-addressing pointer index with 32-byte doubly-linked
 * arena nodes so add/contains/remove/first/last are O(1), pointers stay stable
 * across rehashes, and Set_toList / Set_forEach preserve exact insertion order:
 *
 * Set *s = LinkedHashSet(string);
 * Set *a = LinkedHashSet(u32, 64, myArena);
 */

#define LINKED_HASH_SET_DEFAULT_CAPACITY 16u

/**
 * 32-byte doubly-linked node header preceding inline element payload bytes.
 *
 * struct LinkedHashSetNode *prev - previous element in insertion order
 * struct LinkedHashSetNode *next - next element in insertion order
 * u64 hash                       - cached element hash for O(1) rehash
 * u64 pad                        - padding to 32-byte payload alignment floor
 */
;;CLASSNAME("LinkedHashSetNode")
typedef struct LinkedHashSetNode {
    struct LinkedHashSetNode *prev;
    struct LinkedHashSetNode *next;
    u64 hash;
    u64 pad;
} LinkedHashSetNode;

_Static_assert(sizeof(LinkedHashSetNode) == 32u, "LinkedHashSetNode header must be 32 bytes");

/**
 * Insertion-ordered hash set embedding Set at offset zero.
 *
 * Set base                  - polymorphic Set header
 * LinkedHashSetNode **table - open-addressing pointer index (power-of-two)
 * LinkedHashSetNode *head   - oldest element node in insertion order
 * LinkedHashSetNode *tail   - newest element node in insertion order
 * bytes nodeBytes           - total byte allocation per node
 * u32 tombstones            - tombstone count in pointer index
 */
;;CLASSNAME("LinkedHashSet")
typedef struct LinkedHashSet {
    Set base;
    LinkedHashSetNode **table;
    LinkedHashSetNode *head;
    LinkedHashSetNode *tail;
    bytes nodeBytes;
    u32 tombstones;
} LinkedHashSet;

Set *LinkedHashSet_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena);

#define LinkedHashSet_ctor_1(T)             LinkedHashSet_newLayout(ElementLayout_of(T), LINKED_HASH_SET_DEFAULT_CAPACITY, Re_arena())
#define LinkedHashSet_ctor_2(T, cap)        LinkedHashSet_newLayout(ElementLayout_of(T), (cap), Re_arena())
#define LinkedHashSet_ctor_3(T, cap, arena) LinkedHashSet_newLayout(ElementLayout_of(T), (cap), (arena))
#define LinkedHashSet(...)                  CONSTRUCTOR_DISPATCH(LinkedHashSet_ctor, __VA_ARGS__)

bool LinkedHashSet_isLinkedHashSet(const Set *self);
any LinkedHashSet_first(const Set *self);
any LinkedHashSet_last(const Set *self);

bool LinkedHashSet_addBytes(Set *self, const_any elem, bytes elemBytes);
bool LinkedHashSet_containsBytes(const Set *self, const_any elem, bytes elemBytes);
any LinkedHashSet_getBytes(const Set *self, const_any elem, bytes elemBytes);
bool LinkedHashSet_removeBytes(Set *self, const_any elem, bytes elemBytes);
fn LinkedHashSet_clear(Set *self);
bool LinkedHashSet_reserve(Set *self, bytes minCapacity);
List *LinkedHashSet_toList(const Set *self);
fn LinkedHashSet_forEach(Set *self, SetVisitFn visit, any userdata);
fn LinkedHashSet_free(Set *self);

#endif
