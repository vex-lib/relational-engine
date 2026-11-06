#ifndef RE_PRIMITIVE_I64F32_H
#define RE_PRIMITIVE_I64F32_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/i64f32.h — an i64 and an f32, in canonical pair form.
//
// `integer` is the truncation of the value and `fraction` is the signed
// remainder: the fraction has the same sign as the value (or is zero) and lies
// in [-1, 1). `-0.5` is `{ 0, -0.5 }`; `-1.5` is `{ -1, -0.5 }`.
//
// ;;INTENTION("the integer is exact i64; the fraction is an f32 estimate. The
// pair can hold a 64-bit integer with a coarse fractional tag, which is the
// point of the type.")
// ;;INTENTION("a fraction that casts to exactly 1.0 is clamped to the largest
// f32 below 1.0 instead of borrowing into the integer, because `integer` is
// defined as trunc(value).")
// ;;INTENTION("inf and nan have no finite pair representation. Both are
// accepted and stored as { 0, NAN }; the sign of -inf is deliberately lost.")

typedef struct i64f32 {
    i64 integer;
    f32 fraction;
} i64f32;

#define I64F32_SIZE ((size_t) 16)

_Static_assert(sizeof(i64f32) == I64F32_SIZE, "i64f32 must be 16 bytes");
_Static_assert(_Alignof(i64f32) == 8, "i64f32 must align to 8");
_Static_assert(offsetof(i64f32, fraction) == 8, "i64f32.fraction must sit at offset 8");

i64f32 i64f32_0(void);
i64f32 i64f32_1(const char *text);
i64f32 i64f32_2(i64 integer, f32 fraction);

// Java-style construction: i64f32 a = i64f32("9223372036854775807.5");
#define i64f32(...) CONSTRUCTOR_DISPATCH(i64f32, __VA_ARGS__)

// String projections (the toString Law). Cold path only; no allocation.
void i64f32_toString(const i64f32 *value, char *dest, size_t cap, bool *outTruncated);
void i64f32_toStringStruct(const i64f32 *value, char *dest, size_t cap, bool *outTruncated);

#endif
