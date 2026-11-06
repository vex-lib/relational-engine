// pair_test.c — the i32f32 / i32f64 / i64f32 / i64f64 contract.
//
// Proves the canonical pair form, the strict whole-token parser, the named
// constants, the inf/nan sentinel, the documented precision estimate, the
// truncation boundary clamp, overflow rejection, and both string projections.
//
// The reject cases intentionally call THROW, so a passing run writes the
// expected [vex] diagnostics to stderr. The exit status is the oracle.

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "re/primitive/i32f32.h"
#include "re/primitive/i32f64.h"
#include "re/primitive/i64f32.h"
#include "re/primitive/i64f64.h"

static i32 failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static i32 near(f64 left, f64 right, f64 tolerance) {
    return fabs(left - right) <= tolerance;
}

int main(void) {
    // Canonical pair form.
    i32f32 a = i32f32("1.375");
    CHECK(a.integer == 1);
    CHECK(a.fraction == 0.375f);

    i32f32 negative = i32f32("-1.5");
    CHECK(negative.integer == -1);
    CHECK(negative.fraction == -0.5f);

    i32f32 spaced = i32f32(" 2.25 ");
    CHECK(spaced.integer == 2);
    CHECK(spaced.fraction == 0.25f);

    i32f32 half = i32f32(".5");
    CHECK(half.integer == 0);
    CHECK(half.fraction == 0.5f);

    i32f32 whole = i32f32("5.");
    CHECK(whole.integer == 5);
    CHECK(whole.fraction == 0.0f);

    // Named constants, with an optional sign.
    i32f32 pi = i32f32("pi");
    CHECK(pi.integer == 3);
    CHECK(near((f64) pi.fraction, 0.141592653589793, 1e-6));

    i32f32 negativePi = i32f32("-pi");
    CHECK(negativePi.integer == -3);
    CHECK(near((f64) negativePi.fraction, -0.141592653589793, 1e-6));

    i32f32 tau = i32f32("TAU");
    CHECK(tau.integer == 6);

    i32f32 e = i32f32("e");
    CHECK(e.integer == 2);
    CHECK(near((f64) e.fraction, 0.718281828459045, 1e-6));

    i32f32 phi = i32f32("phi");
    CHECK(phi.integer == 1);
    CHECK(near((f64) phi.fraction, 0.618033988749895, 1e-6));

    // Non-finite input maps to the documented NaN sentinel, sign lost.
    i32f32 nanValue = i32f32("nan");
    CHECK(nanValue.integer == 0);
    CHECK(isnan(nanValue.fraction));
    i32f32 infinity = i32f32("-inf");
    CHECK(infinity.integer == 0);
    CHECK(isnan(infinity.fraction));
    i32f32 infinityWord = i32f32("Infinity");
    CHECK(isnan(infinityWord.fraction));

    // Exponents move the decimal point before the split.
    i32f32 hundred = i32f32("1e2");
    CHECK(hundred.integer == 100);
    CHECK(hundred.fraction == 0.0f);
    i32f32 fifteen = i32f32("1.5e1");
    CHECK(fifteen.integer == 15);
    CHECK(fifteen.fraction == 0.0f);
    i32f32 tenth = i32f32("1.5e-1");
    CHECK(tenth.integer == 0);
    CHECK(near((f64) tenth.fraction, 0.15, 1e-7));

    // Strict whole-token rejection: no silent truncation.
    i32f32 garbage = i32f32("3.14abc");
    CHECK(garbage.integer == 0 && garbage.fraction == 0.0f);
    i32f32 comma = i32f32("1,5");
    CHECK(comma.integer == 0 && comma.fraction == 0.0f);
    i32f32 empty = i32f32("");
    CHECK(empty.integer == 0 && empty.fraction == 0.0f);
    i32f32 signOnly = i32f32("+");
    CHECK(signOnly.integer == 0 && signOnly.fraction == 0.0f);

    // Range rejection and the signed minimum.
    i32f32 overflow = i32f32("2147483648");
    CHECK(overflow.integer == 0 && overflow.fraction == 0.0f);
    i32f32 minimum = i32f32("-2147483648");
    CHECK(minimum.integer == INT32_MIN);
    CHECK(minimum.fraction == 0.0f);

    // integer = trunc(value) survives the 1.0 rounding boundary.
    i32f32 upperEdge = i32f32("2147483647.999999999");
    CHECK(upperEdge.integer == INT32_MAX);
    CHECK(upperEdge.fraction > 0.0f && upperEdge.fraction < 1.0f);
    i32f32 lowerEdge = i32f32("-0.999999999");
    CHECK(lowerEdge.integer == 0);
    CHECK(lowerEdge.fraction < 0.0f && lowerEdge.fraction > -1.0f);

    // The exact constructor validates the canonical range instead of silently
    // carrying into the integer.
    i32f32 exact = i32f32_2(-1, -0.5f);
    CHECK(exact.integer == -1 && exact.fraction == -0.5f);
    i32f32 nonCanonical = i32f32_2(1, 1.5f);
    CHECK(nonCanonical.integer == 0 && nonCanonical.fraction == 0.0f);
    i32f32 exactNaN = i32f32_2(7, (f32) NAN);
    CHECK(exactNaN.integer == 0 && isnan(exactNaN.fraction));

    // Wider fractions: i32f64 keeps about fifteen digits.
    i32f64 precise = i32f64("1.3749872984729874");
    CHECK(precise.integer == 1);
    CHECK(near(precise.fraction, 0.3749872984729874, 1e-15));
    i32f64 longer = i32f64("1.3749872984729874298");
    CHECK(longer.integer == 1);
    CHECK(near(longer.fraction, 0.3749872984729874, 1e-15));

    // i64 integer halves are exact across the full range.
    i64f64 maximum = i64f64("9223372036854775807");
    CHECK(maximum.integer == INT64_MAX);
    CHECK(maximum.fraction == 0.0);
    i64f64 minimum64 = i64f64("-9223372036854775808");
    CHECK(minimum64.integer == INT64_MIN);
    CHECK(minimum64.fraction == 0.0);
    i64f64 overflow64 = i64f64("9223372036854775808");
    CHECK(overflow64.integer == 0 && overflow64.fraction == 0.0);

    i64f32 large = i64f32("4294967296.5");
    CHECK(large.integer == INT64_C(4294967296));
    CHECK(large.fraction == 0.5f);

    // Zero constructors.
    i32f32 zero = i32f32_0();
    CHECK(zero.integer == 0 && zero.fraction == 0.0f);
    i64f64 zero64 = i64f64_0();
    CHECK(zero64.integer == 0 && zero64.fraction == 0.0);

    // String projections, cold path.
    char buffer[128];
    bool truncated = false;
    i32f32_toString(&a, buffer, sizeof buffer, &truncated);
    CHECK(!truncated);
    CHECK(strstr(buffer, "i32f32(integer=1") != nullptr);
    i32f32_toStringStruct(&a, buffer, sizeof buffer, &truncated);
    CHECK(!truncated);
    CHECK(strstr(buffer, "fraction=0x") != nullptr);
    i32f32_toString(nullptr, buffer, sizeof buffer, &truncated);
    CHECK(strcmp(buffer, "nullptr") == 0);
    i32f32_toString(&a, buffer, 4, &truncated);
    CHECK(truncated);

    if (failures != 0) {
        fprintf(stderr, "pair_test: %" PRId32 " failure(s)\n", failures);
        return 1;
    }
    printf("pair_test: pair form, parser, constants, sentinel, boundaries, projections\n");
    return 0;
}
