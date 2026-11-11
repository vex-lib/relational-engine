#include "lang/annotation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vex.h"

#include "substrate/relational/symbol_table.h"
#include "substrate/substrate.h"
#include "nio/mem.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: VariableStrictTest (variable_strict_test/test.c — LOCAL ONLY)
 * LEVEL: L3 — Module Code (headless verification harness)
 * ============================================================================
 * Local proof for the pool-backed SymbolTable: strict create-or-fail,
 * edge-case rejections, dotted names, class filtering, rename flows.
 * Never committed (repo tests/ policy) — run by hand after surgery.
 *
 * STRUCT FIELDS: none — procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main(void)
 * ============================================================================
 */

static int g_failures = 0;

#define CHECK(name, cond) do { \
    if (cond) { printf("[variable_strict_test] PASS %s\n", name); } \
    else { printf("[variable_strict_test] FAIL %s\n", name); g_failures++; } \
} while (0)

// Exercises strict SymbolTable rejection, class filtering, rename, and relational rebinding flows.
int main(void) {
    printf("=== Running SymbolTable Strict Test Suite ===\n");
    if (!Memory_init(0u)) return 1;
    SymbolTable scope = SymbolTable();
    CHECK("constructor", scope.active);

    i32 a = SymbolTable_instant(&scope, "hp", 1, (pointer) 0x100);
    CHECK("first instant", a >= 0);
    i32 dup = SymbolTable_instant(&scope, "hp", 1, (pointer) 0x200);
    CHECK("dup fails", dup == -1);
    CHECK("dup keeps old value", SymbolTable_getPointer(&scope, a) == (pointer) 0x100);

    CHECK("empty fails", SymbolTable_instant(&scope, "", 1, 1) == -1);
    CHECK("null fails", SymbolTable_instant(&scope, nullptr, 1, 1) == -1);
    CHECK("overlong fails", SymbolTable_instant(&scope, "abcdefghijklmnopqrstuvwxyz", 1, 1) == -1);
    CHECK("space fails", SymbolTable_instant(&scope, "has space", 1, 1) == -1);
    CHECK("utf8 fails", SymbolTable_instant(&scope, "caf\xc3\xa9", 1, 1) == -1);
    CHECK("dash fails", SymbolTable_instant(&scope, "has-dash", 1, 1) == -1);
    CHECK("getId bad silent", SymbolTable_getId(&scope, "has space") == -1);

    i32 f = SymbolTable_instant(&scope, "character.position.x", 2, (pointer) 0x300);
    CHECK("dotted instant", f >= 0);
    CHECK("dotted find", SymbolTable_getId(&scope, "character.position.x") == f);
    CHECK("case folds", SymbolTable_getId(&scope, "Character.Position.X") == f);
    CHECK("getName dotted", strcmp(SymbolTable_getName(&scope, f), "character.position.x") == 0);

    SymbolTable_instant(&scope, "mp", 1, (pointer) 0x400);
    SymbolTable_instant(&scope, "name", 3, (pointer) 0x500);
    i32 ids[8];
    CHECK("class1 total", SymbolTable_findByClass(&scope, 1, ids, 8) == 2);
    CHECK("class2 total", SymbolTable_findByClass(&scope, 2, ids, 8) == 1);
    CHECK("class9 total", SymbolTable_findByClass(&scope, 9, ids, 8) == 0);
    i32 one[1];
    CHECK("truncated total visible", SymbolTable_findByClass(&scope, 1, one, 1) == 2);
    CHECK("class pinned", SymbolTable_getClassId(&scope, a) == 1);
    SymbolTable_setPointer(&scope, a, (pointer) 0x111);
    CHECK("setPointer keeps class", SymbolTable_getClassId(&scope, a) == 1 && SymbolTable_getPointer(&scope, a) == (pointer) 0x111);
    CHECK("cas ok", SymbolTable_compareAndSetPointer(&scope, a, (pointer) 0x111, (pointer) 0x222) == true);
    CHECK("cas mismatch", SymbolTable_compareAndSetPointer(&scope, a, (pointer) 0x111, (pointer) 0x333) == false);

    CHECK("rename ok", SymbolTable_rename(&scope, "hp", "health") == true);
    CHECK("old gone", SymbolTable_getId(&scope, "hp") == -1);
    CHECK("new resolves", SymbolTable_getId(&scope, "health") == a);
    CHECK("rename collision", SymbolTable_rename(&scope, "health", "mp") == false);
    CHECK("rename unknown", SymbolTable_rename(&scope, "nope", "alsono") == false);
    CHECK("rename bad new", SymbolTable_rename(&scope, "health", "has space") == false);

    CHECK("setValue create", Relational_setValue(&scope, "score", 4, (void*) 0x600) == true);
    CHECK("setValue rebind", Relational_setValue(&scope, "score", 4, (void*) 0x601) == true);
    CHECK("rebound value", (pointer) Relational_getValue(&scope, "score") == (pointer) 0x601);
    CHECK("setFunction create", Relational_setFunction(&scope, "tick", (void*) 0x700) == true);
    CHECK("setFunction rebind", Relational_setFunction(&scope, "tick", (void*) 0x701) == true);
    Relational_setString(&scope, "title", "hello");
    CHECK("setString create", Relational_getString(&scope, "title") != nullptr &&
        strcmp(Relational_getString(&scope, "title"), "hello") == 0);
    Relational_setString(&scope, "title", "world");
    CHECK("setString replace", strcmp(Relational_getString(&scope, "title"), "world") == 0);

    CHECK("active count", SymbolTable_getActiveCount(&scope) == 7);
    SymbolTable_shutdown(&scope);
    SymbolTable_shutdown(&scope);

    printf("\n=== SymbolTable Strict Test Summary: %d failures ===\n", g_failures);
    return g_failures > 0 ? 1 : 0;
}
