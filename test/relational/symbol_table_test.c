// vexspoke/test/relational/symbol_table_test.c — owner test for
// relational/symbol_table.
//
// The relational symbol registry maps a name to (classId, pointer):
//   - init/shutdown lifecycle, double shutdown, inactive-table safety;
//   - instant (create-or-FAIL: duplicates rejected, never updated), pool-first
//     lookup, case folding to lowercase, and the ASCII name policy
//     (empty/overlong/illegal rejected);
//   - rename moves both the name and the row identity, and rejects unknown
//     olds, collisions, and bad new names;
//   - findByClass returns matches in insertion order and reports the total
//     even when the caller's buffer is short;
//   - pointer get/set/compareAndSet semantics, including failure atomicity;
//   - getName buffer sizing, bad ids, and nullptr out;
//   - failure observability: every promised cold rejection writes one
//     "[variable] ..." line to stderr, and the safe result is still returned;
//   - nullptr for every pointer parameter.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "relational/symbol_table.h"
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

// Checks create-or-fail admission, case-folded lookup, class/pointer reads, and duplicate preservation.
static void test_instant_and_lookup(SymbolTable *v) {
    int32_t id0 = SymbolTable_instant(v, "alpha", ID_INT, 0x10u);
    CHECK(id0 == 0);
    CHECK(SymbolTable_getId(v, "alpha") == id0);
    CHECK(SymbolTable_getId(v, "ALPHA") == id0);        // case-folded
    CHECK(SymbolTable_getClassId(v, id0) == ID_INT);
    CHECK(SymbolTable_getPointer(v, id0) == 0x10u);

    // Create-or-FAIL: a duplicate is rejected, the row is NOT updated.
    int32_t dup = SymbolTable_instant(v, "alpha", ID_LONG, 0x99u);
    CHECK(dup == -1);
    CHECK(SymbolTable_getClassId(v, id0) == ID_INT);
    CHECK(SymbolTable_getPointer(v, id0) == 0x10u);

    // Unknown name resolves silently to -1.
    CHECK(SymbolTable_getId(v, "nope") == -1);
    CHECK(SymbolTable_getId(v, "unknown.name") == -1);

    CHECK(SymbolTable_getActiveCount(v) == 1);
}

// Exercises invalid/valid name boundaries and confirms rejected names do not add rows.
static void test_name_policy(SymbolTable *v) {
    size_t before = SymbolTable_getActiveCount(v);

    CHECK(SymbolTable_instant(v, nullptr, ID_INT, 1u) == -1);
    CHECK(SymbolTable_instant(v, "", ID_INT, 1u) == -1);
    CHECK(SymbolTable_instant(v, "has space", ID_INT, 1u) == -1);
    CHECK(SymbolTable_instant(v, "has-dash", ID_INT, 1u) == -1);
    CHECK(SymbolTable_instant(v, "sla/sh", ID_INT, 1u) == -1);
    {   // a UTF-8 multibyte sequence is not ASCII; rejected
        const char utf8[] = { 'b', (char) 0xC3, (char) 0xA9, '\0' };
        CHECK(SymbolTable_instant(v, utf8, ID_INT, 1u) == -1);
    }
    {   // 24 chars exceeds STRING_POOL_NAME_MAX (23)
        const char tooLong[] = "aaaaaaaaaaaaaaaaaaaaaaaa"; // 24
        CHECK(SymbolTable_instant(v, tooLong, ID_INT, 1u) == -1);
    }
    {   // exactly 23 chars is accepted
        const char maxOk[] = "aaaaaaaaaaaaaaaaaaaaaaa"; // 23
        CHECK(SymbolTable_instant(v, maxOk, ID_INT, 1u) >= 0);
    }
    // Underscore and dot are legal.
    CHECK(SymbolTable_instant(v, "a_b.c9", ID_INT, 1u) >= 0);
    // Mixed case folds to lowercase on the way in.
    int32_t id = SymbolTable_instant(v, "MixedCase_1.x", ID_INT, 1u);
    CHECK(id >= 0);
    CHECK(SymbolTable_getId(v, "mixedcase_1.x") == id);

    // Failures never allocated rows.
    CHECK(SymbolTable_getActiveCount(v) == before + 3);
}

