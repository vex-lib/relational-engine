#ifndef RE_PRIMITIVE_VEC4F32_H
#define RE_PRIMITIVE_VEC4F32_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/vec4f32.h — four f32 lanes, 16-byte aligned.
//
// A value primitive, not a heap object. Four lanes are one 128-bit register
// exactly, with no padding lane.
//
// ;;INTENTION("width-spelled name: a vec of 4 f32 lanes. This is the column
// type every f32 matrix is built from.")

typedef struct vec4f32 {
    alignas(16) union {
        struct { f32 x, y, z, w; };
        f32 data[4];
    };
} vec4f32;

#define VEC4F32_SIZE ((size_t) 16)

_Static_assert(sizeof(vec4f32) == VEC4F32_SIZE, "vec4f32 must be 16 bytes");
_Static_assert(_Alignof(vec4f32) == 16, "vec4f32 must align to 16");
_Static_assert(offsetof(vec4f32, w) == 12, "vec4f32.w must sit at offset 12");

vec4f32 vec4f32_0(void);
vec4f32 vec4f32_4(f32 x, f32 y, f32 z, f32 w);

#define vec4f32(...) CONSTRUCTOR_DISPATCH(vec4f32, __VA_ARGS__)

void vec4f32_toString(const vec4f32 *value, char *dest, size_t cap, bool *outTruncated);
void vec4f32_toStringStruct(const vec4f32 *value, char *dest, size_t cap, bool *outTruncated);

#endif
