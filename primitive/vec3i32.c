#include <inttypes.h>
#include <stdio.h>

#include "re/primitive/vec3i32.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: vec3i32
 * ============================================================================
 * Three i32 lanes plus one zeroed padding lane in a 16-byte, 16-aligned value.
 * The padding lane exists for SIMD width and is never data.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: vec3i32 construction and projections
 * ============================================================================
 * vec3i32_0     zero vector, padding lane zeroed
 * vec3i32_3     component constructor, padding lane zeroed
 * toString      value projection (x, y, z)
 * toStringStruct structure projection (includes the padding lane)
 * ============================================================================
 */

vec3i32 vec3i32_0(void) {
    return (vec3i32){ .x = 0, .y = 0, .z = 0, ._pad = 0 };
}

vec3i32 vec3i32_3(i32 x, i32 y, i32 z) {
    return (vec3i32){ .x = x, .y = y, .z = z, ._pad = 0 };
}

void vec3i32_toString(const vec3i32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "vec3i32(x=%" PRId32 ", y=%" PRId32 ", z=%" PRId32 ")",
                           (*value).x, (*value).y, (*value).z);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void vec3i32_toStringStruct(const vec3i32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap,
                           "vec3i32 { x=%" PRId32 ", y=%" PRId32 ", z=%" PRId32
                           ", pad=%" PRId32 ", bytes=%zu }",
                           (*value).x, (*value).y, (*value).z,
                           (*value)._pad, sizeof(vec3i32));
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