// Checks successful rename identity preservation and rejection-state stability.
static void test_rename(SymbolTable *v) {
    int32_t id = SymbolTable_instant(v, "ren_old", ID_INT, 7u);
    CHECK(id >= 0);

    CHECK(SymbolTable_rename(v, "ren_old", "ren_new"));
    CHECK(SymbolTable_getId(v, "ren_old") == -1);
    CHECK(SymbolTable_getId(v, "ren_new") == id);
    CHECK(SymbolTable_getPointer(v, id) == 7u);         // payload follows
    CHECK(SymbolTable_getClassId(v, id) == ID_INT);

    // Unknown old name.
    CHECK(!SymbolTable_rename(v, "ren_ghost", "ren_x"));
    // Collision with a live name (alpha is registered).
    CHECK(!SymbolTable_rename(v, "ren_new", "alpha"));
    // Bad new name.
    CHECK(!SymbolTable_rename(v, "ren_new", "bad name"));
    CHECK(SymbolTable_getId(v, "ren_new") == id);       // unchanged after failures

    // Rename to another fresh name works.
    CHECK(SymbolTable_rename(v, "ren_new", "ren_other"));
    CHECK(SymbolTable_getId(v, "ren_other") == id);
}

// Checks class filtering order, count-only/short output buffers, and no-match behavior.
static void test_find_by_class(SymbolTable *v) {
    const uint32_t cls = 0xABCDEF01u;                   // unique to this test
    int32_t a = SymbolTable_instant(v, "fbc_a", cls, 1u);
    int32_t b = SymbolTable_instant(v, "fbc_b", cls, 2u);
    int32_t c = SymbolTable_instant(v, "fbc_c", cls, 3u);
    CHECK(a >= 0 && b >= 0 && c >= 0);
    CHECK(SymbolTable_instant(v, "fbc_other", 0xABCDEF02u, 4u) >= 0);

    int32_t out[8] = { -1 };
    size_t total = SymbolTable_findByClass(v, cls, out, 8);
    CHECK(total == 3);
    CHECK(out[0] == a && out[1] == b && out[2] == c);   // insertion order

    // A short buffer still reports the true total, filling only what fits.
    int32_t one[1] = { -1 };
    total = SymbolTable_findByClass(v, cls, one, 1);
    CHECK(total == 3);
    CHECK(one[0] == a);

    // Zero-cap / null out: count only, no writes.
    CHECK(SymbolTable_findByClass(v, cls, nullptr, 8) == 3);
    CHECK(SymbolTable_findByClass(v, cls, out, 0) == 3);

    // No matches.
    CHECK(SymbolTable_findByClass(v, 0xDEADBEEFu, out, 8) == 0);
}

// Checks pointer get/set/CAS semantics, including mismatch preservation and invalid ids.
static void test_pointer_accessors(SymbolTable *v) {
    int32_t id = SymbolTable_instant(v, "pa_x", 0x11u, 100u);
    CHECK(id >= 0);
    CHECK(SymbolTable_getPointer(v, id) == 100u);

    SymbolTable_setPointer(v, id, 200u);
    CHECK(SymbolTable_getPointer(v, id) == 200u);

    // CAS succeeds only when the expectation matches.
    CHECK(SymbolTable_compareAndSetPointer(v, id, 200u, 300u));
    CHECK(SymbolTable_getPointer(v, id) == 300u);
    CHECK(!SymbolTable_compareAndSetPointer(v, id, 999u, 400u));
    CHECK(SymbolTable_getPointer(v, id) == 300u);       // untouched on failure

    CHECK(SymbolTable_getClassId(v, id) == 0x11u);

    // Invalid ids: silent safe defaults.
    CHECK(SymbolTable_getPointer(v, -1) == 0u);
    CHECK(SymbolTable_getPointer(v, 99999) == 0u);
    CHECK(SymbolTable_getClassId(v, -1) == 0u);
    SymbolTable_setPointer(v, -1, 1u);                  // no-op
    CHECK(!SymbolTable_compareAndSetPointer(v, -1, 0u, 1u));
}

// Checks direct name return and invalid-id handling.
static void test_get_name(SymbolTable *v) {
    int32_t id = SymbolTable_instant(v, "gn_name", ID_INT, 1u);
    CHECK(id >= 0);

    CHECK(strcmp(SymbolTable_getName(v, id), "gn_name") == 0);
    CHECK(SymbolTable_getName(v, -1) == nullptr);
    CHECK(SymbolTable_getName(v, 99999) == nullptr);
}

