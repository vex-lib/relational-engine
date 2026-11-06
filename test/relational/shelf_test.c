#include "util/annotation.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "relational/cell.h"
#include "relational/shelf.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: ShelfTest (shelf_test/test.c)
 * LEVEL: L3 — Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the identity cell and the global shelf: the 32-byte cell
 * layout + header identity, Cell_check, and the shelf's never-moved node pool
 * with u32 graph edges (index stability across growth, link walk, null-safety,
 * both string projections).
 *
 * STRUCT FIELDS: none — procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main(void)
 * ============================================================================
 */

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(cond)) {                                                    \
            g_fail++;                                                     \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);              \
        }                                                                 \
    } while (0)

// Checks Cell layout, header identity, value storage, and null-safe destruction.
static void testCell(void) {
    printf("[1] identity cell: 32 B block, header identity, value slot\n");

    CHECK(sizeof(Cell) == 16u);

    Cell *c = Cell_2(TYPE_VARIABLE_SLOT, 0xDEADu);
    CHECK(c != nullptr);
    if (c) {
        CHECK(Cell_typeId(c) == TYPE_VARIABLE_SLOT);
        CHECK(Cell_check(c, TYPE_VARIABLE_SLOT) == true);
        CHECK(Cell_check(c, TYPE_SHELF) == false);
        CHECK(Cell_getValue(c) == 0xDEADu);
        CHECK(Memory_length(c) == CELL_PAYLOAD_BYTES); // the 16 B payload
        Cell_setValue(c, 0xBEEFu);
        CHECK(Cell_getValue(c) == 0xBEEFu);
        Cell_free(c);
    }

    Cell *anon = Cell_0();
    CHECK(anon != nullptr && Cell_typeId(anon) == 0u);
    Cell_free(anon);

    Cell *one = Cell_1(TYPE_SHELF);
    CHECK(one != nullptr && Cell_getValue(one) == 0u && Cell_check(one, TYPE_SHELF));
    Cell_free(one);
    Cell_free(nullptr);
    CHECK(true);
}

// Checks Cell value and structure projections, including truncation and nullptr rendering.
static void testCellStrings(void) {
    printf("[2] cell string projections\n");

    Cell *c = Cell_2(0x2Cu, 0x1Fu);
    char buf[96];
    bool truncated = true;
    Cell_toString(c, buf, sizeof(buf), &truncated);
    CHECK(truncated == false && strcmp(buf, "Cell(typeId=0x2c, value=0x1f)") == 0);
    Cell_toStringStruct(c, buf, sizeof(buf), &truncated);
    CHECK(truncated == false && strcmp(buf, "Cell { value=0x1f, pad=0x0 }") == 0);

    char tiny[4];
    Cell_toString(c, tiny, sizeof(tiny), &truncated);
    CHECK(truncated == true);
    truncated = true;
    Cell_toString(nullptr, buf, sizeof(buf), &truncated);
    CHECK(truncated == false && strcmp(buf, "nullptr") == 0);
    Cell_free(c);
}

// Exercises shelf insertion, linked traversal, invalid indices, and head updates.
static void testShelfBasics(void) {
    printf("[3] shelf: node pool, cells, u32 edges, head walk\n");

    Shelf *shelf = Shelf_0();
    CHECK(shelf != nullptr);
    if (!shelf)
        return;
    CHECK(Memory_type(shelf) == TYPE_SHELF);
    CHECK(Shelf_count(shelf) == 0u);
    CHECK(Shelf_isEmpty(shelf) == true);
    CHECK(Shelf_getHead(shelf) == SHELF_INDEX_NONE);

    uint32_t a = Shelf_addCell(shelf, TYPE_VARIABLE_SLOT, 0x10u);
    uint32_t b = Shelf_addCell(shelf, TYPE_SHELF, 0x20u);
    uint32_t cc = Shelf_addCell(shelf, 0x1234u, 0x30u);
    CHECK(a == 0u && b == 1u && cc == 2u);
    CHECK(Shelf_count(shelf) == 3u);
    CHECK(Shelf_isEmpty(shelf) == false);

    Cell *ca = (Cell*) Shelf_getCell(shelf, a);
    Cell *cb = (Cell*) Shelf_getCell(shelf, b);
    Cell *c3 = (Cell*) Shelf_getCell(shelf, cc);
    CHECK(Cell_check(ca, TYPE_VARIABLE_SLOT) == true && Cell_getValue(ca) == 0x10u);
    CHECK(Cell_check(cb, TYPE_SHELF) == true && Cell_getValue(cb) == 0x20u);
    CHECK(Cell_check(c3, 0x1234u) == true && Cell_getValue(c3) == 0x30u);

    // Link 0 -> 1 -> 2 -> end.
    CHECK(Shelf_setHead(shelf, a) == true);
    CHECK(Shelf_link(shelf, a, b) == true);
    CHECK(Shelf_link(shelf, b, cc) == true);
    CHECK(Shelf_getNext(shelf, cc) == SHELF_INDEX_NONE);

    // Walk the chain: exactly 0,1,2.
    uint32_t order[4] = { 0u, 0u, 0u, 0u };
    uint32_t n = 0u;
    for (uint32_t i = Shelf_getHead(shelf); i != SHELF_INDEX_NONE && n < 4u; i = Shelf_getNext(shelf, i))
        order[n++] = i;
    CHECK(n == 3u && order[0] == 0u && order[1] == 1u && order[2] == 2u);

    CHECK(Shelf_getCell(shelf, 99u) == 0u);
    CHECK(Shelf_getNext(shelf, 99u) == SHELF_INDEX_NONE);
    CHECK(Shelf_setNext(shelf, 99u, 0u) == false);
    CHECK(Shelf_link(shelf, 99u, 0u) == false);
    CHECK(Shelf_setHead(shelf, 99u) == false);
    CHECK(Shelf_setHead(shelf, SHELF_INDEX_NONE) == true);
    CHECK(Shelf_getHead(shelf) == SHELF_INDEX_NONE);

    Shelf_free(shelf);
}

