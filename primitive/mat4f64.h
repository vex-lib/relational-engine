#ifndef RE_PRIMITIVE_MAT4F64_H
#define RE_PRIMITIVE_MAT4F64_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/mat4f64.h — a 4x4 f64 matrix in four 4-lane columns.
//
// Column-major. Four columns of four f64 lanes are 128 bytes and 32-byte
// aligned, with no padding. This is the double-precision transform matrix.
//
// ;;INTENTION("4x4 f64 is 128 bytes: a normal engine block from
// Memory_alloc, whose payload floor is 32 bytes, so it needs no over-aligned
// owner.")

typedef struct mat4f64 {
    alignas(32) struct {
        f64 x, y, z, w;
    } column[4];
} mat4f64;

#define MAT4F64_SIZE ((size_t) 128)

_Static_assert(sizeof(mat4f64) == MAT4F64_SIZE, "mat4f64 must be 128 bytes");
_Static_assert(_Alignof(mat4f64) == 32, "mat4f64 must align to 32");
_Static_assert(offsetof(mat4f64, column[1]) == 32, "mat4f64 column stride must be 32");
_Static_assert(offsetof(mat4f64, column[3]) == 96, "mat4f64 fourth column must sit at 96");

// Zero matrix, then the identity diagonal.
mat4f64 mat4f64_0(void);
// Sixteen column-major values: c0.x c0.y c0.z c0.w c1.x ... c3.w.
mat4f64 mat4f64_1(const f64 *columns);

#define mat4f64(...) CONSTRUCTOR_DISPATCH(mat4f64, __VA_ARGS__)

void mat4f64_toString(const mat4f64 *value, char *dest, size_t cap, bool *outTruncated);
void mat4f64_toStringStruct(const mat4f64 *value, char *dest, size_t cap, bool *outTruncated);

#endif
