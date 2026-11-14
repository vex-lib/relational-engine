/* Companion C-client proof for ffi/memory's standalone learning ABI. */

#include "vex.h"
#include "relational_memory.h"
#include <assert.h>
#include <string.h>
/* Exercises the standalone C ABI client's copy/read behavior and safe rejection preservation. */
int main() {
    ReMemory *owner = re_memory_new();
    u64 id = 0;
    const u8 hello[] = {104, 101, 108, 108, 111};
    assert(re_memory_copy(owner, hello, sizeof hello, &id) == 0);
    u8 output[5] = {0};
    bytes length = 99;
    assert(re_memory_read(owner, id, output, 4, &length) == 4);
    assert(length == 99 && output[0] == 0);
    assert(re_memory_read(owner, id, output, sizeof output, &length) == 0);
    assert(length == sizeof hello && memcmp(output, hello, sizeof hello) == 0);
    re_memory_drop(owner);
    re_memory_drop(nullptr);
    return 0;
}
