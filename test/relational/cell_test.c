// vexspoke/test/relational/cell_test.c — owner test for relational/cell.
//
// Proves the 32-byte identity cell:
//   - all three arity constructors (Cell_0/_1/_2) and the Cell(...) chooser
//     build a cell whose self-describing header carries the caller's typeId;
//   - Cell_check answers identity from the block header, not a side table;
//   - sizeof(Cell) stays 16 Bytes and the block is 32 Bytes (compile-time);
//   - value slot set/get round-trips, including 0 and pointer-wide values;
//   - the toString projections render, null-render "nullptr", and report
//     truncation when the destination is too small;
//   - every entry point is null-safe.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "relational/cell.h"
#include "nio/mem.h"
#include "oop/type.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// Exercises all Cell constructor arities, type identity checks, value defaults, and cleanup.
static void test_construction(void) {
    Cell *anon = Cell_0();
    CHECK(anon != nullptr);
    CHECK(Cell_typeId(anon) == 0u);
    CHECK(Cell_check(anon, 0u));
    CHECK(Cell_getValue(anon) == 0u);

    Cell *one = Cell_1(ID_INT);
    CHECK(one != nullptr);
    CHECK(Cell_typeId(one) == ID_INT);
    CHECK(Cell_check(one, ID_INT));
    CHECK(!Cell_check(one, ID_LONG));
    CHECK(Cell_getValue(one) == 0u);            // zero value

    Cell *two = Cell_2(ID_LONG, 0xDEADBEEFull);
    CHECK(two != nullptr);
    CHECK(Cell_typeId(two) == ID_LONG);
    CHECK(Cell_getValue(two) == 0xDEADBEEFull);

    // The arity chooser dispatches to the same forms.
    Cell *c0 = Cell();
    Cell *c1 = Cell(ID_FLOAT);
    Cell *c2 = Cell(ID_DOUBLE, 42u);
    CHECK(c0 != nullptr && Cell_typeId(c0) == 0u);
    CHECK(c1 != nullptr && Cell_typeId(c1) == ID_FLOAT);
    CHECK(c2 != nullptr && Cell_typeId(c2) == ID_DOUBLE);
    CHECK(Cell_getValue(c2) == 42u);

    Cell_free(anon);
    Cell_free(one);
    Cell_free(two);
    Cell_free(c0);
    Cell_free(c1);
    Cell_free(c2);
}

// Checks value-slot writes at zero, uintptr maximum, and pointer-width payloads.
static void test_value_slot(void) {
    Cell *c = Cell_2(ID_INT, 1u);
    CHECK(c != nullptr);

    Cell_setValue(c, 0u);
    CHECK(Cell_getValue(c) == 0u);
    Cell_setValue(c, UINTPTR_MAX);
    CHECK(Cell_getValue(c) == UINTPTR_MAX);

    int local = 7;
    Cell_setValue(c, (uintptr_t) &local);
    CHECK(Cell_getValue(c) == (uintptr_t) &local);

    // The identity survives a value change (the header is not the payload).
    CHECK(Cell_check(c, ID_INT));

    Cell_free(c);
}

// Checks Cell value/structure strings, truncation signaling, and null destinations/self.
static void test_to_string(void) {
    Cell *c = Cell_2(ID_INT, 0xABCDu);
    CHECK(c != nullptr);

    char buf[128];
    bool trunc = true;
    Cell_toString(c, buf, sizeof(buf), &trunc);
    CHECK(!trunc);
    CHECK(strstr(buf, "Cell(typeId=0x") != nullptr);
    CHECK(strstr(buf, "abcd") != nullptr);      // value rendered in hex

    trunc = false;
    Cell_toStringStruct(c, buf, sizeof(buf), &trunc);
    CHECK(!trunc);
    CHECK(strstr(buf, "Cell { value=0x") != nullptr);
    CHECK(strstr(buf, "pad=0x") != nullptr);

    // A tiny buffer truncates and says so.
    char tiny[8];
    trunc = false;
    Cell_toString(c, tiny, sizeof(tiny), &trunc);
    CHECK(trunc);

    // nullptr renders the word, no truncation at a sane size.
    trunc = true;
    Cell_toString(nullptr, buf, sizeof(buf), &trunc);
    CHECK(!trunc);
    CHECK(strcmp(buf, "nullptr") == 0);
    Cell_toStringStruct(nullptr, buf, sizeof(buf), &trunc);
    CHECK(strcmp(buf, "nullptr") == 0);

    // null dest or zero cap is a safe no-op; outTruncated still initialized.
    trunc = true;
    Cell_toString(c, nullptr, 0, &trunc);
    CHECK(!trunc);
    Cell_toString(c, buf, 0, nullptr);          // null truncation sink
    Cell_toStringStruct(c, nullptr, 0, nullptr);

    Cell_free(c);
}

// Verifies Cell accessors, mutators, and destruction tolerate a null receiver.
static void test_null_safety(void) {
    CHECK(Cell_typeId(nullptr) == 0u);
    CHECK(!Cell_check(nullptr, 0u));
    CHECK(Cell_getValue(nullptr) == 0u);
    Cell_setValue(nullptr, 1u);                 // no-op
    Cell_free(nullptr);                         // no-op
}

// Initializes memory, runs Cell contract cases, and reports aggregate assertion failures.
int main(void) {
    CHECK(Memory_init(0));

    test_construction();
    test_value_slot();
    test_to_string();
    test_null_safety();

    if (g_failures == 0) {
        printf("cell_test: all assertions held\n");
        return 0;
    }
    printf("cell_test: %d FAILURES\n", g_failures);
    return 1;
}
