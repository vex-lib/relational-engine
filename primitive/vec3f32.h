#ifndef RE_PRIMITIVE_VEC3F32_H
#define RE_PRIMITIVE_VEC3F32_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/vec3f32.h — three f32 lanes plus one zeroed SIMD lane.
//
// A value primitive, not a heap object. The fourth lane is padding, never
// data: it exists so the value is 16 bytes and 16-byte aligned, matching one
// 128-bit register. Every constructor writes it as 0.0f so the bytes are
// deterministic.
//
// ;;INTENTION("width-spelled name: a vec of 3 f32 lanes. The fourth lane is
// padding, not a coordinate, and must never carry a frame or a tag.")

typedef struct vec3f32 {
    alignas(16) union {
        struct { f32 x, y, z, _pad; };
        f32 data[4];
    };
} vec3f32;

#define VEC3F32_SIZE ((size_t) 16)

_Static_assert(sizeof(vec3f32) == VEC3F32_SIZE, "vec3f32 must be 16 bytes");
_Static_assert(_Alignof(vec3f32) == 16, "vec3f32 must align to 16");
_Static_assert(offsetof(vec3f32, z) == 8, "vec3f32.z must sit at offset 8");
_Static_assert(offsetof(vec3f32, _pad) == 12, "vec3f32._pad must sit at offset 12");

vec3f32 vec3f32_0(void);
vec3f32 vec3f32_3(f32 x, f32 y, f32 z);

#define vec3f32(...) CONSTRUCTOR_DISPATCH(vec3f32, __VA_ARGS__)

void vec3f32_toString(const vec3f32 *value, char *dest, size_t cap, bool *outTruncated);
void vec3f32_toStringStruct(const vec3f32 *value, char *dest, size_t cap, bool *outTruncated);

#endif
