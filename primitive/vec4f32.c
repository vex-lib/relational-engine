#include <stdio.h>

#include "re/primitive/vec4f32.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: vec4f32
 * ============================================================================
 * Four f32 lanes in a 16-byte, 16-aligned value: one 128-bit register, no
 * padding. This is the column type of the f32 matrices.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: vec4f32 construction and projections
 * ============================================================================
 * vec4f32_0     zero vector
 * vec4f32_4     component constructor
 * toString      value projection
 * toStringStruct structure projection
 * ============================================================================
 */

vec4f32 vec4f32_0(void) {
    return (vec4f32){ .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f };
}

vec4f32 vec4f32_4(f32 x, f32 y, f32 z, f32 w) {
    return (vec4f32){ .x = x, .y = y, .z = z, .w = w };
}

void vec4f32_toString(const vec4f32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "vec4f32(x=%g, y=%g, z=%g, w=%g)",
                           (f64) (*value).x, (f64) (*value).y,
                           (f64) (*value).z, (f64) (*value).w);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void vec4f32_toStringStruct(const vec4f32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "vec4f32 { x=%g, y=%g, z=%g, w=%g, bytes=%zu }",
                           (f64) (*value).x, (f64) (*value).y,
                           (f64) (*value).z, (f64) (*value).w, sizeof(vec4f32));
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
