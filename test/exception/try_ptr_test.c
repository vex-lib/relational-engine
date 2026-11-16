// vexspoke/test/exception/try_ptr_test.c — the TryPtr class _test.
//
// Proves exception/try_ptr.c: by-value construction, the CONTRACT (value is
// meaningful only when code == TRY_OK), symmetric getters/setters, null
// safety, and the bounded string projections.

#include "vex.h"
#include <stdio.h>
#include <string.h>

#include "exception/try_ptr.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

i32 main() {
    i32 payload = 7;

    i32 other = 8;

    // Constructors + the CONTRACT.
    TryPtr ok = TryPtr_ok(&payload);

    CHECK(TryPtr_isOk(&ok));

    CHECK(TryPtr_getValue(&ok) == &payload);

    CHECK(TryPtr_getCode(&ok) == TRY_OK);

    TryPtr err = TryPtr_error(TRY_IO);

    CHECK(!TryPtr_isOk(&err));

    CHECK(TryPtr_getValue(&err) == nullptr);

    CHECK(TryPtr_getCode(&err) == TRY_IO);

    TryPtr two = TryPtr_2(&other, TRY_NOT_FOUND);

    CHECK(TryPtr_getValue(&two) == &other);

    CHECK(TryPtr_getCode(&two) == TRY_NOT_FOUND);

    // Dispatch macro routes to the 2-arg form.
    TryPtr dispatched = TryPtr(&payload, TRY_FORMAT);

    CHECK(TryPtr_getCode(&dispatched) == TRY_FORMAT);

    // Setters round-trip.
    TryPtr t = TryPtr_ok(nullptr);

    TryPtr_setValue(&t, &payload);

    CHECK(TryPtr_getValue(&t) == &payload);

    TryPtr_setCode(&t, TRY_BOUNDS);

    CHECK(TryPtr_getCode(&t) == TRY_BOUNDS);

    CHECK(!TryPtr_isOk(&t));

    // Null safety.
    CHECK(!TryPtr_isOk(nullptr));

    CHECK(TryPtr_getValue(nullptr) == nullptr);

    CHECK(TryPtr_getCode(nullptr) == TRY_NULL_ARG);

    TryPtr_setValue(nullptr, &payload);

    TryPtr_setCode(nullptr, TRY_IO);

    CHECK(1);

    // String projections.
    char buf[128];

    bool trunc = false;

    TryPtr_toString(&err, buf, sizeof buf, &trunc);

    CHECK(strstr(buf, "TRY_IO") != nullptr);

    CHECK(!trunc);

    TryPtr_toString(&ok, buf, sizeof buf, &trunc);

    CHECK(strstr(buf, "TRY_OK") != nullptr);

    TryPtr_toStringStruct(&ok, buf, sizeof buf, &trunc);

    CHECK(strstr(buf, "value=") != nullptr);

    TryPtr_toString(nullptr, buf, sizeof buf, &trunc);

    CHECK(strcmp(buf, "nullptr") == 0);

    trunc = false;

    TryPtr_toString(&ok, buf, 4u, &trunc);

    CHECK(trunc);

    if (g_failures == 0) {
        printf("try_ptr_test: all assertions held\n");

        return 0;
    }

    printf("try_ptr_test: %d FAILURES\n", g_failures);

    return 1;
}
