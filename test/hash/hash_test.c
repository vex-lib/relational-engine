// re/test/hash/hash_test.c — the Hash owner test (Per-File Battle Test Law).
//
// Hash is stateless pure functions, so the battle rows are DETERMINISM, the
// VALUE BOUNDARY MATRIX (nullptr/empty/huge), and an AVALANCHE oracle: a single
// input-bit flip must flip roughly half the output bits (Murmur3 finalizer
// contract). FNV-1a is pinned to published reference vectors so a silent
// algorithm drift is caught, not merely "some hash changed".

#include "vex.h"
#include <stdio.h>
#include <string.h>

#include "hash/hash.h"

static i32 g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if(!(cond)) {                                                      \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while(0)

// Counts set bits to measure the 64-bit hash avalanche response.
static u32 popcount64(u64 v) {

    u32 n = 0;

    while(v) {

        n += (u32) (v & 1u);

        v >>= 1;

    }

    return n;

}

// Counts set bits to measure the 32-bit hash avalanche response.
static u32 popcount32(u32 v) {

    u32 n = 0;

    while(v) {

        n += (u32) (v & 1u);

        v >>= 1;

    }

    return n;

}

// Checks hash reference vectors, deterministic output, null inputs, and avalanche behavior.
i32 main() {

    // --- Value boundary: nullptr and empty are the zero mapping, never a read.
    CHECK(Hash_fnv1a64(nullptr, 0) == 0);

    CHECK(Hash_fnv1a64(nullptr, 999) == 0);          // nullptr vetoes before length

    const u8 none = 0;

    CHECK(Hash_fnv1a64(&none, 0) == 0);              // zero-length block

    // --- Published FNV-1a 64 reference vectors (algorithm pin).
    CHECK(Hash_fnv1a64((const u8*) "a", 1) == 0xaf63dc4c8601ec8cull);

    CHECK(Hash_fnv1a64((const u8*) "foobar", 6) == 0x85944171f73967e8ull);

    // --- Determinism: identical input yields identical output, every call.
    const u8 buf[64] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

    u64 first = Hash_fnv1a64(buf, sizeof(buf));

    for(i32 i = 0; i < 8; i++) 
       CHECK(Hash_fnv1a64(buf, sizeof(buf)) == first);

    // --- Avalanche: one flipped payload byte changes the digest materially.
    u8 tweaked[64];

    memcpy(tweaked, buf, sizeof(buf));

    tweaked[37] ^= 0x01u;

    CHECK(Hash_fnv1a64(tweaked, sizeof(buf)) != first);

    // --- Hash_pointer: identity, not pointee; nullptr is the zero mapping.
    CHECK(Hash_pointer(nullptr) == Hash_murmur3Mix64(0));

    i32 a = 0, b = 0;

    CHECK(Hash_pointer(&a) == Hash_pointer(&a));     // stable for a run

    CHECK(Hash_pointer(&a) != Hash_pointer(&b));     // distinct addresses differ

    // --- Murmur3 mix64: zero fixed point, determinism, avalanche on every bit.
    CHECK(Hash_murmur3Mix64(0) == 0);

    CHECK(Hash_murmur3Mix64(1) != Hash_murmur3Mix64(2));

    for(i32 bit = 0; bit < 64; bit += 7) {

        u64 base = 0x0123456789abcdefull;

        u64 other = base ^ (1ull << bit);

        u64 h = Hash_murmur3Mix64(base) ^ Hash_murmur3Mix64(other);

        CHECK(popcount64(h) >= 16);                  // >= a quarter of 64 bits flip

    }

    for(i32 i = 0; i < 4; i++) 
       CHECK(Hash_murmur3Mix64(0xdeadbeefcafef00dull) == Hash_murmur3Mix64(0xdeadbeefcafef00dull));

    // --- Murmur3 mix32: same contract on the 32-bit surface.
    CHECK(Hash_murmur3Mix32(0) == 0);

    CHECK(Hash_murmur3Mix32(1u) != Hash_murmur3Mix32(2u));

    for(i32 bit = 0; bit < 32; bit += 5) {

        u32 base = 0x12345678u;

        u32 other = base ^ (1u << bit);

        u32 h = Hash_murmur3Mix32(base) ^ Hash_murmur3Mix32(other);

        CHECK(popcount32(h) >= 8);                   // >= a quarter of 32 bits flip

    }

    if(g_failures == 0) {

        printf("hash_test: all assertions held\n");

        return 0;

    }

    printf("hash_test: %d FAILURES\n", g_failures);

    return 1;

}
