#ifndef STRUCT_ARRAY_H
#define STRUCT_ARRAY_H

#include "vex.h"
#include "struct/collection.h"
#include "exception/try_value.h"

// struct/array.h — the Array class, ported from struct/Array.java.
//
// Fixed-length, zero-initialized, stride-based array. Active count equals
// capacity; unlike List it never grows.

typedef struct Array {
    Collection collection;
} Array;

// Fixed array of length elements of element_class, all zero-initialized.
Array *Array_2(u32 element_class, bytes length);

// Free the array and its data buffer.
fn Array_free(Array *array);

// Get/set the value or pointer at index (bounds-checked).
u64 Array_get(Array *array, bytes index);
fn Array_set(Array *array, bytes index, u64 value);

// Cold, checking accessor: the value or the named reason it is absent — the
// `;;CHECKER` half of the pair; Array_get is the `;;HOTCODE` half.
TryValue Array_getTry(Array *array, bytes index);

// Pointer to the struct element at index (bounds-checked).
u8 *Array_slot(Array *array, bytes index);

bool Array_isEmpty(Array *array);
bytes Array_size(Array *array);
bytes Array_length(Array *array);
bytes Array_capacity(Array *array);
u32 Array_elementClassId(Array *array);
bytes Array_stride(Array *array);
u8 *Array_dataBuffer(Array *array);


#define Array(...) CONSTRUCTOR_DISPATCH(Array, __VA_ARGS__)

#endif