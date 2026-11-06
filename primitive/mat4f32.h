#ifndef RE_PRIMITIVE_MAT4F32_H
#define RE_PRIMITIVE_MAT4F32_H

#include <stdbool.h>
#include <stddef.h>

#include "re/primitive/vec4f32.h"
#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/mat4f32.h — a 4x4 f32 matrix in four columns.
//
// Column-major, matching every GPU and GLSL convention: `column[0]` is the
// first column, and `column[i].x/y/z/w` are its rows. Four vec4f32 columns
// are 64 bytes and 16-byte aligned, with no padding.
//
// ;;INTENTION("4x4 is the transform matrix: the fourth column carries
// translation when the matrix is affine. The type stays a plain value; the
// affine convention belongs to the caller.")

typedef struct mat4f32 {
    alignas(16) vec4f32 column[4];
} mat4f32;

#define MAT4F32_SIZE ((size_t) 64)

_Static_assert(sizeof(mat4f32) == MAT4F32_SIZE, "mat4f32 must be 64 bytes");
_Static_assert(_Alignof(mat4f32) == 16, "mat4f32 must align to 16");
_Static_assert(offsetof(mat4f32, column[1]) == 16, "mat4f32 column stride must be 16");
_Static_assert(offsetof(mat4f32, column[3]) == 48, "mat4f32 fourth column must sit at 48");

// Zero matrix, then the identity diagonal.
mat4f32 mat4f32_0(void);
// Sixteen column-major values: c0.x c0.y c0.z c0.w c1.x ... c3.w.
mat4f32 mat4f32_1(const f32 *columns);

#define mat4f32(...) CONSTRUCTOR_DISPATCH(mat4f32, __VA_ARGS__)

void mat4f32_toString(const mat4f32 *value, char *dest, size_t cap, bool *outTruncated);
void mat4f32_toStringStruct(const mat4f32 *value, char *dest, size_t cap, bool *outTruncated);

#endif
