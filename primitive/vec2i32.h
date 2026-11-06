#ifndef RE_PRIMITIVE_VEC2I32_H
#define RE_PRIMITIVE_VEC2I32_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/vec2i32.h — two i32 lanes, 8-byte aligned.
//
// The integer twin of vec2f32: same geometry, same SIMD view, signed lanes.
//
// ;;INTENTION("width-spelled name: a vec of 2 i32 lanes.")

typedef struct vec2i32 {
    alignas(8) union {
        struct { i32 x, y; };
        i32 data[2];
    };
} vec2i32;

#define VEC2I32_SIZE ((size_t) 8)

_Static_assert(sizeof(vec2i32) == VEC2I32_SIZE, "vec2i32 must be 8 bytes");
_Static_assert(_Alignof(vec2i32) == 8, "vec2i32 must align to 8");
_Static_assert(offsetof(vec2i32, y) == 4, "vec2i32.y must sit at offset 4");

vec2i32 vec2i32_0(void);
vec2i32 vec2i32_2(i32 x, i32 y);

#define vec2i32(...) CONSTRUCTOR_DISPATCH(vec2i32, __VA_ARGS__)

void vec2i32_toString(const vec2i32 *value, char *dest, size_t cap, bool *outTruncated);
void vec2i32_toStringStruct(const vec2i32 *value, char *dest, size_t cap, bool *outTruncated);

#endif
