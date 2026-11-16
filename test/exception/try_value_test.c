// vexspoke/test/exception/try_value_test.c — the value-or-error primitive.
//
// Proves the Try family (the Per-File Battle Test Law):
//   - TryValue (scalar) and TryPtr (pointer) construct, round-trip, and isOk.
//   - The full value domain: 0, 1, UINT64_MAX, every TryCode — no sentinel
//     collision (the reason the Try exists at all).
//   - The CONTRACT: `value` is meaningful only when code == TRY_OK.
//   - Null-safety of every getter/setter (the Symmetric Getter/Setter
//     Completeness Law).
//   - String projections: null self, zero cap, null dest, and truncation
//     flagging (the toString Law / Truncation-Never-Silent clause).
//
// Deterministic, headless, zero allocation.

#include "vex.h"
#include <stdio.h>
#include <string.h>

#include "exception/try.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static const TryCode ALL_CODES[] = {
    TRY_OK, TRY_NULL_ARG, TRY_BOUNDS, TRY_OVERFLOW, TRY_EMPTY,
    TRY_NOT_FOUND, TRY_IO, TRY_FORMAT, TRY_UNSUPPORTED, TRY_NO_MEMORY
};

// --- TryValue: constructors + the CONTRACT ---
/** Verifies value-result constructors preserve successful values and error state. */
static fn testValueConstructors() {
    TryValue ok = TryValue_ok(42u);

    CHECK(TryValue_isOk(&ok));

    CHECK(TryValue_getValue(&ok) == 42u);

    CHECK(TryValue_getCode(&ok) == TRY_OK);

    TryValue err = TryValue_error(TRY_BOUNDS);

    CHECK(!TryValue_isOk(&err));

    CHECK(TryValue_getCode(&err) == TRY_BOUNDS);

    TryValue two = TryValue_2(7u, TRY_IO);

    CHECK(TryValue_getValue(&two) == 7u);

    CHECK(TryValue_getCode(&two) == TRY_IO);

    // The arity dispatch macro routes to TryValue_2.
    TryValue dispatched = TryValue(9u, TRY_FORMAT);

    CHECK(TryValue_getValue(&dispatched) == 9u);

    CHECK(TryValue_getCode(&dispatched) == TRY_FORMAT);
}

// --- TryValue: setters round-trip (Symmetric Getter/Setter Completeness) ---
/** Checks setters update the value/error alternative without corrupting the other state. */
static fn testValueSetters() {
    TryValue t = TryValue_ok(1u);

    TryValue_setValue(&t, 99u);

    CHECK(TryValue_getValue(&t) == 99u);

    TryValue_setCode(&t, TRY_IO);

    CHECK(TryValue_getCode(&t) == TRY_IO);

    CHECK(!TryValue_isOk(&t));

    TryValue_setCode(&t, TRY_OK);

    CHECK(TryValue_isOk(&t));
}

// --- TryValue: the full domain has no spare sentinel ---
/** Exercises zero, extreme, and empty values accepted by the value-result wrapper. */
static fn testValueBoundaries() {
    TryValue zero = TryValue_ok(0u);

    CHECK(TryValue_isOk(&zero));

    CHECK(TryValue_getValue(&zero) == 0u);       // legal 0 distinguishes from error

    TryValue max = TryValue_ok(UINT64_MAX);

    CHECK(TryValue_isOk(&max));

    CHECK(TryValue_getValue(&max) == UINT64_MAX);
}

// --- TryValue: null-safety of every accessor ---
/** Verifies null inputs and accessors return the wrapper's documented safe results. */
static fn testValueNullSafety() {
    CHECK(TryValue_isOk(nullptr) == false);

    CHECK(TryValue_getValue(nullptr) == 0u);

    CHECK(TryValue_getCode(nullptr) == TRY_NULL_ARG);

    TryValue_setValue(nullptr, 5u);              // must not crash

    TryValue_setCode(nullptr, TRY_IO);           // must not crash

    CHECK(1);
}

