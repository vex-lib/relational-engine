// re/test/primitive/buffer_test.c — owner test for primitive/buffer.
//
// Proves the contiguous byte buffer:
//   - Buffer() empty, Buffer(n) zeroed, Buffer(bytes, n) copies;
//   - append grows by cold doubling, bytes stay contiguous;
//   - setLength clamps, reserve grows, null-safe everywhere;
//   - free releases payload + record.

#include "vex.h"
#include "re.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static i32 g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

i32 main() {
    Buffer *e = Buffer();
    CHECK(e != nullptr);
    CHECK(Buffer_length(e) == 0);
    CHECK(Buffer_capacity(e) == 0);
    CHECK(Buffer_bytes(e) == nullptr);

    Buffer *sized = Buffer((bytes) 16);
    CHECK(sized != nullptr);
    CHECK(Buffer_length(sized) == 16);
    CHECK(Buffer_capacity(sized) == 16);
    CHECK(Buffer_bytes(sized) != nullptr);
    CHECK(Buffer_bytes(sized)[0] == 0);
    CHECK(Buffer_bytes(sized)[15] == 0);

    const u8 src[4] = { 1, 2, 3, 4 };
    Buffer *copied = Buffer(src, sizeof(src));
    CHECK(copied != nullptr);
    CHECK(Buffer_length(copied) == 4);
    CHECK(memcmp(Buffer_bytes(copied), src, 4) == 0);
    CHECK(Buffer_bytes(copied) != src);

    const u8 more[4] = { 5, 6, 7, 8 };
    CHECK(Buffer_append(copied, more, sizeof(more)));
    CHECK(Buffer_length(copied) == 8);
    u8 want[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(memcmp(Buffer_bytes(copied), want, 8) == 0);

    CHECK(Buffer_setLength(copied, 3));
    CHECK(Buffer_length(copied) == 3);
    CHECK(!Buffer_setLength(copied, Buffer_capacity(copied) + 1));

    bytes grown = Buffer_capacity(copied);
    CHECK(Buffer_reserve(copied, grown + 128));
    CHECK(Buffer_capacity(copied) >= grown + 128);
    CHECK(Buffer_length(copied) == 3);

    CHECK(!Buffer_append(nullptr, more, 1));
    CHECK(!Buffer_append(copied, nullptr, 1));
    CHECK(Buffer_append(copied, more, 0));
    CHECK(Buffer_length(nullptr) == 0);
    CHECK(Buffer_bytes(nullptr) == nullptr);
    Buffer_free(nullptr);

    Buffer_free(e);
    Buffer_free(sized);
    Buffer_free(copied);

    if (g_failures == 0)
        printf("buffer_test: PASS\n");
    else
        printf("buffer_test: %d FAILURES\n", g_failures);
    return g_failures != 0;
}
