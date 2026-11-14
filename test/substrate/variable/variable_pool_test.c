#include "vex.h"
#include "lang/annotation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"
#include "substrate/variable/variable_pool.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: VariablePoolTest (variable_pool_test/test.c)
 * LEVEL: L3 — Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the static interned string pool: pre-init fail-closed
 * behavior, rejection policy, dedup, self-link validity, sorted lookup,
 * growth with relink, and shutdown semantics. Sections re-init the pool for
 * hermetic state (one process-wide table).
 *
 * STRUCT FIELDS: none — procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static int g_failures = 0;

#define CHECK(name, cond) do { \
    if (cond) { printf("[variable_pool_test] PASS %s\n", name); } \
    else { printf("[variable_pool_test] FAIL %s\n", name); g_failures++; } \
} while (0)

// Exercises static StringPool lifecycle, validation, deduplication, growth, and relinking.
int main() {
    printf("=== Running Variable Pool Test Suite ===\n");

    {
        CHECK("pre-init intern closed", StringPool_intern("label") == -1);
        CHECK("pre-init find closed", StringPool_find("label") == -1);
        CHECK("pre-init slot closed", StringPool_slot(0) == nullptr);
        CHECK("pre-init name closed", StringPool_name(0) == nullptr);
        CHECK("pre-init count closed", StringPool_count() == 0);
        CHECK("pre-init isSlot closed", StringPool_isSlot("label") == false);
        StringPool_shutdown();
        MemoryArena *arena = MemoryArena_create(64u << 20);
        CHECK("arena created", arena != nullptr);
        CHECK("init null arena", StringPool_init(nullptr) == false);
        CHECK("init ok", StringPool_init(arena) == true);
        CHECK("re-init succeeds shared", StringPool_init(arena) == true);
        CHECK("intern null", StringPool_intern(nullptr) == -1);
        CHECK("intern empty", StringPool_intern("") == -1);
        CHECK("intern 24-char rejected", StringPool_intern("abcdefghijklmnopqrstuvwx") == -1);
        CHECK("find null", StringPool_find(nullptr) == -1);
        CHECK("find absent", StringPool_find("nope") == -1);
        CHECK("slot OOB", StringPool_slot(99) == nullptr);
        CHECK("name OOB", StringPool_name(99) == nullptr);
        CHECK("isSlot null", StringPool_isSlot(nullptr) == false);
        StringPool_shutdown();
        StringPool_shutdown();
        CHECK("shutdown twice count", StringPool_count() == 0);
        CHECK("post-shutdown intern closed", StringPool_intern("label") == -1);
        MemoryArena_destroy(arena);
    }

    {
        MemoryArena *arena = MemoryArena_create(64u << 20);
        StringPool_init(arena);
        i32 a = StringPool_intern("label");
        i32 b = StringPool_intern("label");
        CHECK("intern dedup", a >= 0 && a == b);
        CHECK("count one", StringPool_count() == 1);
        CHECK("find hit", StringPool_find("label") == a);
        const char *edge23 = "abcdefghijklmnopqrstuvw";
        CHECK("23-char edge length", strlen(edge23) == 23);
        i32 e = StringPool_intern(edge23);
        CHECK("23-char accepted", e >= 0);
        CHECK("name round-trip", strcmp(StringPool_name((u32) e), edge23) == 0);
        const StringSlot *slot = StringPool_slot((u32) a);
        CHECK("slot self link", slot != nullptr && (*slot).self == (u64) (pointer) slot);
        CHECK("isSlot live", StringPool_isSlot(slot) == true);
        char nearby[32];
        memset(nearby, 0, sizeof(nearby));
        CHECK("isSlot foreign", StringPool_isSlot(nearby) == false);
        StringPool_shutdown();
        CHECK("isSlot after shutdown", StringPool_isSlot(slot) == false);
        MemoryArena_destroy(arena);
    }

    {
        MemoryArena *arena = MemoryArena_create(64u << 20);
        StringPool_init(arena);
        const char *words[] = { "delta", "alpha", "charlie", "bravo" };
        for (int i = 0; i < 4; i++)
            StringPool_intern(words[i]);
        CHECK("count four", StringPool_count() == 4);
        bool allFound = true;
        for (int i = 0; i < 4; i++) {
            i32 idx = StringPool_find(words[i]);
            if (idx < 0 || strcmp(StringPool_name((u32) idx), words[i]) != 0)
                allFound = false;
        }
        CHECK("all found by name", allFound);
        char buf[8];
        for (int i = 0; i < 200; i++) {
            snprintf(buf, sizeof(buf), "k%d", i);
            if (StringPool_intern(buf) < 0)
                break;
        }
        CHECK("growth past capacity", StringPool_count() == 204);
        CHECK("early name survives grow", StringPool_find("alpha") >= 0);
        const StringSlot *s = StringPool_slot((u32) StringPool_find("alpha"));
        CHECK("self relinked after grow", s != nullptr && StringPool_isSlot(s) == true);
        StringPool_shutdown();
        MemoryArena_destroy(arena);
    }

    printf("\n=== Variable Pool Test Summary: %d failures ===\n", g_failures);
    return g_failures > 0 ? 1 : 0;
}
