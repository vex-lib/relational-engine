/* Engine-owned extern handshake against real resident Rust storage.
 * Not a Hotcwap loader test or default allocator migration. */
#include "nio/relational_memory.h"
#include <assert.h>
#include <string.h>

int main(void) {
    ReMemory *owner = re_memory_new();
    uint64_t byteId = 99;
    assert(re_memory_new_atomic_byte(owner, 0, &byteId) == 0);
    uint8_t value = 99;
    assert(re_memory_get_atomic_byte(owner, byteId, &value) == 0 && value == 0);
    assert(re_memory_set_atomic_byte(owner, byteId, 255) == 0);
    assert(re_memory_get_atomic_byte(owner, byteId, &value) == 0 && value == 255);
    assert(re_memory_get_atomic_byte(nullptr, byteId, &value) == 1);
    assert(re_memory_get_atomic_byte(owner, byteId, nullptr) == 1);
    assert(re_memory_set_atomic_byte(nullptr, byteId, 0) == 1);
    assert(re_memory_get_byte(owner, byteId, 0, &value) == 5);
    assert(value == 255);

    const uint8_t initial[] = {1, 0, 255};
    uint64_t bytesId = 0;
    assert(re_memory_copy(owner, initial, sizeof initial, &bytesId) == 0);
    assert(re_memory_get_byte(owner, bytesId, 2, &value) == 0 && value == 255);
    assert(re_memory_get_byte(owner, bytesId, 3, &value) == 6);
    assert(re_memory_get_atomic_byte(owner, bytesId, &value) == 5);
    assert(re_memory_get_atomic_byte(owner, 0, &value) == 3);

    uint64_t stringId = 99;
    const size_t retentionBudget = 4096; /* Explicit caller-selected test budget. */
    assert(re_memory_new_atomic_string(owner, initial, sizeof initial, retentionBudget, &stringId) == 0);
    uint8_t destination[8] = {99};
    size_t length = 99;
    bool truncated = false;
    assert(re_memory_get_atomic_string(owner, stringId, destination, 2, &length, &truncated) == 4);
    assert(truncated && length == 99 && destination[0] == 99);
    assert(re_memory_get_atomic_string(owner, stringId, destination, sizeof destination, &length, &truncated) == 0);
    assert(!truncated && length == sizeof initial && memcmp(destination, initial, length) == 0);
    const uint8_t replacement[] = {7, 7, 7, 7, 7};
    assert(re_memory_set_atomic_string(owner, stringId, replacement, sizeof replacement) == 0);
    assert(re_memory_get_atomic_string(owner, stringId, destination, sizeof destination, &length, &truncated) == 0);
    assert(length == sizeof replacement && memcmp(destination, replacement, length) == 0);
    assert(re_memory_set_atomic_string(owner, stringId, nullptr, 1) == 1);
    assert(re_memory_set_atomic_string(owner, byteId, initial, sizeof initial) == 5);
    assert(re_memory_new_atomic_string(owner, initial, sizeof initial, 0, &stringId) == 4);
    assert(re_memory_get_atomic_string(owner, stringId, destination, sizeof destination, &length, &truncated) == 0);
    assert(memcmp(destination, replacement, length) == 0);
    assert(re_memory_get_atomic_string(owner, stringId, destination, sizeof destination, nullptr, &truncated) == 1);
    assert(re_memory_get_atomic_string(owner, stringId, destination, sizeof destination, &length, nullptr) == 1);
    assert(re_memory_set_atomic_string(owner, stringId, nullptr, 0) == 0);
    assert(re_memory_get_atomic_string(owner, stringId, nullptr, 0, &length, &truncated) == 0 && length == 0);
    re_memory_drop(owner);
    return 0;
}