// --- TryValue: string projections ---
/** Checks bounded string projections for successful and failed value results. */
static fn testValueStrings() {
    char buf[128];

    bool trunc = false;

    TryValue ok = TryValue_ok(42u);

    TryValue_toString(&ok, buf, sizeof buf, &trunc);

    CHECK(!trunc);

    CHECK(strcmp(buf, "TryValue(value=42, code=TRY_OK)") == 0);

    TryValue err = TryValue_error(TRY_BOUNDS);

    TryValue_toString(&err, buf, sizeof buf, &trunc);

    CHECK(strstr(buf, "TRY_BOUNDS") != nullptr);

    CHECK(strstr(buf, "index outside the active range") != nullptr);

    TryValue_toStringStruct(&ok, buf, sizeof buf, &trunc);

    CHECK(strcmp(buf, "TryValue { value=42, code=TRY_OK }") == 0);

    // null self writes "nullptr"
    TryValue_toString(nullptr, buf, sizeof buf, &trunc);

    CHECK(strcmp(buf, "nullptr") == 0);

    CHECK(!trunc);

    // zero cap: no write, no crash, not flagged truncated
    trunc = true;

    TryValue_toString(&ok, buf, 0u, &trunc);

    CHECK(!trunc);

    // null dest: no crash
    TryValue_toString(&ok, nullptr, sizeof buf, &trunc);

    CHECK(!trunc);

    // too-small buffer: truncation flagged
    trunc = false;

    TryValue_toString(&ok, buf, 4u, &trunc);

    CHECK(trunc);
}

// --- TryPtr: the pointer-shaped sibling ---
/** Exercises pointer-result success, failure, and null-pointee behavior. */
static fn testPtr() {
    i32 payload = 1234;

    char buf[128];

    bool trunc = false;

    TryPtr ok = TryPtr_ok(&payload);

    CHECK(TryPtr_isOk(&ok));

    CHECK(TryPtr_getValue(&ok) == &payload);

    CHECK(TryPtr_getCode(&ok) == TRY_OK);

    TryPtr err = TryPtr_error(TRY_IO);

    CHECK(!TryPtr_isOk(&err));

    CHECK(TryPtr_getValue(&err) == nullptr);

    CHECK(TryPtr_getCode(&err) == TRY_IO);

    TryPtr two = TryPtr_2(nullptr, TRY_NOT_FOUND);

    CHECK(TryPtr_getCode(&two) == TRY_NOT_FOUND);

    TryPtr_setValue(&two, &payload);

    CHECK(TryPtr_getValue(&two) == &payload);

    TryPtr_setCode(&two, TRY_OK);

    CHECK(TryPtr_isOk(&two));

    CHECK(TryPtr_isOk(nullptr) == false);

    CHECK(TryPtr_getValue(nullptr) == nullptr);

    CHECK(TryPtr_getCode(nullptr) == TRY_NULL_ARG);

    TryPtr_setValue(nullptr, &payload);          // must not crash

    TryPtr_setCode(nullptr, TRY_IO);             // must not crash

    TryPtr_toString(&err, buf, sizeof buf, &trunc);

    CHECK(strstr(buf, "TRY_IO") != nullptr);

    TryPtr_toString(&ok, buf, sizeof buf, &trunc);

    CHECK(strstr(buf, "TRY_OK") != nullptr);

    TryPtr_toString(nullptr, buf, sizeof buf, &trunc);

    CHECK(strcmp(buf, "nullptr") == 0);

    trunc = false;

    TryPtr_toString(&ok, buf, 4u, &trunc);

    CHECK(trunc);
}

// --- TryCode taxonomy ---
/** Confirms error codes retain their declared categories and identifying values. */
static fn testCodeTaxonomy() {
    bytes count = sizeof ALL_CODES / sizeof ALL_CODES[0];

    for (bytes i = 0; i < count; i++) {
        TryCode code = ALL_CODES[i];

        CHECK(TryCode_name(code) != nullptr);

        CHECK(TryCode_message(code) != nullptr);

        CHECK((code == TRY_OK) == TryCode_isOk(code));   // only TRY_OK is ok
    }

    CHECK(strcmp(TryCode_name(TRY_OK), "TRY_OK") == 0);

    CHECK(strcmp(TryCode_name((TryCode) 9999), "TRY_UNKNOWN") == 0);
}

/** Runs the TryValue and TryPtr owner cases and reports aggregate assertion status. */
i32 main() {
    testValueConstructors();

    testValueSetters();

    testValueBoundaries();

    testValueNullSafety();

    testValueStrings();

    testPtr();

    testCodeTaxonomy();

    if (g_failures == 0) {
        printf("try_value_test: all checks passed\n");

        return 0;
    }

    printf("try_value_test: %d FAILURES\n", g_failures);

    return 1;
}
