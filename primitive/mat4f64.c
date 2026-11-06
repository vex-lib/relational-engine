#include <stdio.h>

#include "re/primitive/mat4f64.h"
#include "util/throw.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: mat4f64
 * ============================================================================
 * A 4x4 f64 matrix stored as four 4-lane columns (128 bytes, 32-aligned).
 * Column-major, with no padding: the fourth column carries translation when
 * the matrix is affine.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: mat4f64 construction and projections
 * ============================================================================
 * mat4f64_0     identity matrix
 * mat4f64_1     sixteen column-major values
 * toString      value projection
 * toStringStruct structure projection (stride, bytes)
 * ============================================================================
 */

mat4f64 mat4f64_0(void) {
    mat4f64 value = { 0 };
    value.column[0].x = 1.0;
    value.column[1].y = 1.0;
    value.column[2].z = 1.0;
    value.column[3].w = 1.0;
    return value;
}

mat4f64 mat4f64_1(const f64 *columns) {
    if (!columns) {
        THROW("mat4f64: null column source");
        return mat4f64_0();
    }
    mat4f64 value = { 0 };
    for (i32 column = 0; column < 4; column++) {
        value.column[column].x = columns[column * 4 + 0];
        value.column[column].y = columns[column * 4 + 1];
        value.column[column].z = columns[column * 4 + 2];
        value.column[column].w = columns[column * 4 + 3];
    }
    return value;
}

void mat4f64_toString(const mat4f64 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap,
                           "mat4f64[c0=(%g,%g,%g,%g) c1=(%g,%g,%g,%g) "
                           "c2=(%g,%g,%g,%g) c3=(%g,%g,%g,%g)]",
                           (*value).column[0].x, (*value).column[0].y,
                           (*value).column[0].z, (*value).column[0].w,
                           (*value).column[1].x, (*value).column[1].y,
                           (*value).column[1].z, (*value).column[1].w,
                           (*value).column[2].x, (*value).column[2].y,
                           (*value).column[2].z, (*value).column[2].w,
                           (*value).column[3].x, (*value).column[3].y,
                           (*value).column[3].z, (*value).column[3].w);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void mat4f64_toStringStruct(const mat4f64 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "mat4f64 { columns=4, stride=%zu, bytes=%zu }",
                           sizeof((*value).column[0]), sizeof(mat4f64));
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
