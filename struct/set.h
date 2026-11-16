#ifndef STRUCT_SET_H
#define STRUCT_SET_H

#include "vex.h"
#include "struct/collection.h"
#include "struct/list.h"

// struct/set.h — the Set class, ported from struct/Set.java.
//
// Open-addressing hash set of unique elements. Each slot is 24 Bytes:
// element(8) + hash(8) + state(8); state is 0=empty, 1=occupied, 2=deleted.
// element_class lives in the embedded Collection; stride is unused.

typedef struct Set {
    Collection collection;
} Set;

// Empty set for element_class with initial slot capacity (min 4, power of two).
Set *Set_2(u32 element_class, bytes capacity);

fn Set_free(Set *set);

// Add an element; returns 1 if inserted, 0 if already present.
i32 Set_add(Set *set, u64 element);

bool Set_contains(Set *set, u64 element);

// Remove an element; returns 1 if removed, 0 if absent.
i32 Set_remove(Set *set, u64 element);

// Export all elements into a List of element_class.
List *Set_toList(Set *set);

// Export into a List sorted by value (stride 4/8 use Arrays sort).
List *Set_toSortedList(Set *set);

bool Set_isEmpty(Set *set);
bytes Set_size(Set *set);
// Refused-grow count since construction (the Exhaustion Loudness Law).
;;TEST
u64 Set_exhaustionCount(const Set *set);
bytes Set_capacity(Set *set);
u32 Set_elementClassId(Set *set);
u8 *Set_dataBuffer(Set *set);


Set *Set_1(u32 element_class);

#define Set(...) CONSTRUCTOR_DISPATCH(Set, __VA_ARGS__)

#endif