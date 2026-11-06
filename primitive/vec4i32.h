#ifndef RE_PRIMITIVE_VEC4I32_H
#define RE_PRIMITIVE_VEC4I32_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/vec4i32.h — four i32 lanes, 16-byte aligned.
//
// The integer twin of vec4f32: four lanes, one 128-bit register, no padding.
//
// ;;INTENTION("width-spelled name: a vec of 4 i32 lanes.")

typedef struct vec4i32 {
    alignas(16) union {
        struct { i32 x, y, z, w; };
        i32 data[4];
    };
} vec4i32;

#define VEC4I32_SIZE ((size_t) 16)

_Static_assert(sizeof(vec4i32) == VEC4I32_SIZE, "vec4i32 must be 16 bytes");
_Static_assert(_Alignof(vec4i32) == 16, "vec4i32 must align to 16");
_Static_assert(offsetof(vec4i32, w) == 12, "vec4i32.w must sit at offset 12");

vec4i32 vec4i32_0(void);
vec4i32 vec4i32_4(i32 x, i32 y, i32 z, i32 w);

#define vec4i32(...) CONSTRUCTOR_DISPATCH(vec4i32, __VA_ARGS__)

void vec4i32_toString(const vec4i32 *value, char *dest, size_t cap, bool *outTruncated);
void vec4i32_toStringStruct(const vec4i32 *value, char *dest, size_t cap, bool *outTruncated);

#endif
