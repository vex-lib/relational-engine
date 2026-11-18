// vex/re/test/struct/stride_test.c — the Stride class _test.
//
// Proves re/struct/stride.c: the built-in class-id -> byte-width table,
// unknown defaults, and custom struct registration (`Stride_register`,
// `Stride_isRegistered`, `Stride_resetCustom`) plus `List` / `Array` backing
// custom structs. Pure, stateless, headless.

#include "vex.h"
#include <stdio.h>
#include <string.h>

#include "nio/mem.h"
#include "struct/array.h"
#include "struct/list.h"
#include "struct/stride.h"
#include "type/re_type.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

typedef struct TestCustomVertex {
    alignas(16) f32 x;
    f32 y;
    f32 z;
    u32 color;
    d64 weight;
} TestCustomVertex;

;;GETTER
static inline u32 TestCustomVertex_color(const TestCustomVertex *v) { return (*v).color; }
;;GETTER
static inline d64 TestCustomVertex_weight(const TestCustomVertex *v) { return (*v).weight; }

// Checks built-in mappings, custom struct registration, and container integration.
i32 main() {
    // Scalar widths.
    CHECK(Stride_get(ID_BYTE) == 1);

    CHECK(Stride_get(ID_SHORT) == 2);

    CHECK(Stride_get(ID_INT) == 4);

    CHECK(Stride_get(ID_FLOAT) == 4);

    CHECK(Stride_get(ID_LONG) == 8);

    CHECK(Stride_get(ID_f64) == 8);

    // Geometry + outliers.
    CHECK(Stride_get(ID_VEC2) == 8);

    CHECK(Stride_get(ID_VEC3) == 12);

    CHECK(Stride_get(ID_VEC4) == 16);

    CHECK(Stride_get(ID_MAT4) == 64);

    CHECK(Stride_get(ID_VARIABLE) == 40);

    // Unknown class ids answer the pointer-sized default.
    CHECK(Stride_get(0x7FFFu) == 8);

    // A full TYPE_ id is masked to its class field before lookup.
    CHECK(Stride_get((u32) TYPE_VEC3_SINGLETON) == 12);

    CHECK(Stride_get((u32) TYPE_VEC4_SINGLETON) == 16);

    // Custom struct registration + rejections.
    Stride_resetCustom();

    u64 customType = Type_make(PROJ_RELATIONAL_ENGINE, FORM_STRUCT_SINGLETON, 0x4200u);

    CHECK(!Stride_isRegistered(customType));

    CHECK(!Stride_register(0u, 32u));

    CHECK(!Stride_register(customType, 0u));

    CHECK(!Stride_register(customType, 24u));

    CHECK(!Stride_register(ID_INT, 32u));

    CHECK(!Stride_register(ID_VEC3, 32u));

    CHECK(STRIDE_REGISTER(customType, TestCustomVertex));

    CHECK(Stride_isRegistered(customType));

    CHECK(Stride_isRegistered(0x4200u));

    CHECK(Stride_get((u32) customType) == sizeof(TestCustomVertex));

    CHECK(Stride_get(0x4200u) == sizeof(TestCustomVertex));

    // Update existing custom stride in place.
    CHECK(Stride_register(0x4201u, 16u));

    CHECK(Stride_get(0x4201u) == 16u);

    CHECK(Stride_register(0x4201u, 48u));

    CHECK(Stride_get(0x4201u) == 48u);

    // Containers respect custom registered struct strides.
    CHECK(Memory_init(0));

    List *verts = List_2((u32) customType, 4u);

    Array *arr = Array(0x4200u, 2u);

    CHECK(verts != nullptr && List_stride(verts) == sizeof(TestCustomVertex));

    CHECK(arr != nullptr && Array_stride(arr) == sizeof(TestCustomVertex));

    TestCustomVertex *vslot = (TestCustomVertex*) List_addSlot(verts);

    CHECK(vslot != nullptr && ((pointer) vslot & 15u) == 0u);

    if (vslot) 
        (*vslot) = (TestCustomVertex){ .x = 1.0f, .y = 2.0f, .z = 3.0f, .color = 0xFF00AA55u, .weight = 0.5 };

    TestCustomVertex *aslot = (TestCustomVertex*) Array_slot(arr, 1u);

    CHECK(aslot != nullptr && ((pointer) aslot & 15u) == 0u);

    if (aslot && vslot) 
        (*aslot) = (*vslot);

    CHECK(aslot != nullptr && TestCustomVertex_color(aslot) == 0xFF00AA55u && TestCustomVertex_weight(aslot) == 0.5);

    List_free(verts);

    Array_free(arr);

    Stride_resetCustom();

    CHECK(!Stride_isRegistered(customType));

    CHECK(Stride_get((u32) customType) == 8u);

    if (g_failures == 0) {
        printf("stride_test: all assertions held\n");

        return 0;
    }

    printf("stride_test: %d FAILURES\n", g_failures);

    return 1;
}
