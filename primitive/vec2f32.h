#ifndef RE_PRIMITIVE_VEC2F32_H
#define RE_PRIMITIVE_VEC2F32_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/vec2f32.h — two f32 lanes, 8-byte aligned.
//
// A value primitive, not a heap object. `data[0..1]` is the SIMD view; `x`/`y`
// name the same bytes. Two lanes fit one 64-bit register exactly.
//
// ;;INTENTION("width-spelled name: a vec of 2 f32 lanes. The name is the
// product of its parts, exactly like i32f32.")
// ;;INTENTION("no padding lane: two f32 lanes are 8 bytes, so vec2* is the one
// member of the family that is fully packed.").

typedef struct vec2f32 {
    alignas(8) union {
        struct { f32 x, y; };
        f32 data[2];
    };
} vec2f32;

#define VEC2F32_SIZE ((size_t) 8)

_Static_assert(sizeof(vec2f32) == VEC2F32_SIZE, "vec2f32 must be 8 bytes");
_Static_assert(_Alignof(vec2f32) == 8, "vec2f32 must align to 8");
_Static_assert(offsetof(vec2f32, y) == 4, "vec2f32.y must sit at offset 4");

vec2f32 vec2f32_0(void);
vec2f32 vec2f32_2(f32 x, f32 y);

#define vec2f32(...) CONSTRUCTOR_DISPATCH(vec2f32, __VA_ARGS__)

void vec2f32_toString(const vec2f32 *value, char *dest, size_t cap, bool *outTruncated);
void vec2f32_toStringStruct(const vec2f32 *value, char *dest, size_t cap, bool *outTruncated);

#endif
