#include <stdio.h>

#include "re/primitive/mat3f64.h"
#include "util/throw.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: mat3f64
 * ============================================================================
 * A 3x3 f64 matrix stored as three 4-lane columns (96 bytes, 32-aligned).
 * Column-major: column[i].x/y/z are the rows. The fourth lane is SIMD padding
 * and every constructor writes it as 0.0.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: mat3f64 construction and projections
 * ============================================================================
 * mat3f64_0     identity matrix
 * mat3f64_1     nine column-major values, fourth lanes zeroed
 * toString      value projection
 * toStringStruct structure projection (stride, padding, bytes)
 * ============================================================================
 */

mat3f64 mat3f64_0(void) {
    mat3f64 value = { 0 };
    value.column[0].x = 1.0;
    value.column[1].y = 1.0;
    value.column[2].z = 1.0;
    return value;
}

mat3f64 mat3f64_1(const f64 *columns) {
    if (!columns) {
        THROW("mat3f64: null column source");
        return mat3f64_0();
    }
    mat3f64 value = { 0 };
    for (i32 column = 0; column < 3; column++) {
        value.column[column].x = columns[column * 3 + 0];
        value.column[column].y = columns[column * 3 + 1];
        value.column[column].z = columns[column * 3 + 2];
        value.column[column]._pad = 0.0;
    }
    return value;
}

void mat3f64_toString(const mat3f64 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap,
                           "mat3f64[c0=(%g,%g,%g) c1=(%g,%g,%g) c2=(%g,%g,%g)]",
                           (*value).column[0].x, (*value).column[0].y, (*value).column[0].z,
                           (*value).column[1].x, (*value).column[1].y, (*value).column[1].z,
                           (*value).column[2].x, (*value).column[2].y, (*value).column[2].z);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void mat3f64_toStringStruct(const mat3f64 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    u8 padZero = (u8) ((*value).column[0]._pad == 0.0 &&
                       (*value).column[1]._pad == 0.0 &&
                       (*value).column[2]._pad == 0.0);
    i32 written = snprintf(dest, cap,
                           "mat3f64 { columns=3, stride=%zu, padZero=%s, bytes=%zu }",
                           sizeof((*value).column[0]), padZero ? "true" : "false", sizeof(mat3f64));
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
