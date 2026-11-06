#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "re/primitive/i32f64.h"
#include "re/primitive/pair_parse.h"
#include "util/throw.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: i32f64
 * ============================================================================
 * An i32 integer half and an f64 fractional half in canonical pair form:
 * integer = trunc(value), fraction has the value's sign and lies in [-1, 1),
 * or is NaN when the input was inf/nan. The string constructor is strict and
 * whole-token; the second constructor validates the canonical range. The
 * fraction is deliberately an estimate (~15 decimal digits), and a cast that
 * lands on exactly 1.0 is clamped below 1.0 so the truncation invariant holds.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * OVERVIEW: i32f64 construction and projections
 * ============================================================================
 * i32f64_0      zero pair
 * i32f64_1      strict text -> pair
 * i32f64_2      exact pair, validated canonical range
 * toString      value projection
 * toStringStruct structure projection with the fraction bits
 * ============================================================================
 */

i32f64 i32f64_0(void) {
    return (i32f64){ .integer = 0, .fraction = 0.0 };
}

i32f64 i32f64_1(const char *text) {
    PairParseResult parsed;
    PairParseStatus status = pair_parseText(text, INT32_MAX, &parsed);
    if (status != PAIR_PARSE_OK) {
        THROW("i32f64: %s literal rejected",
              status == PAIR_PARSE_OVERFLOW ? "overflowing" : "malformed");
        return i32f64_0();
    }
    if (parsed.nonFinite)
        return (i32f64){ .integer = 0, .fraction = (f64) NAN };

    f64 fraction = parsed.fraction;
    if (fraction >= 1.0)
        fraction = nextafter(1.0, 0.0);
    else if (fraction <= -1.0)
        fraction = -nextafter(1.0, 0.0);
    return (i32f64){ .integer = (i32) parsed.integer, .fraction = fraction };
}

i32f64 i32f64_2(i32 integer, f64 fraction) {
    if (isnan(fraction))
        return (i32f64){ .integer = 0, .fraction = (f64) NAN };
    if (fraction < -1.0 || fraction >= 1.0) {
        THROW("i32f64: fraction outside [-1, 1)");
        return i32f64_0();
    }
    return (i32f64){ .integer = integer, .fraction = fraction };
}

void i32f64_toString(const i32f64 *value, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!value) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    i32 written = snprintf(dest, cap, "i32f64(integer=%" PRId32 ", fraction=%.15g)",
                           (*value).integer, (*value).fraction);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void i32f64_toStringStruct(const i32f64 *value, char *dest, size_t cap, bool *outTruncated) {
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
    i32 written = snprintf(dest, cap, "i32f64 { integer=%" PRId32 ", fraction=0x%016" PRIx64 " }",
                           (*value).integer, bits);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
