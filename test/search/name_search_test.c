// Owner: engine C strided-name primitive. No allocator or Rust atomic layout.
// Assert normal/empty/missing, exact/short keys, nulls and overflowing spans;
// rejection leaves output intact. Runner asserts exactly one diagnostic per bad call.
#include "search/primitives/name_search.h"
#include <assert.h>
#include "vex.h"
#include <string.h>

int main() {
    u8 rows[3][32] = {{0}};
    memcpy(rows[0], "alpha", 5);
    memcpy(rows[1], "beta", 4);
    memcpy(rows[2], "gamma", 5);
    u8 key[24] = {0};
    memcpy(key, "beta", 4);
    bytes result = 99;
    assert(re_name_search(&rows[0][0], sizeof(rows), 32, key, 24, &result) == 0 && result == 1);
    memcpy(key, "alpha", 5);
    assert(re_name_search(&rows[0][0], sizeof(rows), 32, key, 24, &result) == 0 && result == 0);
    memset(key, 0, sizeof(key));
    memcpy(key, "gamma", 5);
    assert(re_name_search(&rows[0][0], sizeof(rows), 32, key, 24, &result) == 0 && result == 2);
    key[0] = 'x';
    result = 99;
    assert(re_name_search(&rows[0][0], sizeof(rows), 32, key, 24, &result) == 1 && result == 99);
    assert(re_name_search(nullptr, 0, 24, key, 24, &result) == 1 && result == 99);
    assert(re_name_search(nullptr, 32, 32, key, 24, &result) == 2);
    assert(re_name_search(rows[0], 32, 32, nullptr, 24, &result) == 2);
    assert(re_name_search(rows[0], 32, 32, key, 24, nullptr) == 2);
    assert(re_name_search(rows[0], 32, 0, key, 24, &result) == 2);
    assert(re_name_search(rows[0], 32, 23, key, 24, &result) == 2);
    assert(re_name_search(rows[0], 31, 32, key, 24, &result) == 2);
    assert(re_name_search(rows[0], 32, 32, key, 23, &result) == 2);
    assert(re_name_search(rows[0], 32, 32, key, 25, &result) == 2);
    assert(re_name_search(rows[0], SIZE_MAX, 32, key, 24, &result) == 2);
    assert(re_name_search(rows[0], 32, 32, key, 0, &result) == 2);
    assert(result == 99);
    memset(key, 0, sizeof(key));
    memcpy(key, "beta", 4);
    assert(re_name_search(rows[0], sizeof(rows), 32, key, 24, &result) == 0 && result == 1);
    assert(rows[1][0] == 'b');
    return 0;
}
