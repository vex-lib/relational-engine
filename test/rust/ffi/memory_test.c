/* Companion C-client proof for ffi/memory's standalone learning ABI. */
#include "relational_memory.h"
#include <assert.h>
#include <string.h>
/* Exercises the standalone C ABI client's copy/read behavior and safe rejection preservation. */
int main(void) {
    ReMemory *owner = re_memory_new();
    uint64_t id = 0;
    const uint8_t hello[] = {104, 101, 108, 108, 111};
    assert(re_memory_copy(owner, hello, sizeof hello, &id) == 0);
    uint8_t output[5] = {0};
    size_t length = 99;
    assert(re_memory_read(owner, id, output, 4, &length) == 4);
    assert(length == 99 && output[0] == 0);
    assert(re_memory_read(owner, id, output, sizeof output, &length) == 0);
    assert(length == sizeof hello && memcmp(output, hello, sizeof hello) == 0);
    re_memory_drop(owner);
    re_memory_drop(nullptr);
    return 0;
}
