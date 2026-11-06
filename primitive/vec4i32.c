#include <inttypes.h>
#include <stdio.h>

#include "re/primitive/vec4i32.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: vec4i32
 * ============================================================================
 * Four i32 lanes in a 16-byte, 16-aligned value: one 128-bit register, no
 * padding.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: vec4i32 construction and projections
 * ============================================================================
 * vec4i32_0     zero vector
 * vec4i32_4     component constructor
 * toString      value projection
 * toStringStruct structure projection
 * ============================================================================
 */

vec4i32 vec4i32_0(void) {
    return (vec4i32){ .x = 0, .y = 0, .z = 0, .w = 0 };
}

vec4i32 vec4i32_4(i32 x, i32 y, i32 z, i32 w) {
    return (vec4i32){ .x = x, .y = y, .z = z, .w = w };
}

void vec4i32_toString(const vec4i32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap,
                           "vec4i32(x=%" PRId32 ", y=%" PRId32 ", z=%" PRId32 ", w=%" PRId32 ")",
                           (*value).x, (*value).y, (*value).z, (*value).w);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void vec4i32_toStringStruct(const vec4i32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap,
                           "vec4i32 { x=%" PRId32 ", y=%" PRId32 ", z=%" PRId32
                           ", w=%" PRId32 ", bytes=%zu }",
                           (*value).x, (*value).y, (*value).z, (*value).w, sizeof(vec4i32));
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
