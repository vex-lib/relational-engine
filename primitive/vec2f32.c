#include <stdio.h>

#include "re/primitive/vec2f32.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: vec2f32
 * ============================================================================
 * Two f32 lanes in a single 8-byte, 8-aligned value. There is no padding: the
 * whole type is the data. Constructors are zero and the two-component form;
 * the string projections are cold-path value and structure views.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: vec2f32 construction and projections
 * ============================================================================
 * vec2f32_0     zero vector
 * vec2f32_2     component constructor
 * toString      value projection
 * toStringStruct structure projection
 * ============================================================================
 */

vec2f32 vec2f32_0(void) {
    return (vec2f32){ .x = 0.0f, .y = 0.0f };
}

vec2f32 vec2f32_2(f32 x, f32 y) {
    return (vec2f32){ .x = x, .y = y };
}

void vec2f32_toString(const vec2f32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "vec2f32(x=%g, y=%g)",
                           (f64) (*value).x, (f64) (*value).y);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void vec2f32_toStringStruct(const vec2f32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "vec2f32 { x=%g, y=%g, bytes=%zu }",
                           (f64) (*value).x, (f64) (*value).y, sizeof(vec2f32));
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
