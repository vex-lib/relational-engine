// vector_test.c — the SIMD-sized vector and matrix primitives.
//
// Proves the byte geometry, the zeroed padding lane, the column-major matrix
// copy, identity construction, the SIMD data view, and both string
// projections. The null-source cases intentionally call THROW, so a passing
// run writes the expected [vex] diagnostics to stderr.

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "re/primitive/mat3f32.h"
#include "re/primitive/mat3f64.h"
#include "re/primitive/mat4f32.h"
#include "re/primitive/mat4f64.h"
#include "re/primitive/vec2f32.h"
#include "re/primitive/vec2i32.h"
#include "re/primitive/vec3f32.h"
#include "re/primitive/vec3i32.h"
#include "re/primitive/vec4f32.h"
#include "re/primitive/vec4i32.h"

static i32 failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                        \
        }                                                                      \
    } while (0)

_Static_assert(sizeof(vec2f32) == 8, "vec2f32 is 8 bytes");
_Static_assert(_Alignof(vec2f32) == 8, "vec2f32 aligns to 8");
_Static_assert(sizeof(vec3f32) == 16, "vec3f32 is 16 bytes");
_Static_assert(_Alignof(vec3f32) == 16, "vec3f32 aligns to 16");
_Static_assert(sizeof(vec4f32) == 16, "vec4f32 is 16 bytes");
_Static_assert(_Alignof(vec4f32) == 16, "vec4f32 aligns to 16");
_Static_assert(sizeof(vec2i32) == 8, "vec2i32 is 8 bytes");
_Static_assert(sizeof(vec3i32) == 16, "vec3i32 is 16 bytes");
_Static_assert(sizeof(vec4i32) == 16, "vec4i32 is 16 bytes");
_Static_assert(sizeof(mat3f32) == 48, "mat3f32 is 48 bytes");
_Static_assert(_Alignof(mat3f32) == 16, "mat3f32 aligns to 16");
_Static_assert(sizeof(mat4f32) == 64, "mat4f32 is 64 bytes");
_Static_assert(_Alignof(mat4f32) == 16, "mat4f32 aligns to 16");
_Static_assert(sizeof(mat3f64) == 96, "mat3f64 is 96 bytes");
_Static_assert(_Alignof(mat3f64) == 32, "mat3f64 aligns to 32");
_Static_assert(sizeof(mat4f64) == 128, "mat4f64 is 128 bytes");
_Static_assert(_Alignof(mat4f64) == 32, "mat4f64 aligns to 32");

