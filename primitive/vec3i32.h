#ifndef RE_PRIMITIVE_VEC3I32_H
#define RE_PRIMITIVE_VEC3I32_H

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/constructor.h"
#include "util/types.h"

// re/primitive/vec3i32.h — three i32 lanes plus one zeroed SIMD lane.
//
// The integer twin of vec3f32. The fourth lane is padding, never data: it is
// written as 0 by every constructor so the bytes are deterministic.
//
// ;;INTENTION("width-spelled name: a vec of 3 i32 lanes. The fourth lane is
// padding, not a tag.")

typedef struct vec3i32 {
    alignas(16) union {
        struct { i32 x, y, z, _pad; };
        i32 data[4];
    };
} vec3i32;

#define VEC3I32_SIZE ((size_t) 16)

_Static_assert(sizeof(vec3i32) == VEC3I32_SIZE, "vec3i32 must be 16 bytes");
_Static_assert(_Alignof(vec3i32) == 16, "vec3i32 must align to 16");
_Static_assert(offsetof(vec3i32, z) == 8, "vec3i32.z must sit at offset 8");
_Static_assert(offsetof(vec3i32, _pad) == 12, "vec3i32._pad must sit at offset 12");

vec3i32 vec3i32_0(void);
vec3i32 vec3i32_3(i32 x, i32 y, i32 z);

#define vec3i32(...) CONSTRUCTOR_DISPATCH(vec3i32, __VA_ARGS__)

void vec3i32_toString(const vec3i32 *value, char *dest, size_t cap, bool *outTruncated);
void vec3i32_toStringStruct(const vec3i32 *value, char *dest, size_t cap, bool *outTruncated);

#endif
