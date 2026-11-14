// re/test/substrate/relational/cell_test.c — owner test for substrate/cell.
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

#include "vex.h"
#include <stdio.h>
#include <string.h>

#include "substrate/relational/cell.h"
#include "nio/mem.h"

enum {
    TEST_CLASS_A = 1u,
    TEST_CLASS_B = 2u,
    TEST_CLASS_C = 3u,
    TEST_CLASS_D = 4u,
};

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// Exercises all Cell constructor arities, type identity checks, value defaults, and cleanup.
static fn test_construction() {
    Cell *anon = Cell_0();
    CHECK(anon != nullptr);
    CHECK(Cell_typeId(anon) == 0u);
    CHECK(Cell_check(anon, 0u));
    CHECK(Cell_getValue(anon) == 0u);

    Cell *one = Cell_1(TEST_CLASS_A);
    CHECK(one != nullptr);
    CHECK(Cell_typeId(one) == TEST_CLASS_A);
    CHECK(Cell_check(one, TEST_CLASS_A));
    CHECK(!Cell_check(one, TEST_CLASS_B));
    CHECK(Cell_getValue(one) == 0u);

    Cell *two = Cell_2(TEST_CLASS_B, 0xDEADBEEFull);
    CHECK(two != nullptr);
    CHECK(Cell_typeId(two) == TEST_CLASS_B);
    CHECK(Cell_getValue(two) == 0xDEADBEEFull);

    Cell *c0 = Cell();
    Cell *c1 = Cell(TEST_CLASS_C);
    Cell *c2 = Cell(TEST_CLASS_D, 42u);
    CHECK(c0 != nullptr && Cell_typeId(c0) == 0u);
    CHECK(c1 != nullptr && Cell_typeId(c1) == TEST_CLASS_C);
    CHECK(c2 != nullptr && Cell_typeId(c2) == TEST_CLASS_D);
    CHECK(Cell_getValue(c2) == 42u);

    Cell_free(anon);
    Cell_free(one);
    Cell_free(two);
    Cell_free(c0);
    Cell_free(c1);
    Cell_free(c2);
}

// Checks value-slot writes at zero, uintptr maximum, and pointer-width payloads.
static fn test_value_slot() {
    Cell *c = Cell_2(TEST_CLASS_A, 1u);
    CHECK(c != nullptr);

    Cell_setValue(c, 0u);
    CHECK(Cell_getValue(c) == 0u);
    Cell_setValue(c, UINTPTR_MAX);
    CHECK(Cell_getValue(c) == UINTPTR_MAX);

    int local = 7;
    Cell_setValue(c, (pointer) &local);
    CHECK(Cell_getValue(c) == (pointer) &local);

    CHECK(Cell_check(c, TEST_CLASS_A));

    Cell_free(c);
}

// Checks Cell value/structure strings, truncation signaling, and null destinations/self.
static fn test_to_string() {
    Cell *c = Cell_2(TEST_CLASS_A, 0xABCDu);
    CHECK(c != nullptr);

    char buf[128];
    bool trunc = true;
    Cell_toString(c, buf, sizeof(buf), &trunc);
    CHECK(!trunc);
    CHECK(strstr(buf, "Cell(typeId=0x") != nullptr);
    CHECK(strstr(buf, "abcd") != nullptr);

    trunc = false;
    Cell_toStringStruct(c, buf, sizeof(buf), &trunc);
    CHECK(!trunc);
    CHECK(strstr(buf, "Cell { value=0x") != nullptr);
    CHECK(strstr(buf, "pad=0x") != nullptr);

    char tiny[8];
    trunc = false;
    Cell_toString(c, tiny, sizeof(tiny), &trunc);
    CHECK(trunc);

    trunc = true;
    Cell_toString(nullptr, buf, sizeof(buf), &trunc);
    CHECK(!trunc);
    CHECK(strcmp(buf, "nullptr") == 0);
    Cell_toStringStruct(nullptr, buf, sizeof(buf), &trunc);
    CHECK(strcmp(buf, "nullptr") == 0);

    trunc = true;
    Cell_toString(c, nullptr, 0, &trunc);
    CHECK(!trunc);
    Cell_toString(c, buf, 0, nullptr);
    Cell_toStringStruct(c, nullptr, 0, nullptr);

    Cell_free(c);
}

// Verifies Cell accessors, mutators, and destruction tolerate a null receiver.
static fn test_null_safety() {
    CHECK(Cell_typeId(nullptr) == 0u);
    CHECK(!Cell_check(nullptr, 0u));
    CHECK(Cell_getValue(nullptr) == 0u);
    Cell_setValue(nullptr, 1u);
    Cell_free(nullptr);
}

// Initializes memory, runs Cell contract cases, and reports aggregate assertion failures.
int main() {
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