// Exercises null arguments and zero-initialized inactive tables across the public surface.
static void test_null_and_inactive(void) {
    // nullptr everywhere.
    SymbolTable constructed = SymbolTable();
    CHECK(constructed.active);
    SymbolTable_shutdown(&constructed);
    SymbolTable_shutdown(nullptr);
    CHECK(SymbolTable_instant(nullptr, "x", ID_INT, 1u) == -1);
    CHECK(SymbolTable_getId(nullptr, "x") == -1);
    CHECK(!SymbolTable_rename(nullptr, "a", "b.sh"));
    CHECK(SymbolTable_findByClass(nullptr, ID_INT, nullptr, 0) == 0);
    CHECK(SymbolTable_getPointer(nullptr, 0) == 0u);
    SymbolTable_setPointer(nullptr, 0, 1u);
    CHECK(!SymbolTable_compareAndSetPointer(nullptr, 0, 0u, 1u));
    CHECK(SymbolTable_getClassId(nullptr, 0) == 0u);
    CHECK(SymbolTable_getName(nullptr, 0) == nullptr);
    CHECK(SymbolTable_getActiveCount(nullptr) == 0);

    // A zero-initialized (inactive) table is equally safe.
    SymbolTable dead;
    memset(&dead, 0, sizeof(dead));
    CHECK(SymbolTable_instant(&dead, "x", ID_INT, 1u) == -1);
    CHECK(SymbolTable_getId(&dead, "x") == -1);
    CHECK(SymbolTable_getActiveCount(&dead) == 0);
    SymbolTable_shutdown(&dead);                        // no-op
}

// Captures stderr to assert each rejected cold admission/rename emits one diagnostic.
static void test_observability(SymbolTable *v) {
    FILE *capture = tmpfile();
    CHECK(capture != nullptr);
    int original = dup(fileno(stderr));
    CHECK(original >= 0);
    CHECK(dup2(fileno(capture), fileno(stderr)) >= 0);

    CHECK(SymbolTable_instant(v, nullptr, ID_INT, 1u) == -1);   // empty/null name
    CHECK(SymbolTable_instant(v, "", ID_INT, 1u) == -1);
    CHECK(SymbolTable_instant(v, "bad name", ID_INT, 1u) == -1);
    CHECK(SymbolTable_instant(v, "aaaaaaaaaaaaaaaaaaaaaaaa", ID_INT, 1u) == -1);
    CHECK(!SymbolTable_rename(v, "ghost", "ghost2"));
    CHECK(!SymbolTable_rename(v, "alpha", "also bad"));

    fflush(stderr);
    CHECK(dup2(original, fileno(stderr)) >= 0);
    close(original);
    rewind(capture);

    int lines = 0;
    char line[512];
    while (fgets(line, sizeof(line), capture) != nullptr) {
        CHECK(strstr(line, "[variable]") != nullptr);
        lines++;
    }
    CHECK(lines == 6);                                  // one rejection, one line
    fclose(capture);
}

// Checks active-entry removal and safe repeated shutdown on an initialized table.
static void test_shutdown(void) {
    SymbolTable v = SymbolTable();
    CHECK(v.active);
    CHECK(SymbolTable_instant(&v, "sd_x", ID_INT, 1u) >= 0);
    CHECK(SymbolTable_getActiveCount(&v) == 1);

    SymbolTable_shutdown(&v);
    CHECK(SymbolTable_getActiveCount(&v) == 0);
    CHECK(SymbolTable_getId(&v, "sd_x") == -1);
    CHECK(SymbolTable_getPointer(&v, 0) == 0u);
    SymbolTable_shutdown(&v);                           // safe to call twice
}

// Initializes memory and the table, runs isolated contract cases, and aggregates failures.
int main(void) {
    CHECK(Memory_init(0));

    SymbolTable v = SymbolTable();
    CHECK(v.active);

    test_instant_and_lookup(&v);
    test_name_policy(&v);
    test_rename(&v);
    test_find_by_class(&v);
    test_pointer_accessors(&v);
    test_get_name(&v);
    test_observability(&v);
    SymbolTable_shutdown(&v);

    test_null_and_inactive();
    test_shutdown();

    if (g_failures == 0) {
        printf("symbol_table_test: all assertions held\n");
        return 0;
    }
    printf("symbol_table_test: %d FAILURES\n", g_failures);
    return 1;
}
