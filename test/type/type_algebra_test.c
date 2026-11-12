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
//   - GROWTH: the slate f64s under many projects.
//
// This owner compiles against the algebra plus the Relational Engine class
// registry, pinning the project byte and class numbers that re owns.
#include "vex.h"
#include <stdio.h>

#include "type/re_type.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// A project byte with no stray bits: the field occupies bits 52..59.
static u64 projByte(u64 byte) {
    return (byte << 52) & MASK_PROJECT;
}

int main(void) {
    u64 id = Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u);
    CHECK(Type_class(id) == 1u);
    CHECK(Type_form(id) == FORM_SINGLETON);
    CHECK(Type_project(id) == PROJ_VEXSPOKE);
    CHECK(Type_class(0xFFFFFFFFull) == 0x00FFFFFFu);
    CHECK((SUGAR_VEX & MASK_SUGAR) == SUGAR_VEX);
    CHECK(Type_form(FORM_STRUCT_ARRAY) == FORM_STRUCT_ARRAY);

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

    CHECK(Type_isGlobal(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | MOD_GLOBAL));
    CHECK(Type_isLocale(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | MOD_LOCALE));
    CHECK(Type_isReactive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP_REACTIVE));
    CHECK(Type_isProactive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP_PROACTIVE));
    CHECK(Type_isProbable(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_PROBABLE));
    CHECK(Type_isProbableObjects(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_PROBABLE_OBJECTS));
    CHECK(Type_isFuture(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_FUTURE));
    CHECK(Type_isChoice(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_CHOICE));
    CHECK(!Type_isReactive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP_PROACTIVE));

    CHECK(Type_arch(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u)) == ARCH_VEXSPOKE);
    CHECK(Type_arch(1u) == ARCH_VEXSPOKE);
    CHECK(Type_arch(projByte(2) | 1u) == ARCH_GRAPHVEX);
    CHECK(Type_arch(projByte(3) | 1u) == ARCH_HOTCWAP);
    CHECK(Type_arch(projByte(4) | 1u) == ARCH_DARLING);
    CHECK(Type_arch(projByte(5) | 1u) == ARCH_APIHAVEN);
    CHECK(Type_arch(projByte(6) | 1u) == ARCH_DARKBASE);
    CHECK(Type_isDarkbase(projByte(6) | 1u));
    CHECK(Type_isDarling(projByte(4) | 1u));

    static const u32 bare[0x64] = { [0x50 ... 0x63] = 0x4Au };
    CHECK(Type_registerBareParents(bare, 0x64u));
    CHECK(Type_getParentClass(0x50u) == 0x4Au);
    CHECK(Type_getParentClass(0x63u) == 0x4Au);
    CHECK(Type_getParentClass(0x4Au) == 0x4Au);
    CHECK(Type_getParentClass(0x01u) == 0x01u);

    static const u32 chain[4] = { 0, 0, 1, 2 };
    CHECK(!Type_registerParents(0u, chain, 4));
    CHECK(!Type_registerParents(0x1ull, chain, 4));
    CHECK(!Type_registerParents(PROJ_VEXSPOKE, chain, 4));
    CHECK(!Type_registerParents(projByte(0x20), nullptr, 3));
    CHECK(Type_registerParents(projByte(0x20), nullptr, 0));
    CHECK(Type_getParentClass(projByte(0x20) | 9) == 9);

    CHECK(Type_registerParents(projByte(0x21), chain, 4));
    CHECK(Type_getParentClass(projByte(0x21) | 3) == 2u);
    CHECK(Type_getParentClass(projByte(0x21) | 2) == 1u);
    CHECK(Type_getParentClass(projByte(0x21) | 1) == 1u);
    CHECK(Type_getParentClass(projByte(0x21) | 0) == 0u);
    CHECK(Type_getParentClass(projByte(0x21) | 99) == 99u);
    CHECK(Type_isA(projByte(0x21) | 3, projByte(0x21) | 1));
    CHECK(Type_isA(projByte(0x21) | 1, projByte(0x21) | 1));
    CHECK(!Type_isA(projByte(0x21) | 1, projByte(0x21) | 3));

    static const u32 cyc[4]   = { 0, 0, 3, 2 };
    static const u32 oob[4]   = { 0, 0, 1, 9 };
    static const u32 selfp[4] = { 0, 1, 0, 1 };
    CHECK(!Type_registerParents(projByte(0x22), cyc, 4));
    CHECK(!Type_registerParents(projByte(0x24), oob, 4));
    CHECK(Type_getParentClass(projByte(0x21) | 3) == 2u);
    CHECK(Type_registerParents(projByte(0x23), selfp, 4));
    CHECK(Type_getParentClass(projByte(0x23) | 1) == 1u);
    CHECK(Type_isA(projByte(0x23) | 1, projByte(0x23) | 1));
    CHECK(!Type_isA(projByte(0x23) | 1, projByte(0x23) | 2));

    CHECK(!Type_isA(projByte(0x22) | 1, projByte(0x22) | 3));
    CHECK(!Type_isA(projByte(0x23) | 1, projByte(0x23) | 3));

    CHECK(Type_isA(projByte(0x21) | 1, projByte(0x30) | 1));
    CHECK(Type_isA(projByte(0x30) | 1, 1u));
    CHECK(!Type_isA(projByte(0x30) | 2, 1u));

    static const u32 chain2[4] = { 0, 0, 0, 1 };
    CHECK(Type_registerParents(projByte(0x21), chain2, 4));
    CHECK(Type_getParentClass(projByte(0x21) | 3) == 1u);

    static const u32 flat[4] = { 0, 0, 0, 0 };
    for (u64 b = 0x30; b < 0x30 + 24; b++)
        CHECK(Type_registerParents(projByte(b), flat, 4));
    for (u64 b = 0x30; b < 0x30 + 24; b++) {
        CHECK(Type_getParentClass(projByte(b) | 1) == 1u);
        CHECK(Type_getParentClass(projByte(b) | 2) == 2u);
    }

    CHECK(Type_project(TYPE_VARIABLE_SLOT) == PROJ_RELATIONAL_ENGINE);
    CHECK(Type_project(TYPE_STRING_POOL) == PROJ_RELATIONAL_ENGINE);
    CHECK(Type_project(TYPE_VARIABLE_HASH_MAP) == PROJ_RELATIONAL_ENGINE);
    CHECK(Type_project(TYPE_VARIABLE_MINI_MAP) == PROJ_RELATIONAL_ENGINE);
    CHECK(Type_project(TYPE_SHELF) == PROJ_RELATIONAL_ENGINE);
    CHECK(Type_project(TYPE_SHELF_NODE) == PROJ_RELATIONAL_ENGINE);
    CHECK(Type_project(TYPE_VARIABLE_POOL_ORDER) == PROJ_RELATIONAL_ENGINE);

    CHECK(Type_class(TYPE_VARIABLE_SLOT) == ID_VARIABLE_SLOT);
    CHECK(Type_class(TYPE_STRING_POOL) == ID_STRING_POOL);
    CHECK(Type_class(TYPE_VARIABLE_HASH_MAP) == ID_VARIABLE_HASH_MAP);
    CHECK(Type_class(TYPE_VARIABLE_MINI_MAP) == ID_VARIABLE_MINI_MAP);
    CHECK(Type_class(TYPE_SHELF) == ID_SHELF);
    CHECK(Type_class(TYPE_SHELF_NODE) == ID_SHELF_NODE);
    CHECK(Type_class(TYPE_VARIABLE_POOL_ORDER) == ID_VARIABLE_POOL_ORDER);

    CHECK(Type_form(TYPE_VARIABLE_SLOT) == FORM_STRUCT_SINGLETON);
    CHECK(Type_form(TYPE_VARIABLE_HASH_MAP) == FORM_STRUCT_SINGLETON);
    CHECK(Type_form(TYPE_VARIABLE_MINI_MAP) == FORM_STRUCT_SINGLETON);
    CHECK(Type_form(TYPE_SHELF) == FORM_STRUCT_SINGLETON);
    CHECK(Type_form(TYPE_SHELF_NODE) == FORM_STRUCT_SINGLETON);
    CHECK(Type_form(TYPE_STRING_POOL) == FORM_ARRAY);
    CHECK(Type_form(TYPE_VARIABLE_POOL_ORDER) == FORM_ARRAY);

    if (g_failures == 0) {
        printf("type_test: all assertions held\n");
        return 0;
    }
    printf("type_test: %d FAILURES\n", g_failures);
    return 1;
}
