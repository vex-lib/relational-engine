#ifndef RE_PRIMITIVE_MAT3F32_H
#define RE_PRIMITIVE_MAT3F32_H

#include <stdbool.h>
#include <stddef.h>

#include "re/primitive/vec4f32.h"
#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/mat3f32.h — a 3x3 f32 matrix in three padded columns.
//
// Column-major, matching every GPU and GLSL convention: `column[0]` is the
// first column, and `column[i].x/y/z` are its rows. Each column is a vec4f32
// so the matrix is 48 bytes and 16-byte aligned; the fourth lane of each
// column is padding and is written as 0.
//
// ;;INTENTION("the name says 3x3, the storage is 3 columns x 4 lanes. The
// fourth lane is SIMD padding, never a translation component; a 4x4 matrix is
// a different type.")

typedef struct mat3f32 {
    alignas(16) vec4f32 column[3];
} mat3f32;

#define MAT3F32_SIZE ((size_t) 48)

_Static_assert(sizeof(mat3f32) == MAT3F32_SIZE, "mat3f32 must be 48 bytes");
_Static_assert(_Alignof(mat3f32) == 16, "mat3f32 must align to 16");
_Static_assert(offsetof(mat3f32, column[1]) == 16, "mat3f32 column stride must be 16");

// Zero matrix, then the identity diagonal.
mat3f32 mat3f32_0(void);
// Nine column-major values: c0.x c0.y c0.z c1.x ... c2.z.
mat3f32 mat3f32_1(const f32 *columns);

#define mat3f32(...) CONSTRUCTOR_DISPATCH(mat3f32, __VA_ARGS__)

void mat3f32_toString(const mat3f32 *value, char *dest, size_t cap, bool *outTruncated);
void mat3f32_toStringStruct(const mat3f32 *value, char *dest, size_t cap, bool *outTruncated);

#endif
