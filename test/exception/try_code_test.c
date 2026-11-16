// vexspoke/test/exception/try_code_test.c — the TryCode class _test.
//
// Proves exception/try_code.c: the taxonomy's two projections are stable,
// complete, and null-free, and TryCode_isOk is true only for TRY_OK.

#include "vex.h"
#include <stdio.h>
#include <string.h>

#include "exception/try_code.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static const TryCode ALL[] = {
    TRY_OK, TRY_NULL_ARG, TRY_BOUNDS, TRY_OVERFLOW, TRY_EMPTY,
    TRY_NOT_FOUND, TRY_IO, TRY_FORMAT, TRY_UNSUPPORTED, TRY_NO_MEMORY
};

i32 main() {
    bytes n = sizeof ALL / sizeof ALL[0];

    for (bytes i = 0; i < n; i++) {
        TryCode c = ALL[i];

        CHECK(TryCode_name(c) != nullptr);

        CHECK(TryCode_message(c) != nullptr);

        CHECK(TryCode_name(c)[0] != '\0');

        CHECK(TryCode_message(c)[0] != '\0');
        // Only TRY_OK is ok.
        CHECK(TryCode_isOk(c) == (c == TRY_OK));
    }

    // Symbolic names are exact and stable.
    CHECK(strcmp(TryCode_name(TRY_OK), "TRY_OK") == 0);

    CHECK(strcmp(TryCode_name(TRY_BOUNDS), "TRY_BOUNDS") == 0);

    CHECK(strcmp(TryCode_name(TRY_IO), "TRY_IO") == 0);

    // Unknown codes fall back without crashing.
    CHECK(strcmp(TryCode_name((TryCode) 9999), "TRY_UNKNOWN") == 0);

    CHECK(TryCode_message((TryCode) 9999) != nullptr);

    CHECK(!TryCode_isOk((TryCode) 9999));

    if (g_failures == 0) {
        printf("try_code_test: all assertions held\n");

        return 0;
    }

    printf("try_code_test: %d FAILURES\n", g_failures);

    return 1;
}
