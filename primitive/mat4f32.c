#include <stdio.h>

#include "re/primitive/mat4f32.h"
#include "util/throw.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: mat4f32
 * ============================================================================
 * A 4x4 f32 matrix stored as four vec4f32 columns (64 bytes, 16-aligned).
 * Column-major: column[i].x/y/z/w are the rows, and the fourth column carries
 * translation when the matrix is affine.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: mat4f32 construction and projections
 * ============================================================================
 * mat4f32_0     identity matrix
 * mat4f32_1     sixteen column-major values
 * toString      value projection
 * toStringStruct structure projection (stride, bytes)
 * ============================================================================
 */

mat4f32 mat4f32_0(void) {
    mat4f32 value = { 0 };
    value.column[0].x = 1.0f;
    value.column[1].y = 1.0f;
    value.column[2].z = 1.0f;
    value.column[3].w = 1.0f;
    return value;
}

mat4f32 mat4f32_1(const f32 *columns) {
    if (!columns) {
        THROW("mat4f32: null column source");
        return mat4f32_0();
    }
    mat4f32 value = { 0 };
    for (i32 column = 0; column < 4; column++) {
        value.column[column].x = columns[column * 4 + 0];
        value.column[column].y = columns[column * 4 + 1];
        value.column[column].z = columns[column * 4 + 2];
        value.column[column].w = columns[column * 4 + 3];
    }
    return value;
}

void mat4f32_toString(const mat4f32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap,
                           "mat4f32[c0=(%g,%g,%g,%g) c1=(%g,%g,%g,%g) "
                           "c2=(%g,%g,%g,%g) c3=(%g,%g,%g,%g)]",
                           (f64) (*value).column[0].x, (f64) (*value).column[0].y,
                           (f64) (*value).column[0].z, (f64) (*value).column[0].w,
                           (f64) (*value).column[1].x, (f64) (*value).column[1].y,
                           (f64) (*value).column[1].z, (f64) (*value).column[1].w,
                           (f64) (*value).column[2].x, (f64) (*value).column[2].y,
                           (f64) (*value).column[2].z, (f64) (*value).column[2].w,
                           (f64) (*value).column[3].x, (f64) (*value).column[3].y,
                           (f64) (*value).column[3].z, (f64) (*value).column[3].w);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void mat4f32_toStringStruct(const mat4f32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "mat4f32 { columns=4, stride=%zu, bytes=%zu }",
                           sizeof(vec4f32), sizeof(mat4f32));
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
