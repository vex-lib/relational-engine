#include <stdio.h>

#include "re/primitive/vec3f32.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: vec3f32
 * ============================================================================
 * Three f32 lanes plus one zeroed padding lane in a 16-byte, 16-aligned value.
 * The padding lane exists for SIMD width and is never data. Every constructor
 * writes it as 0.0f so two equal vectors have equal bytes.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: vec3f32 construction and projections
 * ============================================================================
 * vec3f32_0     zero vector, padding lane zeroed
 * vec3f32_3     component constructor, padding lane zeroed
 * toString      value projection (x, y, z)
 * toStringStruct structure projection (includes the padding lane)
 * ============================================================================
 */

vec3f32 vec3f32_0(void) {
    return (vec3f32){ .x = 0.0f, .y = 0.0f, .z = 0.0f, ._pad = 0.0f };
}

vec3f32 vec3f32_3(f32 x, f32 y, f32 z) {
    return (vec3f32){ .x = x, .y = y, .z = z, ._pad = 0.0f };
}

void vec3f32_toString(const vec3f32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "vec3f32(x=%g, y=%g, z=%g)",
                           (f64) (*value).x, (f64) (*value).y, (f64) (*value).z);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void vec3f32_toStringStruct(const vec3f32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "vec3f32 { x=%g, y=%g, z=%g, pad=%g, bytes=%zu }",
                           (f64) (*value).x, (f64) (*value).y, (f64) (*value).z,
                           (f64) (*value)._pad, sizeof(vec3f32));
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
