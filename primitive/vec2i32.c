#include <inttypes.h>
#include <stdio.h>

#include "re/primitive/vec2i32.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: vec2i32
 * ============================================================================
 * Two i32 lanes in a single 8-byte, 8-aligned value. There is no padding: the
 * whole type is the data.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: vec2i32 construction and projections
 * ============================================================================
 * vec2i32_0     zero vector
 * vec2i32_2     component constructor
 * toString      value projection
 * toStringStruct structure projection
 * ============================================================================
 */

vec2i32 vec2i32_0(void) {
    return (vec2i32){ .x = 0, .y = 0 };
}

vec2i32 vec2i32_2(i32 x, i32 y) {
    return (vec2i32){ .x = x, .y = y };
}

void vec2i32_toString(const vec2i32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "vec2i32(x=%" PRId32 ", y=%" PRId32 ")",
                           (*value).x, (*value).y);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void vec2i32_toStringStruct(const vec2i32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "vec2i32 { x=%" PRId32 ", y=%" PRId32 ", bytes=%zu }",
                           (*value).x, (*value).y, sizeof(vec2i32));
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
