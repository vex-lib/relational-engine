#ifndef RE_PRIMITIVE_I32F32_H
#define RE_PRIMITIVE_I32F32_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/i32f32.h — an i32 and an f32, in canonical pair form.
//
// `integer` is the truncation of the value and `fraction` is the signed
// remainder: the fraction has the same sign as the value (or is zero) and lies
// in [-1, 1). `-0.5` is `{ 0, -0.5 }`; `-1.5` is `{ -1, -0.5 }`.
//
// ;;INTENTION("the fraction is f32, about seven significant decimal digits.
// A longer decimal literal is deliberately estimated, not preserved. The pair
// is a fast substrate representation, not a decimal number type.")
// ;;INTENTION("a fraction that casts to exactly 1.0 is clamped to the largest
// f32 below 1.0 instead of borrowing into the integer, because `integer` is
// defined as trunc(value). The stored value is therefore the nearest pair, not
// the original decimal. That limitation is part of the contract.")
// ;;INTENTION("inf and nan have no finite pair representation. Both are
// accepted and stored as { 0, NAN }; the sign of -inf is deliberately lost.")

typedef struct i32f32 {
    i32 integer;
    f32 fraction;
} i32f32;

#define I32F32_SIZE ((size_t) 8)

_Static_assert(sizeof(i32f32) == I32F32_SIZE, "i32f32 must be 8 bytes");
_Static_assert(_Alignof(i32f32) == 4, "i32f32 must align to 4");
_Static_assert(offsetof(i32f32, fraction) == 4, "i32f32.fraction must sit at offset 4");

i32f32 i32f32_0(void);
i32f32 i32f32_1(const char *text);
i32f32 i32f32_2(i32 integer, f32 fraction);

// Java-style construction: i32f32 a = i32f32("1.375");
#define i32f32(...) CONSTRUCTOR_DISPATCH(i32f32, __VA_ARGS__)

// String projections (the toString Law). Cold path only; no allocation.
void i32f32_toString(const i32f32 *value, char *dest, size_t cap, bool *outTruncated);
void i32f32_toStringStruct(const i32f32 *value, char *dest, size_t cap, bool *outTruncated);

#endif
