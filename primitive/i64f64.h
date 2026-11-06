#ifndef RE_PRIMITIVE_I64F64_H
#define RE_PRIMITIVE_I64F64_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/i64f64.h — an i64 and an f64, in canonical pair form.
//
// `integer` is the truncation of the value and `fraction` is the signed
// remainder: the fraction has the same sign as the value (or is zero) and lies
// in [-1, 1). `-0.5` is `{ 0, -0.5 }`; `-1.5` is `{ -1, -0.5 }`.
//
// ;;INTENTION("the integer is exact i64; the fraction is an f64 estimate.
// Both halves are exact for values below 2^53, which is the useful range of
// the type.")
// ;;INTENTION("a fraction that casts to exactly 1.0 is clamped to the largest
// f64 below 1.0 instead of borrowing into the integer, because `integer` is
// defined as trunc(value).")
// ;;INTENTION("inf and nan have no finite pair representation. Both are
// accepted and stored as { 0, NAN }; the sign of -inf is deliberately lost.")

typedef struct i64f64 {
    i64 integer;
    f64 fraction;
} i64f64;

#define I64F64_SIZE ((size_t) 16)

_Static_assert(sizeof(i64f64) == I64F64_SIZE, "i64f64 must be 16 bytes");
_Static_assert(_Alignof(i64f64) == 8, "i64f64 must align to 8");
_Static_assert(offsetof(i64f64, fraction) == 8, "i64f64.fraction must sit at offset 8");

i64f64 i64f64_0(void);
i64f64 i64f64_1(const char *text);
i64f64 i64f64_2(i64 integer, f64 fraction);

// Java-style construction: i64f64 a = i64f64("9223372036854775807.25");
#define i64f64(...) CONSTRUCTOR_DISPATCH(i64f64, __VA_ARGS__)

// String projections (the toString Law). Cold path only; no allocation.
void i64f64_toString(const i64f64 *value, char *dest, size_t cap, bool *outTruncated);
void i64f64_toStringStruct(const i64f64 *value, char *dest, size_t cap, bool *outTruncated);

#endif