// Verifies cell addresses and values remain stable as the shelf grows to 500 entries.
static void testShelfStability(void) {
    printf("[4] node/cell addresses stay stable across growth\n");

    Shelf *shelf = Shelf_0();
    if (!shelf) {
        CHECK(false);
        return;
    }

    uint32_t first = Shelf_addCell(shelf, TYPE_VARIABLE_SLOT, 0x0u);
    uintptr_t firstCell = Shelf_getCell(shelf, first);
    CHECK(firstCell != 0u);

    // Grow well past the leaf and root boundaries (default 8 rows/leaf).
    for (uint32_t i = 1u; i < 500u; i++)
        CHECK(Shelf_addCell(shelf, TYPE_VARIABLE_SLOT, i) == i);
    CHECK(Shelf_count(shelf) == 500u);

    CHECK(Shelf_getCell(shelf, first) == firstCell); // node 0's cell never moved
    bool intact = true;
    for (uint32_t i = 0u; i < 500u; i++) {
        Cell *c = (Cell*) Shelf_getCell(shelf, i);
        if (!c || Cell_getValue(c) != i) {
            intact = false;
            break;
        }
    }
    CHECK(intact);

    Shelf_free(shelf);
}

// Checks Shelf null-safe operations, lifecycle idempotence, and bounded projections.
static void testShelfNullAndStrings(void) {
    printf("[5] shelf null-safety + string projections\n");

    CHECK(Shelf_count(nullptr) == 0u);
    CHECK(Shelf_isEmpty(nullptr) == true);
    CHECK(Shelf_getHead(nullptr) == SHELF_INDEX_NONE);
    CHECK(Shelf_getCell(nullptr, 0u) == 0u);
    CHECK(Shelf_getNext(nullptr, 0u) == SHELF_INDEX_NONE);
    CHECK(Shelf_setNext(nullptr, 0u, 0u) == false);
    CHECK(Shelf_setHead(nullptr, 0u) == false);
    CHECK(Shelf_addNode(nullptr, 0u) == SHELF_INDEX_NONE);
    CHECK(Shelf_addCell(nullptr, 0u, 0u) == SHELF_INDEX_NONE);
    CHECK(Shelf_init(nullptr) == false);
    Shelf_shutdown(nullptr);
    Shelf_free(nullptr);

    Shelf shelf;
    CHECK(Shelf_init(&shelf) == true);
    Shelf_addCell(&shelf, TYPE_VARIABLE_SLOT, 0x1u);
    char buf[128];
    bool truncated = true;
    Shelf_toString(&shelf, buf, sizeof(buf), &truncated);
    CHECK(truncated == false && strcmp(buf, "Shelf(count=1)") == 0);
    Shelf_toStringStruct(&shelf, buf, sizeof(buf), &truncated);
    const char *prefix = "Shelf { active=true, count=1, head=-1, nodes=0x";
    CHECK(truncated == false && strncmp(buf, prefix, strlen(prefix)) == 0);

    char tiny[4];
    Shelf_toString(&shelf, tiny, sizeof(tiny), &truncated);
    CHECK(truncated == true);
    truncated = true;
    Shelf_toString(nullptr, buf, sizeof(buf), &truncated);
    CHECK(truncated == false && strcmp(buf, "nullptr") == 0);

    Shelf_shutdown(&shelf);
    CHECK(Shelf_count(&shelf) == 0u);
    Shelf_shutdown(&shelf); // idempotent
    CHECK(true);
}

// Runs Cell and Shelf identity, collection, growth, projection, and null-safety checks.
int main(void) {
    printf("=== Cell + Shelf Test Suite ===\n\n");

    testCell();
    testCellStrings();
    testShelfBasics();
    testShelfStability();
    testShelfNullAndStrings();

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0)
        printf("=== Cell + Shelf Test Suite: ALL PASS ===\n");
    return g_fail == 0 ? 0 : 1;
}
