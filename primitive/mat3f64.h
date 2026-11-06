#ifndef RE_PRIMITIVE_MAT3F64_H
#define RE_PRIMITIVE_MAT3F64_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/mat3f64.h — a 3x3 f64 matrix in three 4-lane columns.
//
// Column-major. Each column carries four f64 lanes (32 bytes) so the matrix is
// 96 bytes and 32-byte aligned, matching the memory payload floor and one
// 256-bit register. The fourth lane of each column is padding and is written
// as 0.
//
// ;;INTENTION("the name says 3x3, the storage is 3 columns x 4 f64 lanes. The
// fourth lane is SIMD padding, never a translation component.")

typedef struct mat3f64 {
    alignas(32) struct {
        f64 x, y, z, _pad;
    } column[3];
} mat3f64;

#define MAT3F64_SIZE ((size_t) 96)

_Static_assert(sizeof(mat3f64) == MAT3F64_SIZE, "mat3f64 must be 96 bytes");
_Static_assert(_Alignof(mat3f64) == 32, "mat3f64 must align to 32");
_Static_assert(offsetof(mat3f64, column[1]) == 32, "mat3f64 column stride must be 32");

// Zero matrix, then the identity diagonal.
mat3f64 mat3f64_0(void);
// Nine column-major values: c0.x c0.y c0.z c1.x ... c2.z.
mat3f64 mat3f64_1(const f64 *columns);

#define mat3f64(...) CONSTRUCTOR_DISPATCH(mat3f64, __VA_ARGS__)

void mat3f64_toString(const mat3f64 *value, char *dest, size_t cap, bool *outTruncated);
void mat3f64_toStringStruct(const mat3f64 *value, char *dest, size_t cap, bool *outTruncated);

#endif