int main(void) {
    // Vectors: component constructors and the zero vector.
    vec2f32 a = vec2f32(1.0f, 2.0f);
    CHECK(a.x == 1.0f && a.y == 2.0f);
    CHECK(a.data[0] == 1.0f && a.data[1] == 2.0f);

    vec3f32 b = vec3f32(3.0f, 4.0f, 5.0f);
    CHECK(b.x == 3.0f && b.y == 4.0f && b.z == 5.0f);
    CHECK(b.data[0] == b.x && b.data[1] == b.y && b.data[2] == b.z);
    CHECK(b.data[3] == 0.0f);
    CHECK(b._pad == 0.0f);

    vec3f32 bZero = vec3f32_0();
    CHECK(bZero.x == 0.0f && bZero.y == 0.0f && bZero.z == 0.0f);
    CHECK(bZero._pad == 0.0f);

    vec4f32 c = vec4f32(6.0f, 7.0f, 8.0f, 9.0f);
    CHECK(c.x == 6.0f && c.y == 7.0f && c.z == 8.0f && c.w == 9.0f);
    CHECK(c.data[3] == 9.0f);

    vec2i32 ia = vec2i32(10, 11);
    CHECK(ia.x == 10 && ia.y == 11);
    vec3i32 ib = vec3i32(12, 13, 14);
    CHECK(ib.x == 12 && ib.y == 13 && ib.z == 14 && ib._pad == 0);
    vec4i32 ic = vec4i32(15, 16, 17, 18);
    CHECK(ic.x == 15 && ic.y == 16 && ic.z == 17 && ic.w == 18);

    // f32 matrices: identity and column-major copy.
    mat3f32 identity3 = mat3f32_0();
    CHECK(identity3.column[0].x == 1.0f && identity3.column[0].y == 0.0f);
    CHECK(identity3.column[1].y == 1.0f && identity3.column[2].z == 1.0f);
    CHECK(identity3.column[0].w == 0.0f && identity3.column[2].w == 0.0f);

    f32 nine[9] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f };
    mat3f32 copied3 = mat3f32_1(nine);
    CHECK(copied3.column[0].x == 1.0f && copied3.column[0].y == 2.0f && copied3.column[0].z == 3.0f);
    CHECK(copied3.column[1].x == 4.0f && copied3.column[1].y == 5.0f && copied3.column[1].z == 6.0f);
    CHECK(copied3.column[2].x == 7.0f && copied3.column[2].y == 8.0f && copied3.column[2].z == 9.0f);
    CHECK(copied3.column[0].w == 0.0f && copied3.column[2].w == 0.0f);

    mat4f32 identity4 = mat4f32_0();
    CHECK(identity4.column[0].x == 1.0f && identity4.column[1].y == 1.0f);
    CHECK(identity4.column[2].z == 1.0f && identity4.column[3].w == 1.0f);
    CHECK(identity4.column[0].y == 0.0f && identity4.column[3].x == 0.0f);

    f32 sixteen[16] = {
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    };
    mat4f32 copied4 = mat4f32_1(sixteen);
    CHECK(copied4.column[0].x == 1.0f && copied4.column[0].w == 4.0f);
    CHECK(copied4.column[1].x == 5.0f && copied4.column[1].w == 8.0f);
    CHECK(copied4.column[3].x == 13.0f && copied4.column[3].w == 16.0f);

    // f64 matrices: identity and column-major copy.
    mat3f64 identity3d = mat3f64_0();
    CHECK(identity3d.column[0].x == 1.0 && identity3d.column[1].y == 1.0);
    CHECK(identity3d.column[2].z == 1.0 && identity3d.column[0]._pad == 0.0);

    f64 nined[9] = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0 };
    mat3f64 copied3d = mat3f64_1(nined);
    CHECK(copied3d.column[0].x == 1.0 && copied3d.column[0].z == 3.0);
    CHECK(copied3d.column[2].x == 7.0 && copied3d.column[2].z == 9.0);
    CHECK(copied3d.column[0]._pad == 0.0 && copied3d.column[2]._pad == 0.0);

    mat4f64 identity4d = mat4f64_0();
    CHECK(identity4d.column[0].x == 1.0 && identity4d.column[3].w == 1.0);

    f64 sixteend[16] = {
        1.0, 2.0, 3.0, 4.0,
        5.0, 6.0, 7.0, 8.0,
        9.0, 10.0, 11.0, 12.0,
        13.0, 14.0, 15.0, 16.0
    };
    mat4f64 copied4d = mat4f64_1(sixteend);
    CHECK(copied4d.column[0].x == 1.0 && copied4d.column[0].w == 4.0);
    CHECK(copied4d.column[3].x == 13.0 && copied4d.column[3].w == 16.0);

    // A null column source is a cold rejection that returns identity.
    mat3f32 rejected3 = mat3f32_1(nullptr);
    CHECK(rejected3.column[0].x == 1.0f && rejected3.column[1].y == 1.0f);
    mat4f64 rejected4 = mat4f64_1(nullptr);
    CHECK(rejected4.column[0].x == 1.0 && rejected4.column[3].w == 1.0);

    // String projections.
    char buffer[512];
    bool truncated = false;
    vec3f32_toString(&b, buffer, sizeof buffer, &truncated);
    CHECK(!truncated);
    CHECK(strstr(buffer, "vec3f32(x=3, y=4, z=5)") != nullptr);
    vec3f32_toStringStruct(&b, buffer, sizeof buffer, &truncated);
    CHECK(!truncated);
    CHECK(strstr(buffer, "pad=0") != nullptr);
    CHECK(strstr(buffer, "bytes=16") != nullptr);

    mat3f32_toString(&copied3, buffer, sizeof buffer, &truncated);
    CHECK(!truncated);
    CHECK(strstr(buffer, "c0=(1,2,3)") != nullptr);
    mat3f32_toStringStruct(&copied3, buffer, sizeof buffer, &truncated);
    CHECK(!truncated);
    CHECK(strstr(buffer, "padZero=true") != nullptr);
    CHECK(strstr(buffer, "bytes=48") != nullptr);

    vec2f32_toString(nullptr, buffer, sizeof buffer, &truncated);
    CHECK(strcmp(buffer, "nullptr") == 0);
    vec4i32_toString(&ic, buffer, sizeof buffer, &truncated);
    CHECK(strstr(buffer, "vec4i32(x=15") != nullptr);

    if (failures != 0) {
        fprintf(stderr, "vector_test: %" PRId32 " failure(s)\n", failures);
        return 1;
    }
    printf("vector_test: geometry, padding, columns, identity, projections\n");
    return 0;
}
