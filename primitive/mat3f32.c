#include <stdio.h>

#include "re/primitive/mat3f32.h"
#include "util/throw.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: mat3f32
 * ============================================================================
 * A 3x3 f32 matrix stored as three vec4f32 columns (48 bytes, 16-aligned).
 * Column-major: column[i].x/y/z are the rows. The fourth lane of each column
 * is SIMD padding and every constructor writes it as 0.0f.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: mat3f32 construction and projections
 * ============================================================================
 * mat3f32_0     identity matrix
 * mat3f32_1     nine column-major values, fourth lanes zeroed
 * toString      value projection
 * toStringStruct structure projection (stride, padding, bytes)
 * ============================================================================
 */

mat3f32 mat3f32_0(void) {
    mat3f32 value = { 0 };
    value.column[0].x = 1.0f;
    value.column[1].y = 1.0f;
    value.column[2].z = 1.0f;
    return value;
}

mat3f32 mat3f32_1(const f32 *columns) {
    if (!columns) {
        THROW("mat3f32: null column source");
        return mat3f32_0();
    }
    mat3f32 value = { 0 };
    for (i32 column = 0; column < 3; column++) {
        value.column[column].x = columns[column * 3 + 0];
        value.column[column].y = columns[column * 3 + 1];
        value.column[column].z = columns[column * 3 + 2];
        value.column[column].w = 0.0f;
    }
    return value;
}

void mat3f32_toString(const mat3f32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap,
                           "mat3f32[c0=(%g,%g,%g) c1=(%g,%g,%g) c2=(%g,%g,%g)]",
                           (f64) (*value).column[0].x, (f64) (*value).column[0].y, (f64) (*value).column[0].z,
                           (f64) (*value).column[1].x, (f64) (*value).column[1].y, (f64) (*value).column[1].z,
                           (f64) (*value).column[2].x, (f64) (*value).column[2].y, (f64) (*value).column[2].z);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void mat3f32_toStringStruct(const mat3f32 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    u8 padZero = (u8) ((*value).column[0].w == 0.0f &&
                       (*value).column[1].w == 0.0f &&
                       (*value).column[2].w == 0.0f);
    i32 written = snprintf(dest, cap,
                           "mat3f32 { columns=3, stride=%zu, padZero=%s, bytes=%zu }",
                           sizeof(vec4f32), padZero ? "true" : "false", sizeof(mat3f32));
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
