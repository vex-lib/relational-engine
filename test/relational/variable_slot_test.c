#include "annotation/annotation.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "relational/variable_slot.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: VariableSlotTest (variable_slot_test/test.c)
 * LEVEL: L3 — Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the 32-byte name box (the reflection atom): layout, the
 * name charset gate (fold to lowercase; reject empty/overlong/illegal incl. the
 * dot splitter), round-trip getters, the pointer binding, the arity
 * constructors, null-safety, and both bounded string projections with the
 * truncation flag (the Cold-Strict, Hot-Minimal Validation Law).
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
    if (cond) { printf("[variable_slot_test] PASS %s\n", name); } \
    else { printf("[variable_slot_test] FAIL %s\n", name); g_failures++; } \
} while (0)

// Checks VariableSlot layout, name grammar/folding, accessors, constructors, strings, and null safety.
int main(void) {
    printf("=== Running Variable Slot Test Suite ===\n");

    // section 1 Layout — the atom is exactly 32 Bytes (the 24-Byte Variable Slot Law).
    CHECK("sizeof slot is 32", sizeof(VariableSlot) == 32u);

    // section 2 Inline init — validate + fold + bind.
    {
        VariableSlot slot;
        memset(&slot, 0, sizeof(slot));
        CHECK("init ok", VariableSlot_init(&slot, "Health", 0xABCDu) == true);
        CHECK("pointer bound", VariableSlot_getPointer(&slot) == 0xABCDu);
        CHECK("name folded to lowercase", strcmp(VariableSlot_getName(&slot), "health") == 0);
        CHECK("not empty", VariableSlot_isEmpty(&slot) == false);
        CHECK("nameEquals case-insensitive", VariableSlot_nameEquals(&slot, "HEALTH") == true);
        CHECK("nameEquals differs", VariableSlot_nameEquals(&slot, "health_ui") == false);
    }

    // section 3 Camel hump fold — helloWorld -> helloworld (hashed as lowercase).
    {
        VariableSlot slot;
        CHECK("camel fold ok", VariableSlot_init(&slot, "helloWorld", 0) == true);
        CHECK("camel folded", strcmp(VariableSlot_getName(&slot), "helloworld") == 0);
    }

    // section 4 Name grammar — segments split by '.', empty segments rejected.
    {
        VariableSlot slot;
        CHECK("init null name", VariableSlot_init(&slot, nullptr, 0) == false);
        CHECK("init empty name", VariableSlot_init(&slot, "", 0) == false);
        CHECK("init 24-char name", VariableSlot_init(&slot, "abcdefghijklmnopqrstuvwx", 0) == false);
        CHECK("init 23-char ok", VariableSlot_init(&slot, "abcdefghijklmnopqrstuvw", 0) == true);
        CHECK("init dotted ok", VariableSlot_init(&slot, "character.position.x", 0) == true);
        CHECK("init leading dot rejected", VariableSlot_init(&slot, ".a", 0) == false);
        CHECK("init trailing dot rejected", VariableSlot_init(&slot, "a.", 0) == false);
        CHECK("init double dot rejected", VariableSlot_init(&slot, "a..b.sh", 0) == false);
        CHECK("init space rejected", VariableSlot_init(&slot, "a b.sh", 0) == false);
        CHECK("init slash rejected", VariableSlot_init(&slot, "a/b.sh", 0) == false);
        CHECK("init dollar ok", VariableSlot_init(&slot, "a$b.sh", 0) == true);
        CHECK("init dash ok", VariableSlot_init(&slot, "a-b.sh", 0) == true);
        CHECK("init null slot", VariableSlot_init(nullptr, "x", 0) == false);
    }

    // section 4b Dotted names fold per segment and compare whole.
    {
        VariableSlot dotted;
        VariableSlot_init(&dotted, "Character.Position.X", 0);
        CHECK("dotted folded", strcmp(VariableSlot_getName(&dotted), "character.position.x") == 0);
        CHECK("dotted nameEquals", VariableSlot_nameEquals(&dotted, "CHARACTER.position.x") == true);
    }

    // section 5 Setters — good name swaps, bad name leaves the old one.
    {
        VariableSlot slot;
        VariableSlot_init(&slot, "first", 1);
        CHECK("setName bad rejected", VariableSlot_setName(&slot, "bad name") == false);
        CHECK("old name survives bad set", strcmp(VariableSlot_getName(&slot), "first") == 0);
        CHECK("setName ok", VariableSlot_setName(&slot, "Second") == true);
        CHECK("name replaced + folded", strcmp(VariableSlot_getName(&slot), "second") == 0);
        VariableSlot_setPointer(&slot, 0x99u);
        CHECK("pointer replaced", VariableSlot_getPointer(&slot) == 0x99u);
    }

    // section 6 Null safety (the Symmetric Getter/Setter Completeness Law).
    {
        CHECK("null getName", VariableSlot_getName(nullptr) == nullptr);
        CHECK("null getPointer", VariableSlot_getPointer(nullptr) == 0u);
        CHECK("null isEmpty", VariableSlot_isEmpty(nullptr) == true);
        CHECK("null nameEquals", VariableSlot_nameEquals(nullptr, "x") == false);
        VariableSlot_setName(nullptr, "x");
        VariableSlot_setPointer(nullptr, 1);
        CHECK("null setters no-op", true);
    }

    // section 8 Arity constructors — arena-allocated slots.
    {
        VariableSlot *empty = VariableSlot();
        CHECK("arity 0 allocates", empty != nullptr && VariableSlot_isEmpty(empty) == true);
        VariableSlot *named = VariableSlot("player");
        CHECK("arity 1 names", named != nullptr && VariableSlot_nameEquals(named, "player") == true);
        VariableSlot *bound = VariableSlot("score", 0x42u);
        CHECK("arity 2 binds", bound != nullptr && VariableSlot_getPointer(bound) == 0x42u);
        CHECK("allocated typed", bound != nullptr && Memory_type(bound) == TYPE_VARIABLE_SLOT);
        VariableSlot *bad = VariableSlot("no good");
        CHECK("arity rejects bad name", bad == nullptr);
        VariableSlot_free(empty);
        VariableSlot_free(named);
        VariableSlot_free(bound);
        VariableSlot_free(nullptr);
        CHECK("free null-safe", true);
    }

    // section 9 String projections — bounded, dest-last, truncation flagged.
    {
        VariableSlot slot;
        VariableSlot_init(&slot, "health", 0x1Fu);
        char buf[96];
        bool truncated = true;
        VariableSlot_toString(&slot, buf, sizeof(buf), &truncated);
        CHECK("toString fits", truncated == false && strcmp(buf, "VariableSlot(health => 0x1f)") == 0);
        VariableSlot_toStringStruct(&slot, buf, sizeof(buf), &truncated);
        CHECK("toStringStruct fits", truncated == false &&
              strcmp(buf, "VariableSlot { name=\"health\", pointer=0x1f }") == 0);

        char tiny[4];
        VariableSlot_toString(&slot, tiny, sizeof(tiny), &truncated);
        CHECK("toString truncated flagged", truncated == true);
        VariableSlot_toStringStruct(&slot, tiny, sizeof(tiny), &truncated);
        CHECK("toStringStruct truncated flagged", truncated == true);

        truncated = true;
        VariableSlot_toString(nullptr, buf, sizeof(buf), &truncated);
        CHECK("toString null renders nullptr", truncated == false && strcmp(buf, "nullptr") == 0);
    }

    printf("=== Variable Slot: %d failure(s) ===\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
