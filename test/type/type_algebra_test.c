// relational-engine/test/type/type_algebra_test.c — the R2 type-algebra owner test.
// (Per-File Battle Test Law.) type/type.c owns the project-agnostic id algebra
// and the parent-chain resolver, so the battle rows are:
//   - VALUE BOUNDARY: each id field (form / project / class / sugar) isolates;
//   - ADVERSARIAL: registration rejects zero/foreign project Bytes, nullptr
//     with a non-zero count, out-of-range parents, self-parents and CYCLES;
//   - FAILURE ATOMICITY: every rejection preserves the previous table;
//   - BOUNDED PROGRESS: a cyclic table can never be registered, so the
//     Type_isA walk cannot spin;
//   - CROSS-PROJECT: the documented project-invariant ancestor match is pinned;
//   - GROWTH: the slate doubles under many projects.
//
// This owner compiles against the ALGEBRA ONLY (no project registry), proving
// the moved file carries no per-project class numbers.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../type/type.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// A project byte with no stray bits: the field occupies bits 52..59.
static uint64_t projByte(uint64_t byte) {
    return (byte << 52) & MASK_PROJECT;
}

int main(void) {
    // --- Bit field isolation: make then read each field back exactly.
    uint64_t id = Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u);
    CHECK(Type_class(id) == 1u);
    CHECK(Type_form(id) == FORM_SINGLETON);
    CHECK(Type_project(id) == PROJ_VEXSPOKE);
    CHECK(Type_class(0xFFFFFFFFull) == 0x00FFFFFFu);         // class keeps 24 bits
    CHECK((SUGAR_VEX & MASK_SUGAR) == SUGAR_VEX);            // reserved "векс" slot
    CHECK(Type_form(FORM_STRUCT_ARRAY) == FORM_STRUCT_ARRAY);

    // --- Form predicates: one true, the disjoint neighbours false.
    CHECK(Type_isSingleton(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u)));
    CHECK(!Type_isArray(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u)));
    CHECK(Type_isArray(Type_make(PROJ_VEXSPOKE, FORM_ARRAY, 1u)));
    CHECK(Type_isPointer(Type_make(PROJ_VEXSPOKE, FORM_POINTER, 1u)));
    CHECK(Type_isStruct(FORM_STRUCT_SINGLETON));
    CHECK(!Type_isStruct(0));
    CHECK(!Type_isStruct(FORM_SINGLETON));
    CHECK(Type_isStructSingleton(Type_make(PROJ_VEXSPOKE, FORM_STRUCT_SINGLETON, 1u)));
    CHECK(Type_isStructArray(FORM_STRUCT_ARRAY));
    CHECK(Type_isStructSOA(FORM_ARRAY_SOA));
    CHECK(Type_isStructAOS(FORM_ARRAY_AOS));
    CHECK(Type_isStructCoexistent(FORM_STRUCT_COEXISTENT));
    CHECK(Type_isStructPointer(FORM_STRUCT_POINTER));
    CHECK(Type_isPrimitive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u)));
    CHECK(!Type_isPrimitive(FORM_STRUCT_SINGLETON));

    // --- Modifier / wrapper predicates.
    CHECK(Type_isGlobal(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | MOD_GLOBAL));
    CHECK(Type_isLocale(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | MOD_LOCALE));
    CHECK(Type_isReactive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP_REACTIVE));
    CHECK(Type_isProactive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP_PROACTIVE));
    CHECK(Type_isProbable(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_PROBABLE));
    CHECK(Type_isProbableObjects(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_PROBABLE_OBJECTS));
    CHECK(Type_isFuture(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_FUTURE));
    CHECK(Type_isChoice(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_CHOICE));
    CHECK(!Type_isReactive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP_PROACTIVE));

    // --- Architecture byte: each project reports itself; bare ids are vexspoke.
    CHECK(Type_arch(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u)) == ARCH_VEXSPOKE);
    CHECK(Type_arch(1u) == ARCH_VEXSPOKE);                   // bare id -> vexspoke
    CHECK(Type_arch(projByte(2) | 1u) == ARCH_GRAPHVEX);
    CHECK(Type_arch(projByte(3) | 1u) == ARCH_HOTCWAP);
    CHECK(Type_arch(projByte(4) | 1u) == ARCH_DARLING);
    CHECK(Type_arch(projByte(5) | 1u) == ARCH_APIHAVEN);
    CHECK(Type_arch(projByte(6) | 1u) == ARCH_DARKBASE);
    CHECK(Type_isDarkbase(projByte(6) | 1u));
    CHECK(Type_isDarling(projByte(4) | 1u));

    // --- Bare (vexspoke) chain: buffer family 0x50..0x63 rolls up to 0x4A.
    static const uint32_t bare[0x64] = { [0x50 ... 0x63] = 0x4Au };
    CHECK(Type_registerBareParents(bare, 0x64u));
    CHECK(Type_getParentClass(0x50u) == 0x4Au);
    CHECK(Type_getParentClass(0x63u) == 0x4Au);
    CHECK(Type_getParentClass(0x4Au) == 0x4Au);              // the root
    CHECK(Type_getParentClass(0x01u) == 0x01u);              // bare root

    // --- Registry seam: ADVERSARIAL rejections are atomic (false, no state).
    static const uint32_t chain[4] = { 0, 0, 1, 2 };
    CHECK(!Type_registerParents(0u, chain, 4));                         // zero project
    CHECK(!Type_registerParents(0x1ull, chain, 4));                     // stray bits
    CHECK(!Type_registerParents(PROJ_VEXSPOKE, chain, 4));              // vexspoke itself
    CHECK(!Type_registerParents(projByte(0x20), nullptr, 3));           // null table, count!=0
    CHECK(Type_registerParents(projByte(0x20), nullptr, 0));            // null table, count==0 ok
    CHECK(Type_getParentClass(projByte(0x20) | 9) == 9);                // unlisted -> root

    // --- A real chain: 3 -> 2 -> 1 (root).
    CHECK(Type_registerParents(projByte(0x21), chain, 4));
    CHECK(Type_getParentClass(projByte(0x21) | 3) == 2u);
    CHECK(Type_getParentClass(projByte(0x21) | 2) == 1u);
    CHECK(Type_getParentClass(projByte(0x21) | 1) == 1u);               // 0 row = root
    CHECK(Type_getParentClass(projByte(0x21) | 0) == 0u);              // class 0 -> root
    CHECK(Type_getParentClass(projByte(0x21) | 99) == 99u);            // past count -> root
    CHECK(Type_isA(projByte(0x21) | 3, projByte(0x21) | 1));
    CHECK(Type_isA(projByte(0x21) | 1, projByte(0x21) | 1));            // self
    CHECK(!Type_isA(projByte(0x21) | 1, projByte(0x21) | 3));           // ancestor is not descendant

    // --- Cycles and out-of-range parents are REJECTED atomically; a self-parent
    //     is a ROOT encoding, not a cycle, so it is accepted.
    static const uint32_t cyc[4]   = { 0, 0, 3, 2 };  // 2 -> 3, 3 -> 2 (cycle)
    static const uint32_t oob[4]   = { 0, 0, 1, 9 };  // 3 -> 9 (out of range)
    static const uint32_t selfp[4] = { 0, 1, 0, 1 };  // 1 self-root; 3 -> 1
    CHECK(!Type_registerParents(projByte(0x22), cyc, 4));
    CHECK(!Type_registerParents(projByte(0x24), oob, 4));
    CHECK(Type_getParentClass(projByte(0x21) | 3) == 2u);               // prior table intact
    CHECK(Type_registerParents(projByte(0x23), selfp, 4));               // self-parent = root
    CHECK(Type_getParentClass(projByte(0x23) | 1) == 1u);
    CHECK(Type_isA(projByte(0x23) | 1, projByte(0x23) | 1));
    CHECK(!Type_isA(projByte(0x23) | 1, projByte(0x23) | 2));            // 2 is a root

    // --- Bounded progress: a rejected cyclic project resolves as roots, so the
    //     walk returns instead of spinning (the Bounded Wait Law).
    CHECK(!Type_isA(projByte(0x22) | 1, projByte(0x22) | 3));
    CHECK(!Type_isA(projByte(0x23) | 1, projByte(0x23) | 3));            // 3 is not an ancestor

    // --- CROSS-PROJECT pin: the ancestor target is masked to class only, so the
    //     documented project-invariant match holds. This is intentional
    //     (type/type.h); a future policy change must update this row, not
    //     silently change behavior.
    CHECK(Type_isA(projByte(0x21) | 1, projByte(0x30) | 1));            // class 1 == class 1
    CHECK(Type_isA(projByte(0x30) | 1, 1u));                            // bare target mixes
    CHECK(!Type_isA(projByte(0x30) | 2, 1u));                           // class 2 != class 1

    // --- Idempotence: re-registering a project replaces its table.
    static const uint32_t chain2[4] = { 0, 0, 0, 1 };
    CHECK(Type_registerParents(projByte(0x21), chain2, 4));
    CHECK(Type_getParentClass(projByte(0x21) | 3) == 1u);               // new table won

    // --- Exponential slate growth: many projects must all resolve.
    static const uint32_t flat[4] = { 0, 0, 0, 0 };
    for (uint64_t b = 0x30; b < 0x30 + 24; b++)
        CHECK(Type_registerParents(projByte(b), flat, 4));
    for (uint64_t b = 0x30; b < 0x30 + 24; b++) {
        CHECK(Type_getParentClass(projByte(b) | 1) == 1u);              // flat -> root
        CHECK(Type_getParentClass(projByte(b) | 2) == 2u);
    }

    if (g_failures == 0) {
        printf("type_test: all assertions held\n");
        return 0;
    }
    printf("type_test: %d FAILURES\n", g_failures);
    return 1;
}
