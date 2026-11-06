#ifndef RE_PRIMITIVE_I32F64_H
#define RE_PRIMITIVE_I32F64_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/i32f64.h — an i32 and an f64, in canonical pair form.
//
// `integer` is the truncation of the value and `fraction` is the signed
// remainder: the fraction has the same sign as the value (or is zero) and lies
// in [-1, 1). `-0.5` is `{ 0, -0.5 }`; `-1.5` is `{ -1, -0.5 }`.
//
// ;;INTENTION("the fraction is f64, about fifteen significant decimal digits.
// A longer decimal literal is deliberately estimated, not preserved.")
// ;;INTENTION("a fraction that casts to exactly 1.0 is clamped to the largest
// f64 below 1.0 instead of borrowing into the integer, because `integer` is
// defined as trunc(value).")
// ;;INTENTION("inf and nan have no finite pair representation. Both are
// accepted and stored as { 0, NAN }; the sign of -inf is deliberately lost.")

typedef struct i32f64 {
    i32 integer;
    f64 fraction;
} i32f64;

#define I32F64_SIZE ((size_t) 16)

_Static_assert(sizeof(i32f64) == I32F64_SIZE, "i32f64 must be 16 bytes");
_Static_assert(_Alignof(i32f64) == 8, "i32f64 must align to 8");
_Static_assert(offsetof(i32f64, fraction) == 8, "i32f64.fraction must sit at offset 8");

i32f64 i32f64_0(void);
i32f64 i32f64_1(const char *text);
i32f64 i32f64_2(i32 integer, f64 fraction);

// Java-style construction: i32f64 a = i32f64("1.3749872984729874");
#define i32f64(...) CONSTRUCTOR_DISPATCH(i32f64, __VA_ARGS__)

// String projections (the toString Law). Cold path only; no allocation.
void i32f64_toString(const i32f64 *value, char *dest, size_t cap, bool *outTruncated);
void i32f64_toStringStruct(const i32f64 *value, char *dest, size_t cap, bool *outTruncated);

#endif
