#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "re/primitive/i64f64.h"
#include "re/primitive/pair_parse.h"
#include "util/throw.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: i64f64
 * ============================================================================
 * An i64 integer half and an f64 fractional half in canonical pair form:
 * integer = trunc(value), fraction has the value's sign and lies in [-1, 1),
 * or is NaN when the input was inf/nan. The integer half is exact across the
 * whole i64 range; the fraction is an f64 (~15 decimal digits) estimate. The
 * string constructor is strict and whole-token; a cast that lands on exactly
 * 1.0 is clamped below 1.0 so the truncation invariant holds.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: i64f64 construction and projections
 * ============================================================================
 * i64f64_0      zero pair
 * i64f64_1      strict text -> pair
 * i64f64_2      exact pair, validated canonical range
 * toString      value projection
 * toStringStruct structure projection with the fraction bits
 * ============================================================================
 */

i64f64 i64f64_0(void) {
    return (i64f64){ .integer = 0, .fraction = 0.0 };
}

i64f64 i64f64_1(const char *text) {
    PairParseResult parsed;
    PairParseStatus status = pair_parseText(text, INT64_MAX, &parsed);
    if (status != PAIR_PARSE_OK) {
        THROW("i64f64: %s literal rejected",
              status == PAIR_PARSE_OVERFLOW ? "overflowing" : "malformed");
        return i64f64_0();
    }
    if (parsed.nonFinite)
        return (i64f64){ .integer = 0, .fraction = (f64) NAN };

    f64 fraction = parsed.fraction;
    if (fraction >= 1.0)
        fraction = nextafter(1.0, 0.0);
    else if (fraction <= -1.0)
        fraction = -nextafter(1.0, 0.0);
    return (i64f64){ .integer = parsed.integer, .fraction = fraction };
}

i64f64 i64f64_2(i64 integer, f64 fraction) {
    if (isnan(fraction))
        return (i64f64){ .integer = 0, .fraction = (f64) NAN };
    if (fraction < -1.0 || fraction >= 1.0) {
        THROW("i64f64: fraction outside [-1, 1)");
        return i64f64_0();
    }
    return (i64f64){ .integer = integer, .fraction = fraction };
}

void i64f64_toString(const i64f64 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "i64f64(integer=%" PRId64 ", fraction=%.15g)",
                           (*value).integer, (*value).fraction);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void i64f64_toStringStruct(const i64f64 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    u64 bits = 0;
    memcpy(&bits, &(*value).fraction, sizeof bits);
    i32 written = snprintf(dest, cap, "i64f64 { integer=%" PRId64 ", fraction=0x%016" PRIx64 " }",
                           (*value).integer, bits);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
